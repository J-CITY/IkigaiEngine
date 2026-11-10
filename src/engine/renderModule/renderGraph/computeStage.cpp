#include "computeStage.h"

#include "coreModule/core/core.h"

#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/backends/interface/shaderInterface.h"
#include "renderModule/backends/interface/storageBufferInterface.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "renderModule/backends/interface/uniformBufferInterface.h"
#include "renderModule/gameRenderer.h"
#include "renderModule/render.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/shaderManager.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	using TextureDesc = RenderGraphPipeline::TextureDesc;

	const TextureDesc* FindPipelineTexture(const RenderGraphPipeline& pipeline, const std::string& name) {
		for (const auto& texture : pipeline.mDescriptor.Textures) {
			if (texture.Name == name || (texture.History && texture.Name + "Previous" == name)) {
				return &texture;
			}
		}
		return nullptr;
	}

	const RenderGraphPipeline::BufferDescriptor* FindPipelineBuffer(const RenderGraphPipeline& pipeline, const std::string& name) {
		for (const auto& buffer : pipeline.mDescriptor.Buffers) {
			if (buffer.Name == name) {
				return &buffer;
			}
		}
		return nullptr;
	}

	size_t StorageBufferBytes(const RenderGraphPipeline& pipeline, const std::string& name, const std::string& stage) {
		const auto found = pipeline.mStorageBuffers.find(name);
		if (found == pipeline.mStorageBuffers.end() || !found->second) {
			throw std::runtime_error(stage + ": unknown storage buffer " + name);
		}
		return found->second->getSizeByte();
	}

	std::vector<const PipelineStage::Descriptor*> CollectStageDescriptors(const RenderGraphPipeline::Descriptor& descriptor) {
		std::vector<const PipelineStage::Descriptor*> result;
		auto append = [&](const std::vector<PipelineStage::Descriptor>& stages) {
			for (const auto& stage : stages) {
				result.push_back(&stage);
			}
		};
		append(descriptor.StartStages);
		append(descriptor.Stages);
		for (const auto& pass : descriptor.LightPasses) {
			append(pass.InitStages);
			append(pass.CaptureStages);
			append(pass.PrepareStages);
			append(pass.ResolveStages);
		}
		return result;
	}

	const ShaderReflection::Uniform& FindStorageImage(const ShaderReflection& reflection, const std::string& name, const std::string& stage) {
		if (!reflection.mNameToUniforms.contains(name)) {
			throw std::runtime_error(stage + ": unknown MipChain image uniform " + name);
		}
		const auto& uniform = reflection.mUniforms.at(reflection.mNameToUniforms.at(name));
		if (uniform.mType != ShaderReflection::UniformType::IMAGE_2D) {
			throw std::runtime_error(stage + ": MipChain uniform must be an image2D: " + name);
		}
		return uniform;
	}

	bool WritesOnlyDispatchMipZero(const PipelineStage::Descriptor& stage, const ShaderInterface& shader, const TextureDesc& target) {
		using UniformType = ShaderReflection::UniformType;
		if (shader.bindingMode() != ShaderBindingMode::Bound || !target.FullMipChain || !target.Path.empty() || target.History
			|| !target.Resource.storage || target.Resource.texType != TextureType::TEXTURE_2D || target.Name != stage.DispatchTexture) {
			return false;
		}

		bool writesTarget = false;
		for (const auto& uniform : shader.getReflection().mUniforms) {
			if (uniform.mSet != 0 || uniform.mDescriptorCount != 1 || uniform.mRuntimeArray) {
				return false;
			}
			switch (uniform.mType) {
			case UniformType::SAMPLER_2D:
			case UniformType::SAMPLER_3D:
			case UniformType::SAMPLER_CUBE:
			case UniformType::SAMPLER_2D_ARRAY:
			case UniformType::UNIFORM_BUFFER:
				break;
			case UniformType::IMAGE_2D: {
				const auto binding = stage.Uniforms.find(uniform.mName);
				const auto* resource = binding == stage.Uniforms.end() ? nullptr : std::get_if<std::string>(&binding->second);
				if (writesTarget || !uniform.mWriteOnly || uniform.mReadOnly || !resource || *resource != target.Name
					|| uniform.mName.starts_with("engine_") || uniform.mName.starts_with("Engine")) {
					return false;
				}
				writesTarget = true;
				break;
			}
			default:
				return false;
			}
		}
		return writesTarget;
	}

	ShaderReflection ReflectComputeProgram(const RayTracingProgramResource& resource, const std::string& path) {
		if (resource.Shaders.size() != 1 || resource.Shaders[0].Stage != ShaderType::COMPUTE) {
			throw std::runtime_error("Compute program needs exactly one compute shader: " + path);
		}
		if (resource.Shaders[0].Language != "GLSL") {
			throw std::runtime_error("Compute reflection needs a GLSL program: " + path);
		}
		ShaderReflection reflection;
		ShaderInterface::GetReflection(reflection, CompileGlslToSpirv(ShaderType::COMPUTE, resource.Shaders[0].Source, {}), ShaderType::COMPUTE);
		return reflection;
	}

	void ResolveAsyncImageProgramBindings(
		const RenderGraphPipeline& pipeline, const PipelineStage::Descriptor& desc, const ShaderReflection& reflection, ComputeStageState& state) {
		using UniformType = ShaderReflection::UniformType;
		using Access = GRAPH::Access;
		std::set<std::string> usedBindings;
		for (const auto& uniform : reflection.mUniforms) {
			auto binding = desc.Bindings.find(std::to_string(uniform.mSet) + ":" + std::to_string(uniform.mBind));
			if (binding == desc.Bindings.end()) {
				binding = desc.Bindings.find(uniform.mName);
			}
			if (binding == desc.Bindings.end() || uniform.mDescriptorCount != 1 || uniform.mRuntimeArray
				|| !usedBindings.insert(binding->first).second) {
				throw std::runtime_error(desc.Name + ": async image program needs one binding for " + uniform.mName);
			}
			const auto& source = binding->second;
			const auto uses = [&](Access access) {
				return std::ranges::any_of(desc.ResourceUses, [&](const auto& use) { return use.Resource == source && use.Mode == access; });
			};
			const auto texture = pipeline.mTextures.find(source);
			const auto buffer = pipeline.mStorageBuffers.find(source);
			const bool sampled = uses(Access::SAMPLED);
			const bool storageRead = uses(Access::STORAGE_READ);
			const bool storageWrite = uses(Access::STORAGE_WRITE);
			if (texture != pipeline.mTextures.end()
				&& ((uniform.mType == UniformType::SAMPLER_2D && sampled && !storageRead && !storageWrite)
					|| (uniform.mType == UniformType::IMAGE_2D && !sampled && storageRead == !uniform.mWriteOnly
						&& storageWrite == !uniform.mReadOnly))) {
				state.AsyncImageProgramBindings.push_back({ binding->first, &texture->second, {} });
			}
			else if (uniform.mType == UniformType::STORAGE_BUFFER && buffer != pipeline.mStorageBuffers.end()
				&& (uniform.mReadOnly ? storageRead : storageWrite)) {
				state.AsyncImageProgramBindings.push_back({ binding->first, nullptr, buffer->second });
			}
			else {
				throw std::runtime_error(desc.Name + ": graph access does not match async image program binding " + uniform.mName);
			}
		}
		if (usedBindings.size() != desc.Bindings.size()) {
			throw std::runtime_error(desc.Name + ": async image program has bindings absent from shader reflection");
		}
	}

	bool HasPipelineGpuFeature(const GpuFeatures& features, PipelineGpuFeature feature) {
		switch (feature) {
		case PipelineGpuFeature::COMPUTE:
			return features.Compute;
		case PipelineGpuFeature::BUFFER_USAGE_FLAGS:
			return features.BufferUsageFlags;
		case PipelineGpuFeature::BUFFER_TRANSFER:
			return features.BufferTransfer;
		case PipelineGpuFeature::BUFFER_READBACK:
			return features.BufferReadback;
		case PipelineGpuFeature::DRAW_INDIRECT:
			return features.DrawIndirect;
		case PipelineGpuFeature::DRAW_INDIRECT_COUNT:
			return features.DrawIndirectCount;
		case PipelineGpuFeature::DISPATCH_INDIRECT:
			return features.DispatchIndirect;
		case PipelineGpuFeature::STORAGE_IMAGE_MIP_VIEWS:
			return features.StorageImageMipViews;
		case PipelineGpuFeature::HALF_FLOAT_TEXTURES:
			return features.HalfFloatTextures;
		case PipelineGpuFeature::SAMPLER_MIN_MAX_REDUCTION:
			return features.SamplerMinMaxReduction;
		case PipelineGpuFeature::SUBGROUP_BASIC:
			return features.SubgroupBasic;
		case PipelineGpuFeature::SUBGROUP_BALLOT:
			return features.SubgroupBallot;
		case PipelineGpuFeature::BUFFER_INT64_ATOMICS:
			return features.BufferInt64Atomics;
		case PipelineGpuFeature::TIMESTAMP_QUERIES:
			return features.TimestampQueries;
		}
		return false;
	}

	const char* PipelineGpuFeatureName(PipelineGpuFeature feature) {
		switch (feature) {
		case PipelineGpuFeature::COMPUTE:
			return "COMPUTE";
		case PipelineGpuFeature::BUFFER_USAGE_FLAGS:
			return "BUFFER_USAGE_FLAGS";
		case PipelineGpuFeature::BUFFER_TRANSFER:
			return "BUFFER_TRANSFER";
		case PipelineGpuFeature::BUFFER_READBACK:
			return "BUFFER_READBACK";
		case PipelineGpuFeature::DRAW_INDIRECT:
			return "DRAW_INDIRECT";
		case PipelineGpuFeature::DRAW_INDIRECT_COUNT:
			return "DRAW_INDIRECT_COUNT";
		case PipelineGpuFeature::DISPATCH_INDIRECT:
			return "DISPATCH_INDIRECT";
		case PipelineGpuFeature::STORAGE_IMAGE_MIP_VIEWS:
			return "STORAGE_IMAGE_MIP_VIEWS";
		case PipelineGpuFeature::HALF_FLOAT_TEXTURES:
			return "HALF_FLOAT_TEXTURES";
		case PipelineGpuFeature::SAMPLER_MIN_MAX_REDUCTION:
			return "SAMPLER_MIN_MAX_REDUCTION";
		case PipelineGpuFeature::SUBGROUP_BASIC:
			return "SUBGROUP_BASIC";
		case PipelineGpuFeature::SUBGROUP_BALLOT:
			return "SUBGROUP_BALLOT";
		case PipelineGpuFeature::BUFFER_INT64_ATOMICS:
			return "BUFFER_INT64_ATOMICS";
		case PipelineGpuFeature::TIMESTAMP_QUERIES:
			return "TIMESTAMP_QUERIES";
		}
		return "UNKNOWN";
	}

	uint32_t CeilDiv(size_t value, uint32_t divisor) {
		if (!divisor) {
			throw std::runtime_error("Invalid compute local size");
		}
		return static_cast<uint32_t>((value + divisor - 1) / divisor);
	}
}

