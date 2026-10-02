#include "shaderDx12.h"

#ifdef DX12_BACKEND
#include "d3dUtil.h"
#include "driverDx12.h"
#include "d3dx12/d3dx12.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include <resourceModule/serviceManager.h>
#include <resourceModule/fileSystem/fileSystem.h>
#include "utilsModule/log/loggerDefine.h"
#include "spirv_hlsl.hpp"
#include <cstring>
#include <vector>

using namespace IKIGAI::RENDER;

namespace {

struct ShaderStageProfile {
	ShaderType type;
	const wchar_t* profile;
	spv::ExecutionModel model;
};

constexpr ShaderStageProfile kStages[] = {
	{ShaderType::VERTEX, L"vs_6_6", spv::ExecutionModelVertex},
	{ShaderType::FRAGMENT, L"ps_6_6", spv::ExecutionModelFragment},
	{ShaderType::GEOMETRY, L"gs_6_6", spv::ExecutionModelGeometry},
	{ShaderType::TESSELLATION_CONTROL, L"hs_6_6", spv::ExecutionModelTessellationControl},
	{ShaderType::TESSELLATION_EVALUATION, L"ds_6_6", spv::ExecutionModelTessellationEvaluation},
	{ShaderType::COMPUTE, L"cs_6_6", spv::ExecutionModelGLCompute},
};

constexpr uint32_t kHlslShaderModel = 60;

bool IsTexture(ShaderReflection::UniformType type) {
	return type == ShaderReflection::UniformType::SAMPLER_2D ||
		type == ShaderReflection::UniformType::SAMPLER_3D ||
		type == ShaderReflection::UniformType::SAMPLER_CUBE ||
		type == ShaderReflection::UniformType::SAMPLER_2D_ARRAY;
}

bool HasStage(size_t mask, ShaderType type) {
	return (mask & static_cast<size_t>(type)) != 0;
}

D3D12_SHADER_VISIBILITY VisibilityForMask(size_t mask) {
	const bool vs = HasStage(mask, ShaderType::VERTEX);
	const bool ps = HasStage(mask, ShaderType::FRAGMENT);
	const bool gs = HasStage(mask, ShaderType::GEOMETRY);
	const bool hs = HasStage(mask, ShaderType::TESSELLATION_CONTROL);
	const bool ds = HasStage(mask, ShaderType::TESSELLATION_EVALUATION);
	const bool cs = HasStage(mask, ShaderType::COMPUTE);
	const int stages = static_cast<int>(vs) + static_cast<int>(ps) + static_cast<int>(gs) +
		static_cast<int>(hs) + static_cast<int>(ds) + static_cast<int>(cs);
	if (stages != 1 || cs) {
		return D3D12_SHADER_VISIBILITY_ALL;
	}
	if (vs) return D3D12_SHADER_VISIBILITY_VERTEX;
	if (ps) return D3D12_SHADER_VISIBILITY_PIXEL;
	if (gs) return D3D12_SHADER_VISIBILITY_GEOMETRY;
	if (hs) return D3D12_SHADER_VISIBILITY_HULL;
	if (ds) return D3D12_SHADER_VISIBILITY_DOMAIN;
	return D3D12_SHADER_VISIBILITY_ALL;
}

bool RegisterUsed(const std::vector<std::pair<size_t, size_t>>& used, size_t set, size_t bind) {
	for (const auto& slot : used) {
		if (slot.first == set && slot.second == bind) {
			return true;
		}
	}
	return false;
}

void ReadPushConstantBindings(const std::vector<uint32_t>& spirv, ShaderReflection& reflection) {
	spirv_cross::CompilerHLSL compiler(spirv);
	const auto resources = compiler.get_shader_resources();
	if (resources.push_constant_buffers.empty()) {
		return;
	}
	const auto id = resources.push_constant_buffers[0].id;
	for (auto& uniform : reflection.mUniforms) {
		if (uniform.mType != ShaderReflection::UniformType::PUSH_CONSTANT) {
			continue;
		}
		if (compiler.has_decoration(id, spv::DecorationBinding)) {
			uniform.mBind = compiler.get_decoration(id, spv::DecorationBinding);
		}
		if (compiler.has_decoration(id, spv::DecorationDescriptorSet)) {
			uniform.mSet = compiler.get_decoration(id, spv::DecorationDescriptorSet);
		}
	}
}

void AssignPushConstantRegisters(ShaderReflection& reflection) {
	std::vector<std::pair<size_t, size_t>> used;
	for (const auto& uniform : reflection.mUniforms) {
		if (uniform.mType == ShaderReflection::UniformType::UNIFORM_BUFFER) {
			used.push_back({uniform.mSet, uniform.mBind});
		}
	}
	for (auto& uniform : reflection.mUniforms) {
		if (uniform.mType != ShaderReflection::UniformType::PUSH_CONSTANT) {
			continue;
		}
		if (uniform.mSize < 4) {
			uniform.mSize = 4;
		}
		uniform.mSize = (uniform.mSize + 3) & ~size_t{3};
		while (RegisterUsed(used, uniform.mSet, uniform.mBind)) {
			++uniform.mBind;
		}
		used.push_back({uniform.mSet, uniform.mBind});
	}
}

std::string CompileSpirvStageToHlsl(ShaderType type, spv::ExecutionModel model, const std::vector<uint32_t>& spirv, const ShaderReflection& reflection) {
	spirv_cross::CompilerHLSL compiler(spirv);
	spirv_cross::CompilerHLSL::Options options;
	options.shader_model = kHlslShaderModel;
	options.flatten_matrix_vertex_input_semantics = true;
	options.force_storage_buffer_as_uav = true;
	compiler.set_hlsl_options(options);

	const auto resources = compiler.get_shader_resources();
	if (!resources.push_constant_buffers.empty()) {
		const auto id = resources.push_constant_buffers[0].id;
		const bool hadBinding = compiler.has_decoration(id, spv::DecorationBinding);
		const bool hadSet = compiler.has_decoration(id, spv::DecorationDescriptorSet);
		for (const auto& uniform : reflection.mUniforms) {
			if (uniform.mType != ShaderReflection::UniformType::PUSH_CONSTANT) {
				continue;
			}
			if (!HasStage(uniform.mShaderMask, type)) {
				continue;
			}
			spirv_cross::HLSLResourceBinding binding{};
			binding.stage = model;
			if (hadBinding) {
				binding.desc_set = hadSet ? compiler.get_decoration(id, spv::DecorationDescriptorSet) : 0u;
				binding.binding = compiler.get_decoration(id, spv::DecorationBinding);
			} else {
				binding.desc_set = spirv_cross::ResourceBindingPushConstantDescriptorSet;
				binding.binding = spirv_cross::ResourceBindingPushConstantBinding;
			}
			binding.cbv.register_space = static_cast<uint32_t>(uniform.mSet);
			binding.cbv.register_binding = static_cast<uint32_t>(uniform.mBind);
			compiler.add_hlsl_resource_binding(binding);
		}
	}

	return compiler.compile();
}

enum class RootKind {
	CBV,
	SRV,
	UAV,
	TABLE,
	CONSTANTS
};

struct RootKey {
	RootKind kind;
	uint32_t bind;
	uint32_t set;
	D3D12_SHADER_VISIBILITY visibility;
};

bool SameRoot(const RootKey& key, RootKind kind, uint32_t bind, uint32_t set, D3D12_SHADER_VISIBILITY visibility) {
	return key.kind == kind && key.bind == bind && key.set == set && key.visibility == visibility;
}

}

