#include "driverDx12.h"
#ifdef DX12_BACKEND
#include "indexBufferDx12.h"
#include "spirv_hlsl.hpp"
#include "storageBufferDx12.h"
#include "textureDx12.h"
#include "uniformBufferDx12.h"
#include "vertexBufferDx12.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <iostream>
#include <set>


using namespace IKIGAI::RENDER;
using Microsoft::WRL::ComPtr;
namespace {
uint64_t aligned(uint64_t value, uint64_t alignment) {
	return (value + alignment - 1) / alignment * alignment;
}
void check(HRESULT hr, const char* operation) {
	if (FAILED(hr))
		throw std::runtime_error(std::string(operation) + " HRESULT=" + std::to_string(uint32_t(hr)));
}
void uav(ID3D12GraphicsCommandList* list) {
	auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
	list->ResourceBarrier(1, &barrier);
}
void uav(DriverDx12& driver) {
	uav(driver.getCommandList().Get());
}
Dx12Resource buffer(uint64_t size, D3D12_RESOURCE_STATES state, bool unordered = false) {
	const auto initial = state == D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE ? state : D3D12_RESOURCE_STATE_COMMON;
	auto result = d3dUtil::CreateResource(CD3DX12_RESOURCE_DESC::Buffer(std::max<uint64_t>(size, 256),
											  unordered ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE),
		D3D12_HEAP_TYPE_DEFAULT, initial);
	if (state != initial) {
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(result.Get(), initial, state);
		d3dUtil::GetDriver()->getCommandList()->ResourceBarrier(1, &barrier);
	}
	return result;
}
Dx12Resource upload(const void* data, size_t size) {
	auto result = d3dUtil::CreateBuffer(std::max<size_t>(size, 256), D3D12_HEAP_TYPE_UPLOAD);
	void* mapped = nullptr;
	check(result->Map(0, nullptr, &mapped), "Map RT upload");
	if (data && size)
		std::memcpy(mapped, data, size);
	result->Unmap(0, nullptr);
	return result;
}
struct ASData {
	Dx12Resource Storage, Scratch, Instances;
	std::vector<Dx12Resource> Inputs;
	std::vector<std::shared_ptr<void>> Owners;
	std::vector<RayTracingGeometry> Source;
	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> Geometries;
	bool Top = false, Update = false;
	uint32_t InstanceCount = 0;
};
class ASDx final : public AccelerationStructure {
public:
	DriverDx12* Driver;
	std::shared_ptr<ASData> Data = std::make_shared<ASData>();
	explicit ASDx(DriverDx12& driver)
		: Driver(&driver) { }
	~ASDx() override {
		if (Data && !Driver->mShuttingDown)
			Driver->mRayDeferred.push_back(std::move(Data));
	}
};
struct Binding {
	D3D_SHADER_INPUT_TYPE Type { };
	D3D_SRV_DIMENSION Dimension { };
	UINT Space = 0, Register = 0, Count = 1, Root = 0, HeapOffset = 0;
	std::set<std::string> Names;
	bool Sampler = false, Uav = false, Raw = false;
};
char registerClass(const Binding& binding) {
	return binding.Sampler ? 's' : binding.Type == D3D_SIT_CBUFFER ? 'b' : binding.Uav ? 'u' : 't';
}
struct ProgramData {
	ComPtr<ID3D12RootSignature> Root, LocalRoot;
	ComPtr<ID3D12StateObject> State;
	ComPtr<ID3D12PipelineState> ComputeState;
	std::vector<ComPtr<IDxcBlob>> Modules;
	std::vector<Binding> Bindings;
	Dx12Resource SBT;
	D3D12_DISPATCH_RAYS_DESC Rays { };
	uint32_t RaygenCount = 0, RaygenStride = 0, PushSize = 0, Descriptors = 0, Samplers = 0, RecordSize = 0, RequiredPushSize = 0,
			 RequiredRecordSize = 0;
	int PushRoot = -1;
	bool Compute = false, Query = false;
	std::array<uint32_t, 3> LocalSize { 1, 1, 1 };
};
class ProgramDx final : public RayTracingProgram {
public:
	DriverDx12* Driver;
	std::shared_ptr<ProgramData> Data = std::make_shared<ProgramData>();
	explicit ProgramDx(DriverDx12& driver)
		: Driver(&driver) { }
	bool isRayQuery() const override {
		return Data->Query;
	}
	bool isCompute() const override {
		return Data->Compute;
	}
	~ProgramDx() override {
		if (Data && !Driver->mShuttingDown)
			Driver->mRayDeferred.push_back(std::move(Data));
	}
};
struct DispatchData {
	RayTracingDispatch Snapshot;
	ComPtr<ID3D12DescriptorHeap> Heap, SamplerHeap;
};
D3D12_SAMPLER_DESC sampler(const TextureInterface& texture) {
	auto wrap = [](WrapFilter value) {
		switch (value) {
		case WrapFilter::REPEAT:
			return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		case WrapFilter::MIRRORED_REPEAT:
			return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		case WrapFilter::MIRROR_CLAMP_TO_EDGE:
			return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
		case WrapFilter::CLAMP_TO_BORDER:
			return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		default:
			return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		}
	};
	D3D12_SAMPLER_DESC desc { };
	auto min = texture.getMinFilter() == MinMagFilter::NEAREST ? D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;
	auto mag = texture.getMagFilter() == MinMagFilter::NEAREST ? D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;
	desc.Filter = D3D12_ENCODE_BASIC_FILTER(min, mag, min, D3D12_FILTER_REDUCTION_TYPE_STANDARD);
	desc.AddressU = wrap(texture.getWrapS());
	desc.AddressV = wrap(texture.getWrapT());
	desc.AddressW = wrap(texture.getWrapR());
	desc.MaxAnisotropy = 1;
	desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	desc.MaxLOD = D3D12_FLOAT32_MAX;
	return desc;
}
struct Compiled {
	ComPtr<IDxcBlob> Binary, Reflection;
	std::map<std::pair<UINT, UINT>, std::set<std::string>> Aliases;
};
Compiled compile(const RayTracingShader& shader, bool compute) {
	std::string source = shader.Source;
	Compiled compiled;
	if (shader.Language == "GLSL") {
		const auto spv = CompileGlslToSpirv(shader.Stage, source, { });
		spirv_cross::CompilerHLSL cross(spv);
		spirv_cross::CompilerHLSL::Options options;
		options.shader_model = 66;
		cross.set_hlsl_options(options);
		const auto resources = cross.get_shader_resources();
		for (const auto* list : { &resources.uniform_buffers, &resources.storage_buffers, &resources.sampled_images, &resources.storage_images,
			 &resources.acceleration_structures })
			for (const auto& r : *list) {
				auto& names
					= compiled
						  .Aliases[{ cross.get_decoration(r.id, spv::DecorationDescriptorSet), cross.get_decoration(r.id, spv::DecorationBinding) }];
				names.insert(r.name);
				names.insert(cross.get_name(r.id));
			}
		spirv_cross::HLSLResourceBinding push { };
		push.stage = cross.get_execution_model();
		push.desc_set = spirv_cross::ResourceBindingPushConstantDescriptorSet;
		push.binding = spirv_cross::ResourceBindingPushConstantBinding;
		push.cbv.register_space = 30;
		push.cbv.register_binding = 0;
		cross.add_hlsl_resource_binding(push);
		source = cross.compile();
		std::cout << "SPIRV_CROSS_HLSL shader=" << shader.Name << " stage=" << int(shader.Stage) << "\n";
	}
	else if (shader.Language != "HLSL")
		throw std::runtime_error("Unknown DXR shader language");
	ComPtr<IDxcUtils> utils;
	ComPtr<IDxcCompiler3> compiler;
	check(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)), "DXC utils");
	check(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)), "DXC compiler");
	std::wstring entry(shader.EntryPoint.begin(), shader.EntryPoint.end());
	std::vector<LPCWSTR> args { L"-T", compute ? L"cs_6_6" : L"lib_6_6", L"-HV", L"2021", L"-Zpr", L"-Od" };
	if (compute) {
		args.push_back(L"-E");
		args.push_back(entry.c_str());
	}
	DxcBuffer input { source.data(), source.size(), DXC_CP_UTF8 };
	ComPtr<IDxcResult> result;
	check(compiler->Compile(&input, args.data(), UINT(args.size()), nullptr, IID_PPV_ARGS(&result)), "DXC Compile RT");
	HRESULT status;
	check(result->GetStatus(&status), "DXC GetStatus");
	if (FAILED(status)) {
		ComPtr<IDxcBlobUtf8> errors;
		result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
		throw std::runtime_error("DXR shader " + shader.Name + ": " + (errors ? std::string(errors->GetStringPointer()) : "compile failed"));
	}
	check(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&compiled.Binary), nullptr), "DXC object");
	check(result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&compiled.Reflection), nullptr), "DXC reflection");
	return compiled;
}
void binding(ProgramData& data, const D3D12_SHADER_INPUT_BIND_DESC& input) {
	if (input.Type == D3D_SIT_CBUFFER && input.Space >= 30) {
		if (input.BindPoint != 0 || input.Space > 31)
			throw std::runtime_error("DXR reserved constants require b0 space30 (push) or space31 (record)");
		return;
	}
	if (input.Type == D3D_SIT_SAMPLER && input.Space == 29)
		return;
	Binding b;
	b.Type = input.Type;
	b.Dimension = input.Dimension;
	b.Space = input.Space;
	b.Register = input.BindPoint;
	b.Count = input.BindCount;
	if (!b.Count || b.Count == UINT_MAX)
		throw std::runtime_error("DXR requires bounded descriptor arrays");
	b.Sampler = input.Type == D3D_SIT_SAMPLER;
	b.Uav = input.Type == D3D_SIT_UAV_RWTYPED || input.Type == D3D_SIT_UAV_RWSTRUCTURED || input.Type == D3D_SIT_UAV_RWBYTEADDRESS;
	b.Raw = input.Type == D3D_SIT_BYTEADDRESS || input.Type == D3D_SIT_UAV_RWBYTEADDRESS;
	std::string name = input.Name;
	b.Names.insert(name);
	if (b.Sampler) {
		for (const std::string suffix : { "_sampler", "Sampler" })
			if (name.ends_with(suffix))
				b.Names.insert(name.substr(0, name.size() - suffix.size()));
	}
	auto found = std::find_if(data.Bindings.begin(), data.Bindings.end(),
		[&](const Binding& other) { return b.Space == other.Space && b.Register == other.Register && registerClass(b) == registerClass(other); });
	if (found != data.Bindings.end()) {
		if (found->Type != b.Type || found->Count != b.Count || found->Dimension != b.Dimension)
			throw std::runtime_error("DXR descriptor collision");
		found->Names.insert(b.Names.begin(), b.Names.end());
	}
	else
		data.Bindings.push_back(std::move(b));
}
void build(DriverDx12& driver, const std::shared_ptr<ASDx>& as, bool update) {
	auto& data = *as->Data;
	if (update && !data.Update)
		throw std::runtime_error("DXR AS was built without allowUpdate");
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs { };
	inputs.Type = data.Top ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	if (data.Update)
		inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	if (data.Top) {
		inputs.NumDescs = data.InstanceCount;
		inputs.InstanceDescs = data.Instances->GetGPUVirtualAddress();
	}
	else {
		inputs.NumDescs = UINT(data.Geometries.size());
		inputs.pGeometryDescs = data.Geometries.data();
	}
	if (!update) {
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO size { };
		driver.mRayDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &size);
		if (!size.ResultDataMaxSizeInBytes)
			throw std::runtime_error("Invalid DXR AS prebuild size");
		data.Storage = buffer(size.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, true);
		data.Scratch = buffer(std::max(size.ScratchDataSizeInBytes, size.UpdateScratchDataSizeInBytes), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
	}
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc { };
	desc.Inputs = inputs;
	if (update) {
		desc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
		desc.SourceAccelerationStructureData = data.Storage->GetGPUVirtualAddress();
	}
	desc.DestAccelerationStructureData = data.Storage->GetGPUVirtualAddress();
	desc.ScratchAccelerationStructureData = data.Scratch->GetGPUVirtualAddress();
	uav(driver);
	driver.getCommandList()->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
	uav(driver);
	driver.mRayFrameResources.push_back(as);
}
std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances(
	DriverDx12& driver, const std::vector<RayTracingInstance>& input, std::vector<std::shared_ptr<void>>& owners) {
	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> result;
	for (const auto& source : input) {
		auto as = std::dynamic_pointer_cast<ASDx>(source.BottomLevel);
		if (!as || as->Driver != &driver || as->Data->Top)
			throw std::runtime_error("DXR TLAS requires this driver's BLAS");
		if (source.CustomIndex > 0xffffff || source.HitGroupOffset > 0xffffff)
			throw std::runtime_error("DXR instance index overflow");
		D3D12_RAYTRACING_INSTANCE_DESC instance { };
		auto transform = source.Transform;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 4; ++c) {
				if (!std::isfinite(transform(r, c)))
					throw std::runtime_error("DXR instance transform is not finite");
				instance.Transform[r][c] = transform(r, c);
			}
		instance.InstanceID = source.CustomIndex;
		instance.InstanceMask = source.Mask;
		instance.InstanceContributionToHitGroupIndex = source.HitGroupOffset;
		instance.Flags = source.DoubleSided ? D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE : D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		instance.AccelerationStructure = as->Data->Storage->GetGPUVirtualAddress();
		result.push_back(instance);
		owners.push_back(as);
	}
	return result;
}
}