BufferUsage ResolveBufferUsage(const std::vector<PipelineBufferUsage>& usage) {
	BufferUsage result = BufferUsage::NONE;
	for (const auto flag : usage) {
		switch (flag) {
		case PipelineBufferUsage::STORAGE:
			result = result | BufferUsage::STORAGE;
			break;
		case PipelineBufferUsage::INDIRECT:
			result = result | BufferUsage::INDIRECT;
			break;
		case PipelineBufferUsage::VERTEX:
			result = result | BufferUsage::VERTEX;
			break;
		case PipelineBufferUsage::INDEX:
			result = result | BufferUsage::INDEX;
			break;
		case PipelineBufferUsage::TRANSFER_SRC:
			result = result | BufferUsage::TRANSFER_SRC;
			break;
		case PipelineBufferUsage::TRANSFER_DST:
			result = result | BufferUsage::TRANSFER_DST;
			break;
		case PipelineBufferUsage::READBACK:
			result = result | BufferUsage::READBACK;
			break;
		}
	}
	return result;
}

bool SupportsReflectedComputePrograms(DriverInterface& driver) {
	if (driver.gpuFeatures().Compute) {
		return true;
	}
	const auto backend = DriverInterface::settings.backend;
	return backend == RenderSettings::Backend::VULKAN || backend == RenderSettings::Backend::DIRECTX12;
}