std::shared_ptr<ShaderDx12> ShaderDx12::CreateFromPath(std::map<ShaderType, std::string> path) {
	ShaderResource res;
	res.setPaths(path);
	return Create(res);
}

std::shared_ptr<ShaderDx12> ShaderDx12::Create(const ShaderResource& resource) {
	ShaderResource res = resource;
	auto& fs = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();

	for (const auto& [type, path] : res.paths) {
		if (path.empty()) {
			continue;
		}
		auto file = fs.getFile(path);
		if (file && file->isOpened()) {
			res.sources[ShaderResource::toEnum.at(type)] = file->readStr();
		} else {
			LOG_ERROR << "Failed to load shader: " << path;
		}
	}

	return std::make_shared<ShaderDx12>(res);
}

ShaderDx12::ShaderDx12() {
	static size_t ID = 0;
	mId = ID;
	++ID;
}

ShaderDx12::ShaderDx12(std::map<ShaderType, std::string> path) : ShaderDx12() {
	ShaderResource res;
	res.setPaths(std::move(path));
	create(res);
}

ShaderDx12::ShaderDx12(const ShaderResource& res) : ShaderDx12() {
	create(res);
}

void ShaderDx12::recompile(const ShaderResource& res) {
	mReflection = ShaderReflection();
	mBlobs.clear();
	mRootSignature.Reset();
	mPushConstantRootIndex = -1;
	mPushConstantDwords = 0;
	ShaderResource patched = res;
	if (!patched.sources.empty()) {
		patched.spirvSources.clear();
	}
	create(patched);
}