void DriverDx12::configureRayTracing() {
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options { };
	D3D12_FEATURE_DATA_D3D12_OPTIONS core { };
	if (!std::getenv("IKIGAI_DISABLE_RT") && SUCCEEDED(mDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)))
		&& options.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED && SUCCEEDED(mDevice.As(&mRayDevice))) {
		mRayTracingCapabilities.AccelerationStructure = mRayTracingCapabilities.Pipeline = true;
		mRayTracingCapabilities.RayQuery = options.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1 && !std::getenv("IKIGAI_DISABLE_RAY_QUERY");
		mDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &core, sizeof(core));
		mRayTracingCapabilities.DescriptorIndexing = core.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2;
		mRayTracingCapabilities.MaxRecursionDepth = D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH;
		mRayTracingCapabilities.MaxDispatchInvocations = 1ull << 30;
	}
	std::cout << "RAY_TRACING_CAPABILITIES AS=" << mRayTracingCapabilities.AccelerationStructure << " query=" << mRayTracingCapabilities.RayQuery
			  << " pipeline=" << mRayTracingCapabilities.Pipeline << " backend=dx12\n";
}
std::shared_ptr<VertexBufferInterface> DriverDx12::createRayTracingVertexBuffer(const void* source, size_t count, size_t stride) {
	return std::make_shared<VertexBufferDx12>(const_cast<void*>(source), count, stride);
}
std::shared_ptr<AccelerationStructure> DriverDx12::createBottomLevelAS(const std::vector<RayTracingGeometry>& source, bool allowUpdate) {
	if (!mRayTracingCapabilities.AccelerationStructure)
		throw UnsupportedRayTracing("DXR acceleration structures are unavailable");
	if (source.empty())
		throw std::runtime_error("DXR BLAS requires geometry");
	auto output = std::make_shared<ASDx>(*this);
	auto& data = *output->Data;
	data.Update = allowUpdate;
	data.Source = source;
	for (const auto& input : source) {
		D3D12_RAYTRACING_GEOMETRY_DESC geometry { };
		geometry.Flags = input.Opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
		if (!input.Aabbs.empty()) {
			for (const auto& box : input.Aabbs)
				for (int a = 0; a < 3; ++a)
					if (!std::isfinite(box[a]) || !std::isfinite(box[a + 3]) || box[a] > box[a + 3])
						throw std::runtime_error("Invalid DXR AABB");
			data.Inputs.push_back(upload(input.Aabbs.data(), input.Aabbs.size() * sizeof(input.Aabbs[0])));
			geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
			geometry.AABBs.AABBCount = input.Aabbs.size();
			geometry.AABBs.AABBs = { data.Inputs.back()->GetGPUVirtualAddress(), sizeof(input.Aabbs[0]) };
		}
		else {
			auto vertices = std::dynamic_pointer_cast<VertexBufferDx12>(input.Vertices);
			auto indices = std::dynamic_pointer_cast<IndexBufferDx12>(input.Indices);
			if (input.Indices && !indices)
				throw std::runtime_error("Invalid DXR index buffer backend");
			if (!vertices || !input.VertexCount || input.VertexStride < 12 || input.VertexOffset % 4 || input.VertexStride % 4
				|| input.VertexOffset > vertices->getSizeByte()
				|| uint64_t(input.VertexCount - 1) * input.VertexStride + 12 > vertices->getSizeByte() - input.VertexOffset)
				throw std::runtime_error("Invalid DXR vertex input");
			uint32_t count = indices ? input.IndexCount : input.VertexCount;
			if (!count || count % 3 || (indices && (input.IndexStride != 2 && input.IndexStride != 4)))
				throw std::runtime_error("Invalid DXR triangle count");
			if (indices
				&& (input.IndexOffset % input.IndexStride || input.IndexOffset > indices->getSizeByte()
					|| uint64_t(count) * input.IndexStride > indices->getSizeByte() - input.IndexOffset))
				throw std::runtime_error("Invalid DXR index range");
			vertices->transition(mCommandList.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			auto& triangle = geometry.Triangles;
			triangle.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
			triangle.VertexCount = input.VertexCount;
			triangle.VertexBuffer = { vertices->getBuffer()->GetGPUVirtualAddress() + input.VertexOffset, input.VertexStride };
			triangle.IndexFormat = indices ? (input.IndexStride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT) : DXGI_FORMAT_UNKNOWN;
			if (indices) {
				indices->transition(mCommandList.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				triangle.IndexCount = count;
				triangle.IndexBuffer = indices->getBuffer()->GetGPUVirtualAddress() + input.IndexOffset;
				data.Owners.push_back(indices);
			}
			data.Owners.push_back(vertices);
		}
		data.Geometries.push_back(geometry);
	}
	build(*this, output, false);
	return output;
}
std::shared_ptr<AccelerationStructure> DriverDx12::createTopLevelAS(const std::vector<RayTracingInstance>& source, bool allowUpdate) {
	if (!mRayTracingCapabilities.AccelerationStructure)
		throw UnsupportedRayTracing("DXR acceleration structures are unavailable");
	auto output = std::make_shared<ASDx>(*this);
	auto& data = *output->Data;
	data.Top = true;
	data.Update = allowUpdate;
	data.InstanceCount = UINT(source.size());
	const auto packed = instances(*this, source, data.Owners);
	data.Instances = upload(packed.data(), packed.size() * sizeof(packed[0]));
	build(*this, output, false);
	return output;
}
void DriverDx12::refitBottomLevelAS(const std::shared_ptr<AccelerationStructure>& structure) {
	auto output = std::dynamic_pointer_cast<ASDx>(structure);
	if (!output || output->Driver != this || output->Data->Top)
		throw std::runtime_error("DXR refit requires this driver's BLAS");
	auto& data = *output->Data;
	for (size_t i = 0; i < data.Source.size(); ++i) {
		const auto& source = data.Source[i];
		if (!source.Aabbs.empty())
			continue;
		auto vertices = std::dynamic_pointer_cast<VertexBufferDx12>(source.Vertices);
		auto indices = std::dynamic_pointer_cast<IndexBufferDx12>(source.Indices);
		if (vertices->getBuffer()->GetGPUVirtualAddress() + source.VertexOffset != data.Geometries[i].Triangles.VertexBuffer.StartAddress
			|| (indices && indices->getBuffer()->GetGPUVirtualAddress() + source.IndexOffset != data.Geometries[i].Triangles.IndexBuffer))
			throw std::runtime_error("DXR input allocation changed; rebuild BLAS");
		vertices->transition(mCommandList.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		if (indices)
			indices->transition(mCommandList.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
	build(*this, output, true);
}
void DriverDx12::refitTopLevelAS(const std::shared_ptr<AccelerationStructure>& structure, const std::vector<RayTracingInstance>& source) {
	auto output = std::dynamic_pointer_cast<ASDx>(structure);
	if (!output || output->Driver != this || !output->Data->Top || !output->Data->Update || output->Data->InstanceCount != source.size())
		throw std::runtime_error("DXR TLAS refit requires updateable AS and unchanged count");
	auto& data = *output->Data;
	std::vector<std::shared_ptr<void>> owners;
	auto packed = instances(*this, source, owners);
	destroyDeferred(std::move(data.Instances));
	mRayFrameResources.insert(mRayFrameResources.end(), data.Owners.begin(), data.Owners.end());
	data.Owners = std::move(owners);
	data.Instances = upload(packed.data(), packed.size() * sizeof(packed[0]));
	build(*this, output, true);
}
std::shared_ptr<RayTracingProgram> DriverDx12::createRayTracingProgram(const RayTracingProgramResource& resource) {
	return createRayProgram(resource, true);
}
std::shared_ptr<RayTracingProgram> DriverDx12::createComputeProgram(const RayTracingProgramResource& resource) {
	if (resource.Shaders.size() != 1 || resource.Shaders[0].Stage != ShaderType::COMPUTE || !resource.Groups.empty())
		throw std::runtime_error("Compute program requires one compute shader and no groups");
	return createRayProgram(resource, false);
}
std::shared_ptr<RayTracingProgram> DriverDx12::createRayProgram(const RayTracingProgramResource& resource, bool query) {
	const bool compute = resource.Shaders.size() == 1 && resource.Shaders[0].Stage == ShaderType::COMPUTE;
	if (compute && query && !mRayTracingCapabilities.RayQuery)
		throw UnsupportedRayTracing("DXR inline ray queries are unavailable");
	if (!compute && !mRayTracingCapabilities.Pipeline)
		throw UnsupportedRayTracing("DXR pipelines are unavailable");
	if (resource.RequiresDescriptorIndexing && !mRayTracingCapabilities.DescriptorIndexing)
		throw UnsupportedRayTracing("DXR descriptor indexing is unavailable");
	if (resource.PushConstantSize % 4 || resource.PushConstantSize > 128)
		throw std::runtime_error("DXR push constants must be DWORD aligned and at most 128 bytes");
	if (!compute
		&& (!resource.MaxRecursionDepth || resource.MaxRecursionDepth > mRayTracingCapabilities.MaxRecursionDepth || resource.MaxAttributeSize > 32
			|| resource.MaxAttributeSize % 4 || resource.MaxPayloadSize % 4))
		throw std::runtime_error("Invalid DXR shader limits");
	nlohmann::json identity = { query, resource.MaxRecursionDepth, resource.PushConstantSize, resource.MaxPayloadSize, resource.MaxAttributeSize };
	for (const auto& shader : resource.Shaders)
		identity.push_back({ shader.Name, shader.Source, shader.Language, shader.EntryPoint, int(shader.Stage) });
	for (const auto& group : resource.Groups)
		identity.push_back({ group.Name, group.General, group.ClosestHit, group.AnyHit, group.Intersection, group.RecordData });
	const auto key = identity.dump();
	for (auto i = mRayPrograms.begin(); i != mRayPrograms.end();)
		if (i->second.expired())
			i = mRayPrograms.erase(i);
		else
			++i;
	if (auto found = mRayPrograms.find(key); found != mRayPrograms.end())
		if (auto cached = found->second.lock())
			return cached;
	auto output = std::make_shared<ProgramDx>(*this);
	auto& data = *output->Data;
	data.Compute = compute;
	data.Query = compute && query;
	data.PushSize = resource.PushConstantSize;
	ComPtr<IDxcUtils> utils;
	check(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)), "DXC reflection utils");
	auto reflectBinding = [&](auto* reflected, const D3D12_SHADER_INPUT_BIND_DESC& input) {
		binding(data, input);
		if (input.Type == D3D_SIT_CBUFFER && (input.Space == 30 || input.Space == 31)) {
			D3D12_SHADER_BUFFER_DESC constants { };
			check(reflected->GetConstantBufferByName(input.Name)->GetDesc(&constants), "DXR constant layout");
			auto& required = input.Space == 30 ? data.RequiredPushSize : data.RequiredRecordSize;
			required = std::max(required, constants.Size);
		}
	};
	std::map<std::string, ShaderType> stages;
	for (const auto& shader : resource.Shaders) {
		if (!stages.emplace(shader.Name, shader.Stage).second)
			throw std::runtime_error("Duplicate DXR shader name");
		if (!compute && shader.Stage == ShaderType::COMPUTE)
			throw std::runtime_error("Cannot mix compute and DXR shader stages");
		auto compiled = compile(shader, compute);
		data.Modules.push_back(compiled.Binary);
		DxcBuffer reflection { compiled.Reflection->GetBufferPointer(), compiled.Reflection->GetBufferSize(), 0 };
		if (compute) {
			ComPtr<ID3D12ShaderReflection> reflected;
			check(utils->CreateReflection(&reflection, IID_PPV_ARGS(&reflected)), "DXC compute reflection");
			D3D12_SHADER_DESC desc { };
			reflected->GetDesc(&desc);
			reflected->GetThreadGroupSize(&data.LocalSize[0], &data.LocalSize[1], &data.LocalSize[2]);
			for (UINT i = 0; i < desc.BoundResources; ++i) {
				D3D12_SHADER_INPUT_BIND_DESC input { };
				reflected->GetResourceBindingDesc(i, &input);
				reflectBinding(reflected.Get(), input);
			}
		}
		else {
			ComPtr<ID3D12LibraryReflection> reflected;
			check(utils->CreateReflection(&reflection, IID_PPV_ARGS(&reflected)), "DXC library reflection");
			D3D12_LIBRARY_DESC desc { };
			reflected->GetDesc(&desc);
			for (UINT i = 0; i < desc.FunctionCount; ++i) {
				auto* function = reflected->GetFunctionByIndex(i);
				D3D12_FUNCTION_DESC fn { };
				function->GetDesc(&fn);
				for (UINT j = 0; j < fn.BoundResources; ++j) {
					D3D12_SHADER_INPUT_BIND_DESC input { };
					function->GetResourceBindingDesc(j, &input);
					reflectBinding(function, input);
				}
			}
		}
		for (auto& b : data.Bindings)
			if (auto found = compiled.Aliases.find({ b.Space, b.Register }); found != compiled.Aliases.end())
				b.Names.insert(found->second.begin(), found->second.end());
	}
	if (data.PushSize < data.RequiredPushSize)
		throw std::runtime_error("DXR PushConstantSize is smaller than the shader layout");
	if (compute && data.RequiredRecordSize)
		throw std::runtime_error("DXR shader records are unavailable in compute programs");
	std::vector<D3D12_DESCRIPTOR_RANGE> ranges(data.Bindings.size());
	std::vector<D3D12_ROOT_PARAMETER> roots(data.Bindings.size());
	for (size_t i = 0; i < data.Bindings.size(); ++i) {
		auto& b = data.Bindings[i];
		b.Root = UINT(i);
		b.HeapOffset = b.Sampler ? data.Samplers : data.Descriptors;
		(b.Sampler ? data.Samplers : data.Descriptors) += b.Count;
		ranges[i] = { b.Sampler				? D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
				: b.Type == D3D_SIT_CBUFFER ? D3D12_DESCRIPTOR_RANGE_TYPE_CBV
				: b.Uav						? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
											: D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			b.Count, b.Register, b.Space, 0 };
		roots[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		roots[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		roots[i].DescriptorTable = { 1, &ranges[i] };
	}
	if (data.PushSize) {
		data.PushRoot = int(roots.size());
		D3D12_ROOT_PARAMETER push { };
		push.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		push.Constants = { 0, 30, data.PushSize / 4 };
		roots.push_back(push);
	}
	std::array<D3D12_STATIC_SAMPLER_DESC, 3> samplers { };
	for (UINT i = 0; i < 3; ++i) {
		samplers[i].Filter = i == 2 ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW
			= i == 1 ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
		samplers[i].MaxAnisotropy = 1;
		samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplers[i].ShaderRegister = i;
		samplers[i].RegisterSpace = 29;
	}
	auto makeRoot = [&](const D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>& root) {
		ComPtr<ID3DBlob> binary, errors;
		const auto hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &binary, &errors);
		if (FAILED(hr))
			throw std::runtime_error(errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())
											: "DXR root signature serialization failed");
		check(mDevice->CreateRootSignature(0, binary->GetBufferPointer(), binary->GetBufferSize(), IID_PPV_ARGS(&root)), "DXR root signature");
	};
	D3D12_ROOT_SIGNATURE_DESC root { UINT(roots.size()), roots.data(), UINT(samplers.size()), samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_NONE };
	makeRoot(root, data.Root);
	if (compute) {
		D3D12_COMPUTE_PIPELINE_STATE_DESC desc { };
		desc.pRootSignature = data.Root.Get();
		desc.CS = { data.Modules[0]->GetBufferPointer(), data.Modules[0]->GetBufferSize() };
		check(mDevice->CreateComputePipelineState(&desc, IID_PPV_ARGS(&data.ComputeState)), "DXR compute pipeline");
		TrackGpuObjectDx12(data.ComputeState.Get(), mDevice.Get(), PROFILER::GpuResourceKind::Pipeline, "Ray-query/compute pipeline");
		mRayPrograms[key] = output;
		return output;
	}
	for (const auto& group : resource.Groups)
		data.RecordSize = std::max(data.RecordSize, UINT(group.RecordData.size()));
	data.RecordSize = UINT(aligned(data.RecordSize, 4));
	if (data.RecordSize < data.RequiredRecordSize)
		throw std::runtime_error("DXR RecordData is smaller than the shader layout");
	if (data.RecordSize) {
		D3D12_ROOT_PARAMETER record { };
		record.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		record.Constants = { 0, 31, data.RecordSize / 4 };
		D3D12_ROOT_SIGNATURE_DESC local { 1, &record, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE };
		makeRoot(local, data.LocalRoot);
	}
	CD3DX12_STATE_OBJECT_DESC state(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
	std::map<std::string, std::wstring> names;
	for (const auto& shader : resource.Shaders)
		names[shader.Name] = std::wstring(shader.Name.begin(), shader.Name.end());
	for (const auto& group : resource.Groups)
		names[group.Name] = std::wstring(group.Name.begin(), group.Name.end());
	std::vector<std::wstring> entries;
	entries.reserve(resource.Shaders.size());
	for (size_t i = 0; i < resource.Shaders.size(); ++i) {
		entries.emplace_back(resource.Shaders[i].EntryPoint.begin(), resource.Shaders[i].EntryPoint.end());
		auto* lib = state.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
		D3D12_SHADER_BYTECODE code { data.Modules[i]->GetBufferPointer(), data.Modules[i]->GetBufferSize() };
		lib->SetDXILLibrary(&code);
		lib->DefineExport(names.at(resource.Shaders[i].Name).c_str(), entries.back().c_str());
	}
	std::array<std::vector<size_t>, 4> sections;
	std::vector<std::wstring> identifiers(resource.Groups.size());
	for (size_t i = 0; i < resource.Groups.size(); ++i) {
		const auto& group = resource.Groups[i];
		if (!group.General.empty()) {
			const auto type = stages.at(group.General);
			const auto section = type == ShaderType::RAYGEN ? 0 : type == ShaderType::MISS ? 1 : type == ShaderType::CALLABLE ? 3 : -1;
			if (section < 0 || !group.ClosestHit.empty() || !group.AnyHit.empty() || !group.Intersection.empty())
				throw std::runtime_error("Invalid DXR general group");
			sections[section].push_back(i);
			identifiers[i] = names.at(group.General);
		}
		else {
			if (group.ClosestHit.empty() && group.AnyHit.empty() && group.Intersection.empty())
				throw std::runtime_error("Empty DXR hit group");
			identifiers[i] = L"__hit_" + names.at(group.Name);
			auto* hit = state.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
			hit->SetHitGroupExport(identifiers[i].c_str());
			hit->SetHitGroupType(group.Intersection.empty() ? D3D12_HIT_GROUP_TYPE_TRIANGLES : D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE);
			auto set = [&](const std::string& name, ShaderType type) {
				if (!name.empty() && stages.at(name) != type)
					throw std::runtime_error("DXR hit-group stage mismatch");
			};
			set(group.ClosestHit, ShaderType::CLOSEST_HIT);
			set(group.AnyHit, ShaderType::ANY_HIT);
			set(group.Intersection, ShaderType::INTERSECTION);
			if (!group.ClosestHit.empty())
				hit->SetClosestHitShaderImport(names.at(group.ClosestHit).c_str());
			if (!group.AnyHit.empty())
				hit->SetAnyHitShaderImport(names.at(group.AnyHit).c_str());
			if (!group.Intersection.empty())
				hit->SetIntersectionShaderImport(names.at(group.Intersection).c_str());
			sections[2].push_back(i);
		}
	}
	if (sections[0].empty())
		throw std::runtime_error("DXR program requires raygen");
	state.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>()->Config(resource.MaxPayloadSize, resource.MaxAttributeSize);
	state.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>()->SetRootSignature(data.Root.Get());
	if (data.LocalRoot)
		state.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>()->SetRootSignature(data.LocalRoot.Get());
	state.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>()->Config(resource.MaxRecursionDepth);
	check(mRayDevice->CreateStateObject(state, IID_PPV_ARGS(&data.State)), "CreateStateObject DXR");
	TrackGpuObjectDx12(data.State.Get(), mDevice.Get(), PROFILER::GpuResourceKind::Pipeline, "Ray-tracing pipeline");
	ComPtr<ID3D12StateObjectProperties> properties;
	check(data.State.As(&properties), "DXR state properties");
	const UINT stride = UINT(aligned(32 + data.RecordSize, 64));
	if (stride > 4096)
		throw std::runtime_error("DXR shader record exceeds limit");
	std::array<uint64_t, 4> offsets { };
	uint64_t bytes = 0;
	for (size_t s = 0; s < 4; ++s) {
		offsets[s] = bytes;
		bytes += sections[s].size() * stride;
	}
	std::vector<uint8_t> contents(bytes, 0);
	for (size_t s = 0; s < 4; ++s)
		for (size_t record = 0; record < sections[s].size(); ++record) {
			const auto i = sections[s][record];
			auto* identifier = properties->GetShaderIdentifier(identifiers[i].c_str());
			if (!identifier)
				throw std::runtime_error("Missing DXR shader identifier");
			auto* target = contents.data() + offsets[s] + record * stride;
			std::memcpy(target, identifier, 32);
			if (!resource.Groups[i].RecordData.empty())
				std::memcpy(target + 32, resource.Groups[i].RecordData.data(), resource.Groups[i].RecordData.size());
		}
	data.SBT = upload(contents.data(), contents.size());
	const auto base = data.SBT->GetGPUVirtualAddress();
	data.RaygenCount = UINT(sections[0].size());
	data.RaygenStride = stride;
	data.Rays.RayGenerationShaderRecord = { base + offsets[0], stride };
	auto table = [&](size_t s) {
		return D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE { sections[s].empty() ? 0 : base + offsets[s], sections[s].size() * stride,
			sections[s].empty() ? 0 : stride };
	};
	data.Rays.MissShaderTable = table(1);
	data.Rays.HitGroupTable = table(2);
	data.Rays.CallableShaderTable = table(3);
	mRayPrograms[key] = output;
	return output;
}
void DriverDx12::traceRays(const RayTracingDispatch& dispatch) {
	executeRayProgram(dispatch, false);
}
void DriverDx12::dispatchRayQueries(const RayTracingDispatch& dispatch) {
	if (!dispatch.Program || !dispatch.Program->isRayQuery())
		throw std::runtime_error("DXR query dispatch requires query program");
	executeRayProgram(dispatch, true);
}
void DriverDx12::dispatchComputeProgram(const RayTracingDispatch& dispatch) {
	executeRayProgram(dispatch, true);
}

struct DriverDx12::GraphTiming {
	struct Clock {
		UINT64 GpuTick = 0;
		UINT64 CpuTick = 0;
		UINT64 Frequency = 0;

		bool calibrate(ID3D12CommandQueue& queue) {
			return SUCCEEDED(queue.GetTimestampFrequency(&Frequency)) && Frequency && SUCCEEDED(queue.GetClockCalibration(&GpuTick, &CpuTick));
		}

		double milliseconds(UINT64 tick, UINT64 origin, double cpuFrequency) const {
			return 1000.0 * (double(CpuTick - origin) / cpuFrequency + (double(tick) - double(GpuTick)) / double(Frequency));
		}
	};
	Clock Compute;
	Clock Graphics;
	double CpuFrequency = 0;
	ComPtr<ID3D12QueryHeap> Queries;
	Dx12Resource Readback;
	bool Resolved = false;

	~GraphTiming() {
		if (!Resolved) {
			return;
		}
		const D3D12_RANGE range { 0, 4 * sizeof(UINT64) };
		void* mapped = nullptr;
		if (FAILED(Readback->Map(0, &range, &mapped))) {
			return;
		}
		std::array<UINT64, 4> ticks {};
		std::memcpy(ticks.data(), mapped, sizeof(ticks));
		const D3D12_RANGE written {};
		Readback->Unmap(0, &written);
		const auto origin = std::min(Compute.CpuTick, Graphics.CpuTick);
		const auto computeStart = Compute.milliseconds(ticks[0], origin, CpuFrequency);
		const auto computeEnd = Compute.milliseconds(ticks[1], origin, CpuFrequency);
		const auto graphicsStart = Graphics.milliseconds(ticks[2], origin, CpuFrequency);
		const auto graphicsEnd = Graphics.milliseconds(ticks[3], origin, CpuFrequency);
		std::cout << "GRAPH_GPU_QUEUES backend=dx12 compute_ms=" << computeEnd - computeStart << " graphics_ms=" << graphicsEnd - graphicsStart
				  << " overlap_ms=" << std::max(0.0, std::min(computeEnd, graphicsEnd) - std::max(computeStart, graphicsStart)) << '\n';
	}
};

void DriverDx12::recordGraphTimingEnd() {
	if (mGraphTiming) {
		mCommandList->EndQuery(mGraphTiming->Queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 3);
	}
}

void DriverDx12::resolveGraphTiming() {
	if (mGraphTiming) {
		mCommandList->ResolveQueryData(mGraphTiming->Queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 4, mGraphTiming->Readback.Get(), 0);
		mGraphTiming->Resolved = true;
		mGraphTiming.reset();
	}
}

void DriverDx12::dispatchGraphCompute(const RayTracingDispatch& dispatch) {
	requireOwnerThread();
	if (!hasGraphComputeQueue() || mGraphComputePending) {
		throw std::logic_error("DX12 graph compute queue is unavailable");
	}
	struct Recording {
		ComPtr<ID3D12CommandAllocator> Allocator;
		ComPtr<ID3D12GraphicsCommandList4> List;
	};
	auto recording = std::make_shared<Recording>();
	check(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&recording->Allocator)), "Graph compute allocator");
	check(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, recording->Allocator.Get(), nullptr, IID_PPV_ARGS(&recording->List)),
		"Graph compute command list");
	if (mProfileGraphQueues) {
		auto timing = std::make_shared<GraphTiming>();
		LARGE_INTEGER cpuFrequency {};
		if (QueryPerformanceFrequency(&cpuFrequency) && timing->Compute.calibrate(*mGraphComputeQueue.Get())
			&& timing->Graphics.calibrate(*mCommandQueue.Get())) {
			timing->CpuFrequency = static_cast<double>(cpuFrequency.QuadPart);
			const D3D12_QUERY_HEAP_DESC queryInfo { D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 4, 0 };
			check(mDevice->CreateQueryHeap(&queryInfo, IID_PPV_ARGS(&timing->Queries)), "Create graph queue timestamps");
			timing->Readback = d3dUtil::CreateBuffer(4 * sizeof(UINT64), D3D12_HEAP_TYPE_READBACK);
			mRayFrameResources.push_back(timing);
			mGraphTiming = std::move(timing);
			recording->List->EndQuery(mGraphTiming->Queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
		}
	}
	for (const auto& [binding, resources] : dispatch.Bindings) {
		for (const auto& resource : resources) {
			const auto* storage = std::get_if<std::shared_ptr<StorageBufferInterface>>(&resource);
			auto buffer = storage ? std::dynamic_pointer_cast<StorageBufferDx12>(*storage) : nullptr;
			if (!buffer) {
				throw std::invalid_argument("DX12 graph compute requires storage buffer bindings");
			}
			buffer->transition(mCommandList.Get(), D3D12_RESOURCE_STATE_COMMON);
		}
	}
	executeRayProgram(dispatch, true, recording->List.Get());
	if (mGraphTiming) {
		recording->List->EndQuery(mGraphTiming->Queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
	}
	check(recording->List->Close(), "Close graph compute list");
	mRayFrameResources.push_back(recording);

	const auto value = ++mGraphComputeValue;
	splitGraphGraphics();
	check(mCommandQueue->Signal(mGraphGraphicsFence.Get(), value), "Signal graph graphics fence");
	check(mGraphComputeQueue->Wait(mGraphGraphicsFence.Get(), value), "Wait for graph graphics fence");
	ID3D12CommandList* list = recording->List.Get();
	mGraphComputeQueue->ExecuteCommandLists(1, &list);
	check(mGraphComputeQueue->Signal(mGraphComputeFence.Get(), value), "Signal graph compute fence");
	mGraphComputePending = value;
	if (mGraphTiming) {
		mCommandList->EndQuery(mGraphTiming->Queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
	}
}

void DriverDx12::executeRayProgram(const RayTracingDispatch& dispatch, bool compute, ID3D12GraphicsCommandList4* computeList) {
	auto* list = computeList ? computeList : mCommandList.Get();
	auto program = std::dynamic_pointer_cast<ProgramDx>(dispatch.Program);
	if (!program || program->Driver != this || program->Data->Compute != compute)
		throw std::runtime_error("DXR program/driver/dispatch mismatch");
	const auto& data = *program->Data;
	if (dispatch.PushConstants.size() != data.PushSize || (!compute && dispatch.RaygenRecord >= data.RaygenCount))
		throw std::runtime_error("DXR push constant size or raygen record mismatch");
	uint64_t invocations = 1;
	std::array<UINT, 3> groups { };
	for (size_t i = 0; i < 3; ++i) {
		if (!dispatch.Extent[i])
			throw std::runtime_error("DXR extent must be positive");
		if (!compute) {
			if (dispatch.Extent[i] > mRayTracingCapabilities.MaxDispatchInvocations / invocations)
				throw std::runtime_error("DXR dispatch exceeds GPU limit");
			invocations *= dispatch.Extent[i];
		}
		if (compute) {
			if (!data.LocalSize[i])
				throw std::runtime_error("Invalid compute local size");
			groups[i] = UINT((uint64_t(dispatch.Extent[i]) + data.LocalSize[i] - 1) / data.LocalSize[i]);
			if (groups[i] > 65535)
				throw std::runtime_error("DXR compute group count exceeds limit");
		}
	}
	if (!compute && invocations > mRayTracingCapabilities.MaxDispatchInvocations)
		throw std::runtime_error("DXR dispatch exceeds GPU limit");
	auto frame = std::make_shared<DispatchData>();
	frame->Snapshot = dispatch;
	auto makeHeap = [&](UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type, ComPtr<ID3D12DescriptorHeap>& heap) {
		if (!count)
			return;
		D3D12_DESCRIPTOR_HEAP_DESC desc { type, count, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
		check(mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)), "DXR dispatch descriptor heap");
	};
	makeHeap(data.Descriptors, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, frame->Heap);
	makeHeap(data.Samplers, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, frame->SamplerHeap);
	uav(list);
	std::set<StorageBufferDx12*> computeBuffers;
	std::set<const TextureDx12*> sampled, storage;
	for (const auto& b : data.Bindings) {
		const std::vector<RayTracingResource>* resources = nullptr;
		const auto numeric = std::to_string(b.Space) + ":" + std::to_string(b.Register);
		const auto qualified = std::to_string(b.Space) + ":" + registerClass(b) + std::to_string(b.Register);
		if (auto found = dispatch.Bindings.find(qualified); found != dispatch.Bindings.end())
			resources = &found->second;
		else if (auto found = dispatch.Bindings.find(numeric); found != dispatch.Bindings.end())
			resources = &found->second;
		else
			for (const auto& name : b.Names)
				if (auto found = dispatch.Bindings.find(name); found != dispatch.Bindings.end()) {
					resources = &found->second;
					break;
				}
		if (!resources || resources->size() != b.Count)
			throw std::runtime_error("Missing DXR binding or wrong array size: " + numeric + " (" + *b.Names.begin() + ")");
		auto heap = b.Sampler ? frame->SamplerHeap : frame->Heap;
		const auto type = b.Sampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		const auto increment = mDevice->GetDescriptorHandleIncrementSize(type);
		for (UINT i = 0; i < b.Count; ++i) {
			const auto& resource = resources->at(i);
			auto handle = heap->GetCPUDescriptorHandleForHeapStart();
			handle.ptr += SIZE_T(b.HeapOffset + i) * increment;
			if (b.Type == D3D_SIT_RTACCELERATIONSTRUCTURE) {
				const auto* value = std::get_if<std::shared_ptr<AccelerationStructure>>(&resource);
				auto as = value ? std::dynamic_pointer_cast<ASDx>(*value) : nullptr;
				if (!as || as->Driver != this || !as->Data->Top)
					throw std::runtime_error("DXR AS binding requires this driver's TLAS");
				D3D12_SHADER_RESOURCE_VIEW_DESC desc { };
				desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
				desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				desc.RaytracingAccelerationStructure.Location = as->Data->Storage->GetGPUVirtualAddress();
				mDevice->CreateShaderResourceView(nullptr, &desc, handle);
			}
			else if (b.Type == D3D_SIT_CBUFFER) {
				const auto* value = std::get_if<std::shared_ptr<UniformBufferInterface>>(&resource);
				auto uniform = value ? std::dynamic_pointer_cast<UniformBufferDx12>(*value) : nullptr;
				if (!uniform)
					throw std::runtime_error("Invalid DXR uniform");
				D3D12_CONSTANT_BUFFER_VIEW_DESC desc { uniform->getGpuAddress(), UINT(uniform->getSizeByte()) };
				mDevice->CreateConstantBufferView(&desc, handle);
			}
			else if (b.Dimension == D3D_SRV_DIMENSION_BUFFER || b.Dimension == D3D_SRV_DIMENSION_BUFFEREX) {
				ID3D12Resource* raw = nullptr;
				size_t bytes = 0, stride = 0;
				const auto state = b.Uav ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				if (const auto* value = std::get_if<std::shared_ptr<StorageBufferInterface>>(&resource)) {
					auto buffer = std::dynamic_pointer_cast<StorageBufferDx12>(*value);
					if (!buffer)
						throw std::runtime_error("Invalid DXR storage");
					buffer->transition(list, state);
					if (computeList) {
						computeBuffers.insert(buffer.get());
					}
					raw = buffer->getBuffer().Get();
					bytes = buffer->getSizeByte();
					stride = buffer->getStride();
				}
				else if (const auto* value = std::get_if<std::shared_ptr<VertexBufferInterface>>(&resource)) {
					auto buffer = std::dynamic_pointer_cast<VertexBufferDx12>(*value);
					if (!buffer)
						throw std::runtime_error("Invalid DXR vertex storage");
					buffer->transition(list, state);
					raw = buffer->getBuffer().Get();
					bytes = buffer->getSizeByte();
					stride = buffer->getStride();
				}
				else
					throw std::runtime_error("DXR buffer binding requires storage or vertex buffer");
				if (b.Type != D3D_SIT_STRUCTURED && b.Type != D3D_SIT_UAV_RWSTRUCTURED && !b.Raw)
					throw std::runtime_error("DXR buffers require StructuredBuffer or ByteAddressBuffer");
				const auto element = b.Raw ? 4 : stride;
				if (!element || bytes % element || bytes / element > UINT_MAX)
					throw std::runtime_error("Invalid DXR buffer element layout");
				if (b.Uav) {
					D3D12_UNORDERED_ACCESS_VIEW_DESC desc { };
					desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
					desc.Format = b.Raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
					desc.Buffer.NumElements = UINT(bytes / (b.Raw ? 4 : stride));
					desc.Buffer.StructureByteStride = b.Raw ? 0 : UINT(stride);
					desc.Buffer.Flags = b.Raw ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
					mDevice->CreateUnorderedAccessView(raw, nullptr, &desc, handle);
				}
				else {
					D3D12_SHADER_RESOURCE_VIEW_DESC desc { };
					desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
					desc.Format = b.Raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
					desc.Buffer.NumElements = UINT(bytes / (b.Raw ? 4 : stride));
					desc.Buffer.StructureByteStride = b.Raw ? 0 : UINT(stride);
					desc.Buffer.Flags = b.Raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
					mDevice->CreateShaderResourceView(raw, &desc, handle);
				}
			}
			else {
				const auto* value = std::get_if<std::shared_ptr<TextureInterface>>(&resource);
				auto texture = value ? std::dynamic_pointer_cast<TextureDx12>(*value) : nullptr;
				if (!texture)
					throw std::runtime_error("Invalid DXR image/sampler");
				if (b.Sampler) {
					auto desc = sampler(*texture);
					mDevice->CreateSampler(&desc, handle);
					continue;
				}
				auto raw = texture->getResource().Get();
				auto format = raw->GetDesc().Format;
				if (b.Uav) {
					storage.insert(texture.get());
					if (!(raw->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
						throw std::runtime_error("DXR output image lacks storage usage");
					texture->setState(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					D3D12_UNORDERED_ACCESS_VIEW_DESC desc { };
					desc.Format = format;
					desc.ViewDimension = b.Dimension == D3D_SRV_DIMENSION_TEXTURE3D ? D3D12_UAV_DIMENSION_TEXTURE3D : D3D12_UAV_DIMENSION_TEXTURE2D;
					if (desc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE3D)
						desc.Texture3D.WSize = UINT(texture->getDepth());
					mDevice->CreateUnorderedAccessView(raw, nullptr, &desc, handle);
				}
				else {
					sampled.insert(texture.get());
					texture->setState(list, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					D3D12_SHADER_RESOURCE_VIEW_DESC desc { };
					desc.Format = format == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_R32_FLOAT
						: format == DXGI_FORMAT_R24G8_TYPELESS		 ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS
																	 : format;
					desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					desc.ViewDimension = static_cast<D3D12_SRV_DIMENSION>(b.Dimension);
					if (b.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE)
						desc.TextureCube.MipLevels = raw->GetDesc().MipLevels;
					else if (b.Dimension == D3D_SRV_DIMENSION_TEXTURE3D)
						desc.Texture3D.MipLevels = raw->GetDesc().MipLevels;
					else if (b.Dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY) {
						desc.Texture2DArray.MipLevels = raw->GetDesc().MipLevels;
						desc.Texture2DArray.ArraySize = raw->GetDesc().DepthOrArraySize;
					}
					else
						desc.Texture2D.MipLevels = raw->GetDesc().MipLevels;
					mDevice->CreateShaderResourceView(raw, &desc, handle);
				}
			}
		}
	}
	for (auto* image : sampled)
		if (storage.contains(image))
			throw std::runtime_error("DXR image cannot be sampled and storage in one dispatch");
	std::vector<ID3D12DescriptorHeap*> heaps;
	if (frame->Heap)
		heaps.push_back(frame->Heap.Get());
	if (frame->SamplerHeap)
		heaps.push_back(frame->SamplerHeap.Get());
	if (!heaps.empty())
		list->SetDescriptorHeaps(UINT(heaps.size()), heaps.data());
	list->SetComputeRootSignature(data.Root.Get());
	for (const auto& b : data.Bindings) {
		auto heap = b.Sampler ? frame->SamplerHeap : frame->Heap;
		auto handle = heap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += UINT64(b.HeapOffset)
			* mDevice->GetDescriptorHandleIncrementSize(b.Sampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		list->SetComputeRootDescriptorTable(b.Root, handle);
	}
	if (data.PushRoot >= 0)
		list->SetComputeRoot32BitConstants(data.PushRoot, data.PushSize / 4, dispatch.PushConstants.data(), 0);
	if (compute) {
		list->SetPipelineState(data.ComputeState.Get());
		list->Dispatch(groups[0], groups[1], groups[2]);
	}
	else {
		list->SetPipelineState1(data.State.Get());
		auto desc = data.Rays;
		desc.RayGenerationShaderRecord.StartAddress += uint64_t(dispatch.RaygenRecord) * data.RaygenStride;
		desc.Width = dispatch.Extent[0];
		desc.Height = dispatch.Extent[1];
		desc.Depth = dispatch.Extent[2];
		list->DispatchRays(&desc);
	}
	uav(list);
	for (auto* buffer : computeBuffers) {
		buffer->transition(list, D3D12_RESOURCE_STATE_COMMON);
	}
	mRayFrameResources.push_back(frame);
	if (!computeList) {
		setDirty(Dirty::PIPELINE);
	}
}
#endif