void CreatePipelineBuffers(RenderGraphPipeline& pipeline, Renderer& renderer) {
	std::set<std::string> cleared;
	for (const auto* stage : CollectStageDescriptors(pipeline.mDescriptor)) {
		for (const auto& clear : stage->Compute.ClearBuffers) {
			cleared.insert(clear.Buffer);
		}
	}
	const auto features = renderer.getDriver()->gpuFeatures();
	for (const auto& b : pipeline.mDescriptor.Buffers) {
		if (!b.Size && !b.Count) {
			throw std::runtime_error("Empty pipeline buffer: " + b.Name);
		}
		if (b.Type == RenderGraphPipeline::BufferType::UNIFORM) {
			if (b.Count || !b.Usage.empty() || cleared.contains(b.Name)) {
				throw std::runtime_error("Count, Usage and ClearBuffers apply to STORAGE buffers only: " + b.Name);
			}
			pipeline.mUniformBuffers.emplace(b.Name, renderer.createUniformBuffer(nullptr, b.Size));
			continue;
		}
		if (b.Count && b.Size && b.Count != b.Size) {
			throw std::runtime_error("Pipeline buffer Size and Count disagree: " + b.Name);
		}
		const size_t count = b.Count ? b.Count : b.Size;
		auto usage = ResolveBufferUsage(b.Usage);
		if (cleared.contains(b.Name)) {
			usage = usage | BufferUsage::TRANSFER_DST;
		}
		if (usage == BufferUsage::NONE || usage == BufferUsage::STORAGE) {
			pipeline.mStorageBuffers.emplace(b.Name, renderer.createStorageBuffer(nullptr, count, b.Stride));
			pipeline.mBufferUsages[b.Name] = BufferUsage::STORAGE;
			continue;
		}
		if (!b.Stride) {
			throw std::runtime_error("Pipeline buffer with Usage needs a Stride: " + b.Name);
		}
		usage = usage | BufferUsage::STORAGE;
		if (features.BufferUsageFlags) {
			pipeline.mStorageBuffers.emplace(b.Name, renderer.getDriver()->createStorageBuffer(nullptr, count, b.Stride, usage));
			pipeline.mBufferUsages[b.Name] = usage;
		}
		else {
			// Stages that depend on the missing usage report it through their Unsupported policy.
			pipeline.mStorageBuffers.emplace(b.Name, renderer.createStorageBuffer(nullptr, count, b.Stride));
			pipeline.mBufferUsages[b.Name] = BufferUsage::STORAGE;
			std::cout << "GPU_DRIVEN_FALLBACK buffer " << b.Name << ": buffer usage flags unavailable, created as plain storage buffer\n";
		}
	}
}