void ShaderDx12::create(const ShaderResource& res) {
	mPath = res.path;
	mShaderPaths = res.getPaths();
	mReflection = ShaderReflection();

	ShaderResource local = res;
	const std::vector<std::string> defines;

	if (!local.spirvSources.empty()) {
		for (auto& [type, spirv] : local.spirvSources) {
			GetReflection(mReflection, spirv, type);
		}
	} else {
		for (auto& [type, source] : local.sources) {
			bool isSpirv = false;
			if (source.size() >= 4) {
				const uint32_t magic = *reinterpret_cast<const uint32_t*>(source.data());
				if (magic == 0x07230203) {
					isSpirv = true;
				}
			}

			if (isSpirv) {
				std::vector<uint32_t> spirv(source.size() / 4);
				std::memcpy(spirv.data(), source.data(), source.size());
				local.spirvSources[type] = std::move(spirv);
				GetReflection(mReflection, local.spirvSources[type], type);
			} else {
				local.spirvSources[type] = CompileGlslToSpirv(type, source, defines);
				if (!local.spirvSources[type].empty()) {
					GetReflection(mReflection, local.spirvSources[type], type);
				}
			}
		}
	}

	for (const auto& stage : kStages) {
		const auto spirv = local.spirvSources.find(stage.type);
		if (spirv == local.spirvSources.end() || spirv->second.empty()) {
			continue;
		}
		ReadPushConstantBindings(spirv->second, mReflection);
	}
	AssignPushConstantRegisters(mReflection);

	for (const auto& stage : kStages) {
		const auto spirv = local.spirvSources.find(stage.type);
		if (spirv == local.spirvSources.end() || spirv->second.empty()) {
			continue;
		}
		const auto hlsl = CompileSpirvStageToHlsl(stage.type, stage.model, spirv->second, mReflection);
		mBlobs[stage.type] = d3dUtil::CompileShaderFromHlsl(hlsl, L"main", stage.profile);
	}

	buildRootSignature();
}