bool BypassUnsupportedComputeStageBeforeLoad(RenderGraphPipeline& pipeline, PipelineStage& stage) {
	const auto& desc = stage.mDescriptor;
	if (desc.Draw != DrawContent::COMPUTE || !desc.Enabled || desc.Material.empty() || desc.Compute.Requires.empty()) {
		return false;
	}
	if (desc.Unsupported != "ERROR" && desc.Unsupported != "BYPASS") {
		throw std::runtime_error("Unknown COMPUTE Unsupported policy: " + desc.Name);
	}
	const auto features = RESOURCES::ServiceManager::Get<Renderer>().getDriver()->gpuFeatures();
	std::string missing;
	for (const auto feature : desc.Compute.Requires) {
		if (!HasPipelineGpuFeature(features, feature)) {
			missing = PipelineGpuFeatureName(feature);
			break;
		}
	}
	if (missing.empty()) {
		return false;
	}
	const auto reason = "required GPU feature " + missing + " is unavailable on this backend/GPU";
	if (desc.Unsupported == "ERROR") {
		throw std::runtime_error("COMPUTE stage " + desc.Name + ": " + reason);
	}
	for (const auto& output : desc.Writes) {
		if (FindPipelineTexture(pipeline, output) && !desc.Bypass.contains(output)) {
			throw std::runtime_error("Unsupported COMPUTE stage needs explicit Bypass for output " + output);
		}
	}
	for (const auto& [output, input] : desc.Bypass) {
		if (!FindPipelineTexture(pipeline, output) || !FindPipelineTexture(pipeline, input)) {
			throw std::runtime_error(desc.Name + ": Bypass entries must alias pipeline textures: " + output);
		}
	}
	auto state = std::make_shared<ComputeStageState>();
	state->Bypassed = true;
	stage.mCompute = state;
	std::cout << "GPU_DRIVEN_FALLBACK " << desc.Name << ": " << reason << " (material not loaded)\n";
	return true;
}

void LoadComputeStage(RenderGraphPipeline& pipeline, PipelineStage& stage) {
	const auto& desc = stage.mDescriptor;
	const auto& compute = desc.Compute;
	const auto& name = desc.Name;
	if (desc.Draw != DrawContent::COMPUTE) {
		throw std::runtime_error("Compute settings require Draw COMPUTE: " + name);
	}
	if (desc.Unsupported != "ERROR" && desc.Unsupported != "BYPASS") {
		throw std::runtime_error("Unknown COMPUTE Unsupported policy: " + name);
	}
	auto state = std::make_shared<ComputeStageState>();
	state->UsesProgram = !desc.Program.empty();
	if (state->UsesProgram) {
		if (compute.ProgramKind != ComputeProgramKind::COMPUTE) {
			throw std::runtime_error("Compute settings on a Program stage require ProgramKind COMPUTE: " + name);
		}
		if (!desc.Material.empty()) {
			throw std::runtime_error("COMPUTE stage accepts either Program or Material: " + name);
		}
		if (!compute.DispatchBuffer.empty() || !compute.MipChain.Texture.empty()) {
			throw std::runtime_error("DispatchBuffer and MipChain require a Material compute stage (the driver contract has no indirect or per-mip "
									 "reflected-program dispatch): "
				+ name);
		}
		if (desc.LaunchX <= 0 || desc.LaunchY <= 0 || desc.LaunchZ <= 0) {
			throw std::runtime_error("Invalid compute launch extent: " + name);
		}
	}
	else {
		if (compute.ProgramKind == ComputeProgramKind::COMPUTE) {
			throw std::runtime_error("ProgramKind COMPUTE requires Program: " + name);
		}
		if (!stage.mMaterial) {
			throw std::runtime_error("COMPUTE stage requires Material or Program: " + name);
		}
	}

	// Dispatch sizing: exactly one source.
	const bool texture = !desc.DispatchTexture.empty() || (state->UsesProgram && !desc.LaunchTexture.empty());
	const bool groups = !compute.Groups.empty();
	const bool indirect = !compute.DispatchBuffer.empty();
	const bool mipChain = !compute.MipChain.Texture.empty();
	if (int(texture) + int(groups) + int(indirect) + int(mipChain) > 1) {
		throw std::runtime_error("COMPUTE stage needs one dispatch source (DispatchTexture, Groups, DispatchBuffer or MipChain): " + name);
	}
	if (!desc.DispatchTexture.empty() && !desc.LaunchTexture.empty()) {
		throw std::runtime_error("DispatchTexture and LaunchTexture are exclusive: " + name);
	}
	if (texture) {
		state->Mode = ComputeDispatchMode::TEXTURE;
		state->DispatchTexture = desc.DispatchTexture.empty() ? desc.LaunchTexture : desc.DispatchTexture;
		if (!FindPipelineTexture(pipeline, state->DispatchTexture)) {
			throw std::runtime_error(name + ": unknown dispatch texture " + state->DispatchTexture);
		}
	}
	else if (groups) {
		state->Mode = ComputeDispatchMode::GROUPS;
		if (compute.Groups.size() != 3) {
			throw std::runtime_error("Compute Groups need three values: " + name);
		}
		for (size_t i = 0; i < 3; ++i) {
			if (compute.Groups[i] <= 0) {
				throw std::runtime_error("Compute Groups must be positive: " + name);
			}
			state->Groups[i] = static_cast<uint32_t>(compute.Groups[i]);
		}
	}
	else if (indirect) {
		state->Mode = ComputeDispatchMode::INDIRECT;
		state->DispatchBuffer = compute.DispatchBuffer;
		state->DispatchOffset = compute.DispatchOffset;
		const auto bytes = StorageBufferBytes(pipeline, compute.DispatchBuffer, name);
		if (compute.DispatchOffset % 4 || compute.DispatchOffset + sizeof(DispatchIndirectCommand) > bytes) {
			throw std::runtime_error(name + ": DispatchOffset must be 4-byte aligned and leave room for DispatchIndirectCommand");
		}
		const auto* buffer = FindPipelineBuffer(pipeline, compute.DispatchBuffer);
		if (!buffer || !HasBufferUsage(ResolveBufferUsage(buffer->Usage), BufferUsage::INDIRECT)) {
			throw std::runtime_error(name + ": DispatchBuffer must declare Usage INDIRECT: " + compute.DispatchBuffer);
		}
	}
	else if (mipChain) {
		state->Mode = ComputeDispatchMode::MIP_CHAIN;
		const auto& chain = compute.MipChain;
		const auto* target = FindPipelineTexture(pipeline, chain.Texture);
		if (!target || !target->Resource.storage || target->Resource.texType != TextureType::TEXTURE_2D) {
			throw std::runtime_error(name + ": MipChain texture must be a 2D storage texture: " + chain.Texture);
		}
		if (chain.Output.empty()) {
			throw std::runtime_error(name + ": MipChain needs an Output image uniform");
		}
		if (!chain.Input.empty() && chain.FirstMip == 0) {
			throw std::runtime_error(name + ": MipChain Input reads mip - 1 and needs FirstMip >= 1");
		}
		const auto& reflection = stage.mMaterial->getShader()->getReflection();
		state->MipTexture = chain.Texture;
		const auto& output = FindStorageImage(reflection, chain.Output, name);
		if (output.mReadOnly) {
			throw std::runtime_error(name + ": MipChain Output must be writable");
		}
		state->MipOutputBinding = output.mBind;
		const ShaderReflection::Uniform* input = nullptr;
		if (!chain.Input.empty()) {
			input = &FindStorageImage(reflection, chain.Input, name);
			if (input->mBind == output.mBind) {
				throw std::runtime_error(name + ": MipChain Input and Output need distinct bindings");
			}
			state->MipHasInput = true;
			state->MipInputBinding = input->mBind;
		}
		// The scoped dispatch only publishes the output mip. Other shader resources
		// keep the generic dispatch path, which synchronizes all writes.
		state->MipScopedBarriers = output.mDescriptorCount == 1 && !output.mRuntimeArray
			&& (!input || (input->mReadOnly && input->mDescriptorCount == 1 && !input->mRuntimeArray))
			&& std::ranges::all_of(reflection.mUniforms, [&](const auto& uniform) {
				   return &uniform == &output || &uniform == input || uniform.mType == ShaderReflection::UniformType::UNIFORM_BUFFER;
			   });
		if (!chain.Uniform.empty()) {
			if (!reflection.mNameToUniforms.contains(chain.Uniform)
				|| reflection.mUniforms.at(reflection.mNameToUniforms.at(chain.Uniform)).mType != ShaderReflection::UniformType::UNIFORM_BUFFER) {
				throw std::runtime_error(name + ": MipChain Uniform must name a uniform block: " + chain.Uniform);
			}
			state->MipUniform = chain.Uniform;
		}
		state->FirstMip = chain.FirstMip;
		state->MipCount = chain.MipCount;
	}
	else if (state->UsesProgram) {
		state->Mode = ComputeDispatchMode::LAUNCH;
	}
	else {
		throw std::runtime_error("Material COMPUTE stage needs DispatchTexture, Groups, DispatchBuffer or MipChain: " + name);
	}

	for (const auto& clear : compute.ClearBuffers) {
		const auto bytes = StorageBufferBytes(pipeline, clear.Buffer, name);
		const size_t size = clear.Size ? clear.Size : (clear.Offset < bytes ? bytes - clear.Offset : 0);
		if (clear.Offset % 4 || size % 4 || !size || clear.Offset + size > bytes) {
			throw std::runtime_error(name + ": invalid ClearBuffers range for " + clear.Buffer);
		}
		state->Clears.push_back({ clear.Buffer, clear.Value, clear.Offset, size });
	}

	// Outputs: textures need explicit Bypass aliases under the BYPASS policy; buffers keep their contents.
	std::vector<std::string> textureOutputs;
	for (const auto& output : desc.Writes) {
		if (FindPipelineTexture(pipeline, output)) {
			textureOutputs.push_back(output);
		}
		else if (!pipeline.mStorageBuffers.contains(output)) {
			throw std::runtime_error(name + ": Writes entry is neither a texture nor a storage buffer: " + output);
		}
	}
	if (mipChain && std::find(textureOutputs.begin(), textureOutputs.end(), state->MipTexture) == textureOutputs.end()) {
		textureOutputs.push_back(state->MipTexture);
	}
	for (const auto& [output, input] : desc.Bypass) {
		if (!FindPipelineTexture(pipeline, output) || !FindPipelineTexture(pipeline, input)) {
			throw std::runtime_error(name + ": Bypass entries must alias pipeline textures: " + output);
		}
	}

	stage.mCompute = state;
	if (!desc.Enabled) {
		// Like RT stages, capabilities are resolved only for stages enabled at load.
		return;
	}
	auto& renderer = RESOURCES::ServiceManager::Get<Renderer>();
	auto& driver = *renderer.getDriver();
	const auto features = driver.gpuFeatures();
	try {
		if (state->UsesProgram && !SupportsReflectedComputePrograms(driver)) {
			throw UnsupportedGpuFeature("reflected compute programs are unavailable on this backend");
		}
		for (const auto feature : compute.Requires) {
			if (!HasPipelineGpuFeature(features, feature)) {
				throw UnsupportedGpuFeature(
					std::string("required GPU feature ") + PipelineGpuFeatureName(feature) + " is unavailable on this backend/GPU");
			}
		}
		if (indirect) {
			if (!features.DispatchIndirect) {
				throw UnsupportedGpuFeature("indirect dispatch is unavailable on this backend/GPU");
			}
			if (!HasBufferUsage(pipeline.mBufferUsages.at(compute.DispatchBuffer), BufferUsage::INDIRECT)) {
				throw UnsupportedGpuFeature("buffer " + compute.DispatchBuffer + " could not be created with INDIRECT usage");
			}
		}
		if (!state->Clears.empty() && !features.BufferTransfer) {
			throw UnsupportedGpuFeature("buffer fill (ClearBuffers) is unavailable on this backend/GPU");
		}
		if (mipChain && !features.StorageImageMipViews) {
			throw UnsupportedGpuFeature("per-mip storage images (MipChain) are unavailable on this backend/GPU");
		}
		if (state->UsesProgram) {
			const auto resource = RESOURCES::ShaderLoader::LoadProgramResource(desc.Program);
			const bool asyncImages = pipeline.mDescriptor.Graph.Enabled && desc.Queue == GRAPH::Queue::ASYNC_COMPUTE
				&& std::ranges::any_of(desc.Bindings, [&](const auto& binding) { return pipeline.mTextures.contains(binding.second); });
			if (groups || asyncImages) {
				const auto reflection = ReflectComputeProgram(resource, desc.Program);
				state->LocalSize = { reflection.mLocalSize[0], reflection.mLocalSize[1], reflection.mLocalSize[2] };
				if (asyncImages) {
					if (!resource.Shaders.front().BackendPaths.empty() || resource.Shaders.front().EntryPoint != "main"
						|| resource.RequiresDescriptorIndexing) {
						throw std::runtime_error(name + ": async image programs require a common GLSL entry point and fixed bindings");
					}
					ResolveAsyncImageProgramBindings(pipeline, desc, reflection, *state);
				}
			}
			stage.mRayTracingProgram = driver.createComputeProgram(resource);
			if (!stage.mRayTracingProgram || !stage.mRayTracingProgram->isCompute()) {
				throw std::runtime_error("Program is not a reflected compute program: " + desc.Program);
			}
		}
		else {
			const auto shader = stage.mMaterial->getShader();
			const auto& local = shader->getReflection().mLocalSize;
			state->LocalSize = { local[0], local[1], local[2] };
			if (state->Mode == ComputeDispatchMode::TEXTURE && state->Clears.empty() && desc.Queue == GRAPH::Queue::GRAPHICS
				&& features.StorageImageMipViews) {
				const auto* target = FindPipelineTexture(pipeline, state->DispatchTexture);
				if (target && WritesOnlyDispatchMipZero(desc, *shader, *target)) {
					state->TextureWrite = ScopedTextureDispatch { shader, shader->generation() };
				}
			}
		}
		state->Resolved = true;
	} catch (const UnsupportedGpuFeature& error) {
		if (desc.Unsupported == "ERROR") {
			throw std::runtime_error("COMPUTE stage " + name + ": " + error.what());
		}
		for (const auto& output : textureOutputs) {
			if (!desc.Bypass.contains(output)) {
				throw std::runtime_error("Unsupported COMPUTE stage needs explicit Bypass for output " + output);
			}
		}
		stage.mRayTracingProgram.reset();
		state->Bypassed = true;
		std::cout << "GPU_DRIVEN_FALLBACK " << name << ": " << error.what() << "\n";
	}
}