void ShaderDx12::buildRootSignature() {
	std::vector<CD3DX12_DESCRIPTOR_RANGE> texTables;
	std::vector<CD3DX12_DESCRIPTOR_RANGE> uavTables;
	texTables.reserve(mReflection.mUniforms.size());
	uavTables.reserve(mReflection.mUniforms.size());

	std::vector<CD3DX12_ROOT_PARAMETER> slotRootParameters;
	slotRootParameters.reserve(mReflection.mUniforms.size());
	std::vector<RootKey> rootKeys;
	rootKeys.reserve(mReflection.mUniforms.size());

	auto findRoot = [&](RootKind kind, uint32_t bind, uint32_t set, D3D12_SHADER_VISIBILITY visibility) -> int {
		for (size_t i = 0; i < rootKeys.size(); ++i) {
			if (SameRoot(rootKeys[i], kind, bind, set, visibility)) {
				return static_cast<int>(i);
			}
		}
		return -1;
	};

	for (auto& uniform : mReflection.mUniforms) {
		const auto visibility = VisibilityForMask(uniform.mShaderMask);
		const auto bind = static_cast<UINT>(uniform.mBind);
		const auto set = static_cast<UINT>(uniform.mSet);

		if (uniform.mType == ShaderReflection::UniformType::UNIFORM_BUFFER) {
			const int existing = findRoot(RootKind::CBV, bind, set, visibility);
			if (existing >= 0) {
				uniform.mRootId = static_cast<size_t>(existing);
				continue;
			}
			CD3DX12_ROOT_PARAMETER elem;
			elem.InitAsConstantBufferView(bind, set, visibility);
			uniform.mRootId = slotRootParameters.size();
			rootKeys.push_back({RootKind::CBV, bind, set, visibility});
			slotRootParameters.push_back(elem);
		} else if (uniform.mType == ShaderReflection::UniformType::STORAGE_BUFFER) {
			const int existing = findRoot(RootKind::UAV, bind, set, visibility);
			if (existing >= 0) {
				uniform.mRootId = static_cast<size_t>(existing);
				continue;
			}
			CD3DX12_DESCRIPTOR_RANGE uavTable;
			uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, bind, set);
			uavTables.push_back(uavTable);
			CD3DX12_ROOT_PARAMETER elem;
			elem.InitAsDescriptorTable(1, &uavTables.back(), visibility);
			uniform.mRootId = slotRootParameters.size();
			rootKeys.push_back({RootKind::UAV, bind, set, visibility});
			slotRootParameters.push_back(elem);
		} else if (IsTexture(uniform.mType)) {
			const int existing = findRoot(RootKind::TABLE, bind, set, visibility);
			if (existing >= 0) {
				uniform.mRootId = static_cast<size_t>(existing);
				continue;
			}
			CD3DX12_DESCRIPTOR_RANGE texTable;
			texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, bind, set);
			texTables.push_back(texTable);
			CD3DX12_ROOT_PARAMETER elem;
			elem.InitAsDescriptorTable(1, &texTables.back(), visibility);
			uniform.mRootId = slotRootParameters.size();
			rootKeys.push_back({RootKind::TABLE, bind, set, visibility});
			slotRootParameters.push_back(elem);
		} else if (uniform.mType == ShaderReflection::UniformType::PUSH_CONSTANT) {
			const UINT num32 = static_cast<UINT>(uniform.mSize / 4);
			if (num32 == 0) {
				continue;
			}
			const int existing = findRoot(RootKind::CONSTANTS, bind, set, visibility);
			if (existing >= 0) {
				uniform.mRootId = static_cast<size_t>(existing);
				mPushConstantRootIndex = existing;
				mPushConstantDwords = num32;
				continue;
			}
			CD3DX12_ROOT_PARAMETER elem;
			elem.InitAsConstants(num32, bind, set, visibility);
			uniform.mRootId = slotRootParameters.size();
			mPushConstantRootIndex = static_cast<int>(uniform.mRootId);
			mPushConstantDwords = num32;
			rootKeys.push_back({RootKind::CONSTANTS, bind, set, visibility});
			slotRootParameters.push_back(elem);
		}
	}

	auto staticSamplers = DriverDx12::GetStaticSamplers();

	size_t stageMask = 0;
	for (const auto& [type, blob] : mBlobs) {
		if (blob) {
			stageMask |= static_cast<size_t>(type);
		}
	}
	for (const auto& uniform : mReflection.mUniforms) {
		stageMask |= uniform.mShaderMask;
	}

	const bool hasVs = HasStage(stageMask, ShaderType::VERTEX);
	const bool hasPs = HasStage(stageMask, ShaderType::FRAGMENT);
	const bool hasGs = HasStage(stageMask, ShaderType::GEOMETRY);
	const bool hasHs = HasStage(stageMask, ShaderType::TESSELLATION_CONTROL);
	const bool hasDs = HasStage(stageMask, ShaderType::TESSELLATION_EVALUATION);
	const bool graphics = hasVs || hasPs || hasGs || hasHs || hasDs;

	D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	if (graphics) {
		flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		if (!hasVs) flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
		if (!hasPs) flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
		if (!hasGs) flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
		if (!hasHs) flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;
		if (!hasDs) flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;
	}

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		static_cast<UINT>(slotRootParameters.size()),
		slotRootParameters.empty() ? nullptr : slotRootParameters.data(),
		static_cast<UINT>(staticSamplers.size()),
		staticSamplers.data(),
		flags);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (FAILED(hr)) {
		std::string message = "Failed to serialize DX12 root signature";
		if (errorBlob && errorBlob->GetBufferPointer() && errorBlob->GetBufferSize() > 0) {
			message += ": ";
			message.append(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
		}
		LOG_ERROR << message;
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(d3dUtil::GetDriver()->getDevice()->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

#endif