void GameRenderer::runComputeStage(PipelineStage& stage, bool async) {
	auto& state = *stage.mCompute;
	const auto& desc = stage.mDescriptor;
	if (state.Bypassed || !state.Resolved) {
		if (!state.Bypassed && desc.Unsupported != "BYPASS") {
			throw std::runtime_error("COMPUTE stage was disabled at pipeline load; reload the pipeline to enable it: " + stage.mName);
		}
		for (const auto& [output, input] : desc.Bypass) {
			mRenderPipeline->mTextures.at(output) = mRenderPipeline->mTextures.at(input);
		}
		return;
	}
	auto& renderer = *mContext.render;
	auto* driver = renderer.getDriver();
	if (!state.Clears.empty()) {
		std::vector<BufferFill> fills;
		fills.reserve(state.Clears.size());
		for (const auto& clear : state.Clears) {
			fills.push_back({ mRenderPipeline->mStorageBuffers.at(clear.Buffer), clear.Offset, clear.Size, clear.Value });
		}
		renderer.execute([driver, fills = std::move(fills)] { driver->fillBuffers(fills); });
	}
	auto textureExtent = [&](const std::string& name) {
		const auto& texture = mRenderPipeline->mTextures.at(name);
		return std::array<size_t, 3> { texture->getWidth(), texture->getHeight(), std::max<size_t>(1, texture->getDepth()) };
	};
	bool scopedTextureWrite = false;
	if (state.UsesProgram) {
		auto dispatch = buildProgramDispatch(stage);
		if (state.Mode == ComputeDispatchMode::GROUPS) {
			for (size_t i = 0; i < 3; ++i) {
				dispatch.Extent[i] = state.Groups[i] * state.LocalSize[i];
			}
		}
		else if (state.Mode == ComputeDispatchMode::TEXTURE) {
			const auto extent = textureExtent(state.DispatchTexture);
			dispatch.Extent = { static_cast<uint32_t>(extent[0]), static_cast<uint32_t>(extent[1]), static_cast<uint32_t>(extent[2]) };
		}
		renderer.execute([driver, dispatch = std::move(dispatch), async]() {
			RenderStatistics::Get().recordDispatch();
			if (async) {
				driver->dispatchGraphCompute(dispatch);
			}
			else {
				driver->dispatchComputeProgram(dispatch);
			}
		});
	}
	else {
		bindStage(stage);
		if (state.Mode == ComputeDispatchMode::TEXTURE) {
			const auto texture = mRenderPipeline->mTextures.at(state.DispatchTexture);
			const auto groupsX = CeilDiv(texture->getWidth(), state.LocalSize[0]);
			const auto groupsY = CeilDiv(texture->getHeight(), state.LocalSize[1]);
			const auto groupsZ = CeilDiv(std::max<size_t>(1, texture->getDepth()), state.LocalSize[2]);
			const auto shader = stage.mMaterial->getShader();
			if (state.TextureWrite && state.TextureWrite->Shader.lock() == shader && state.TextureWrite->Generation == shader->generation()
				&& texture->getType() == TextureType::TEXTURE_2D) {
				scopedTextureWrite = true;
				renderer.execute([driver, texture, shader, generation = state.TextureWrite->Generation, groupsX, groupsY, groupsZ] {
					RenderStatistics::Get().recordDispatch();
					if (shader->generation() == generation) {
						// The regular storage-image binding selects mip zero, layer zero.
						driver->dispatchMipCompute(groupsX, groupsY, groupsZ, texture, 0, 0);
					}
					else {
						driver->dispatch(groupsX, groupsY, groupsZ);
					}
				});
			}
			else {
				renderer.dispatch(groupsX, groupsY, groupsZ);
			}
		}
		else if (state.Mode == ComputeDispatchMode::GROUPS) {
			renderer.dispatch(state.Groups[0], state.Groups[1], state.Groups[2]);
		}
		else if (state.Mode == ComputeDispatchMode::INDIRECT) {
			auto buffer = mRenderPipeline->mStorageBuffers.at(state.DispatchBuffer);
			const auto offset = state.DispatchOffset;
			renderer.execute([driver, buffer, offset]() {
				RenderStatistics::Get().recordDispatch();
				driver->dispatchIndirect(buffer, offset);
			});
		}
		else if (state.Mode == ComputeDispatchMode::MIP_CHAIN) {
			auto texture = mRenderPipeline->mTextures.at(state.MipTexture);
			const uint32_t mipCount = texture->getMipCount();
			const uint32_t first = std::min(mipCount, state.FirstMip);
			const uint32_t last = state.MipCount ? first + std::min(mipCount - first, state.MipCount) : mipCount;
			const std::array<size_t, 3> extent { texture->getWidth(), texture->getHeight(), mipCount };
			if (!state.MipUniform.empty() && (state.MipUniforms.size() != mipCount || state.MipUniformExtent != extent)) {
				state.MipUniforms.clear();
				for (uint32_t mip = 0; mip < mipCount; ++mip) {
					const std::array<uint32_t, 4> info { mip, static_cast<uint32_t>(std::max<size_t>(1, extent[0] >> mip)),
						static_cast<uint32_t>(std::max<size_t>(1, extent[1] >> mip)), 1u };
					state.MipUniforms.push_back(renderer.createUniformBuffer(info.data(), sizeof(info)));
				}
				state.MipUniformExtent = extent;
			}
			for (uint32_t mip = first; mip < last; ++mip) {
				if (!state.MipUniform.empty()) {
					renderer.setUniformBuffer(state.MipUniform, state.MipUniforms[mip]);
				}
				const auto output = state.MipOutputBinding;
				renderer.execute([driver, texture, output, mip]() { driver->setStorageImage(output, texture, mip, 0); });
				if (state.MipHasInput) {
					const auto input = state.MipInputBinding;
					renderer.execute([driver, texture, input, mip]() { driver->setStorageImage(input, texture, mip - 1, 0); });
				}
				const auto groupsX = CeilDiv(std::max<size_t>(1, extent[0] >> mip), state.LocalSize[0]);
				const auto groupsY = CeilDiv(std::max<size_t>(1, extent[1] >> mip), state.LocalSize[1]);
				if (state.MipScopedBarriers) {
					renderer.execute([driver, texture, mip, groupsX, groupsY] {
						RenderStatistics::Get().recordDispatch();
						driver->dispatchMipCompute(groupsX, groupsY, 1, texture, mip, 0);
					});
				}
				else {
					renderer.dispatch(groupsX, groupsY, 1);
				}
			}
		}
	}
	stage.mExecuted = true;
	if (mRenderPipeline->mDescriptor.SurfaceCache && stage.mName == SurfaceCacheRelightStageName) {
		std::cout << "GI5_SURFACE_RELIGHT frame=" << mFrameCount << " cards=" << mRenderPipeline->mDescriptor.SurfaceCache->Gpu.State[0] << "\n";
	}
	if (!state.Logged) {
		if (async && !state.AsyncImageProgramBindings.empty()) {
			std::cout << "GRAPH_ASYNC_DISPATCH pass=" << stage.mName << " images="
					  << std::ranges::count_if(state.AsyncImageProgramBindings, [](const auto& binding) { return binding.Texture != nullptr; })
					  << '\n';
		}
		std::cout << "Pipeline generic compute: " << stage.mName << (state.UsesProgram ? " program" : " material") << " mode=" << int(state.Mode);
		if (!state.UsesProgram && state.Mode == ComputeDispatchMode::TEXTURE) {
			std::cout << " texture_barrier=" << (scopedTextureWrite ? "scoped" : "generic");
		}
		if (state.Mode == ComputeDispatchMode::MIP_CHAIN) {
			std::cout << " mip_barriers=" << (state.MipScopedBarriers ? "scoped" : "generic");
		}
		std::cout << std::endl;
		state.Logged = true;
	}
}
}
