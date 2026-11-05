#include "shaderVk.h"
#include "utilsModule/memory/resourceAllocator.h"
#include "utilsModule/time/loadTimer.h"

#include <cassert>

#include "utilsModule/pathGetter.h"

#ifdef VULKAN_BACKEND
#include <cstring>
#include <exception>
#include <map>

#include "driverVk.h"
#include "textureVk.h"
#include "uniformBufferVk.h"

#include "../interface/reflectionStructs.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/log/loggerDefine.h"
#include <renderModule/backends/interface/resourceStruct.h>
#include <resourceModule/fileSystem/fileSystem.h>
#include <resourceModule/serviceManager.h>
#include <resourceModule/shaderManager.h>
#include <spirv.h>
#include <spirv_reflect.h>

namespace {
// Конвертация hex-строки обратно в vector<uint32_t>
std::vector<uint32_t> HexToSpirv(const std::string& hex) {
	std::vector<uint32_t> result;
	result.reserve(hex.size() / 8);
	for (size_t i = 0; i + 8 <= hex.size(); i += 8) {
		uint32_t word = static_cast<uint32_t>(std::stoul(hex.substr(i, 8), nullptr, 16));
		result.push_back(word);
	}
	return result;
}

struct SpirvCache {
	std::map<std::string, std::string> stages;

	template <class Context> constexpr static auto serde(Context& context, SpirvCache& value) {
		using Self = SpirvCache;
		serde::serde_struct(context, value).field(&Self::stages, "Stages");
	}
};
} // namespace

const static std::unordered_map<IKIGAI::RENDER::ShaderReflection::UniformType, vk::DescriptorType> ShaderTypeMap = {
	{ IKIGAI::RENDER::ShaderReflection::UniformType::IMAGE_2D, vk::DescriptorType::eStorageImage },
	{ IKIGAI::RENDER::ShaderReflection::UniformType::IMAGE_3D, vk::DescriptorType::eStorageImage },
	{ IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_2D, vk::DescriptorType::eCombinedImageSampler },
	{ IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_2D_ARRAY, vk::DescriptorType::eCombinedImageSampler },
	{ IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_3D, vk::DescriptorType::eCombinedImageSampler },
	{ IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_CUBE, vk::DescriptorType::eCombinedImageSampler },
	{ IKIGAI::RENDER::ShaderReflection::UniformType::UNIFORM_BUFFER, vk::DescriptorType::eUniformBuffer },
	{ IKIGAI::RENDER::ShaderReflection::UniformType::STORAGE_BUFFER, vk::DescriptorType::eStorageBuffer },
};

std::tuple<vk::raii::PipelineLayout, vk::raii::DescriptorSetLayout, std::vector<vk::DescriptorSetLayoutBinding>>
IKIGAI::RENDER::ShaderVk::createPipelineLayout() {
	std::vector<vk::DescriptorSetLayoutBinding> required_descriptor_bindings;

	for (const auto& uniform : mReflection.mUniforms) {
		vk::ShaderStageFlags stageFlag {};

		if (uniform.mShaderMask & (size_t)ShaderType::VERTEX) {
			stageFlag |= vk::ShaderStageFlagBits::eVertex;
		}
		if (uniform.mShaderMask & (size_t)ShaderType::FRAGMENT) {
			stageFlag |= vk::ShaderStageFlagBits::eFragment;
		}
		if (uniform.mShaderMask & (size_t)ShaderType::GEOMETRY) {
			stageFlag |= vk::ShaderStageFlagBits::eGeometry;
		}
		if (uniform.mShaderMask & (size_t)ShaderType::TESSELLATION_CONTROL) {
			stageFlag |= vk::ShaderStageFlagBits::eTessellationControl;
		}
		if (uniform.mShaderMask & (size_t)ShaderType::TESSELLATION_EVALUATION) {
			stageFlag |= vk::ShaderStageFlagBits::eTessellationEvaluation;
		}
		if (uniform.mShaderMask & (size_t)ShaderType::COMPUTE) {
			stageFlag |= vk::ShaderStageFlagBits::eCompute;
		}

		if (uniform.mType == IKIGAI::RENDER::ShaderReflection::UniformType::PUSH_CONSTANT) {
			continue;
		}

		const auto typeIt = ShaderTypeMap.find(uniform.mType);
		if (typeIt == ShaderTypeMap.end()) {
			if (bindingMode() == ShaderBindingMode::Bindless) {
				throw std::runtime_error("Unsupported bindless/local descriptor type: " + uniform.mName);
			}
			LOG_ERROR << "Unsupported uniform type for descriptor '" << uniform.mName << "'";
			continue;
		}

		const auto count = descriptorCount(uniform);
		if (isBindlessResource(uniform)) {
			const auto caps = UtilityVk::GetDriver()->bindlessCapabilities();
			if (uniform.mSet != 0) {
				throw std::runtime_error("Bindless resource ABI currently uses descriptor set zero");
			}
			if (uniform.mRuntimeArray && !caps.runtimeArrays) {
				throw std::runtime_error("Runtime descriptor arrays unsupported");
			}
			const bool texture = typeIt->second == vk::DescriptorType::eCombinedImageSampler;
			const bool buffer = typeIt->second == vk::DescriptorType::eStorageBuffer;
			if ((!texture && !buffer) || (texture && (!caps.sampledTextures || count > caps.maxSampledTextures))
				|| (buffer && (!caps.storageBuffers || count > caps.maxStorageBuffers))) {
				throw std::runtime_error("Bindless table class/count unsupported: " + uniform.mName);
			}
		}
		else if (count != 1) {
			throw std::runtime_error("Descriptor arrays require BindingMode Bindless");
		}
		auto descriptor_set_layout_binding = vk::DescriptorSetLayoutBinding()
												 .setDescriptorType(typeIt->second)
												 .setDescriptorCount(count)
												 .setBinding(uniform.mBind)
												 .setStageFlags(stageFlag);

		required_descriptor_bindings.push_back(descriptor_set_layout_binding);
	}

	// Not a push-descriptor layout. MoltenVK writes spvBufferSizeConstants only
	// inside vkCmdBindDescriptorSets; vkCmdPushDescriptorSet leaves that Metal
	// buffer unbound and validation aborts in vkQueueSubmit.
	auto descriptor_set_layout_create_info = vk::DescriptorSetLayoutCreateInfo().setBindings(required_descriptor_bindings);

	auto descriptor_set_layout = UtilityVk::GetDriver()->mDevice.createDescriptorSetLayout(descriptor_set_layout_create_info);

	std::vector<vk::PushConstantRange> push_constant_ranges;
	for (const auto& uniform : mReflection.mUniforms) {
		if (uniform.mType == IKIGAI::RENDER::ShaderReflection::UniformType::PUSH_CONSTANT) {
			vk::ShaderStageFlags stageFlag {};
			if (uniform.mShaderMask & (size_t)ShaderType::VERTEX) {
				stageFlag |= vk::ShaderStageFlagBits::eVertex;
			}
			if (uniform.mShaderMask & (size_t)ShaderType::FRAGMENT) {
				stageFlag |= vk::ShaderStageFlagBits::eFragment;
			}
			if (uniform.mShaderMask & (size_t)ShaderType::GEOMETRY) {
				stageFlag |= vk::ShaderStageFlagBits::eGeometry;
			}
			if (uniform.mShaderMask & (size_t)ShaderType::TESSELLATION_CONTROL) {
				stageFlag |= vk::ShaderStageFlagBits::eTessellationControl;
			}
			if (uniform.mShaderMask & (size_t)ShaderType::TESSELLATION_EVALUATION) {
				stageFlag |= vk::ShaderStageFlagBits::eTessellationEvaluation;
			}
			if (uniform.mShaderMask & (size_t)ShaderType::COMPUTE) {
				stageFlag |= vk::ShaderStageFlagBits::eCompute;
			}

			push_constant_ranges.push_back(vk::PushConstantRange().setStageFlags(stageFlag).setOffset(uniform.mBind).setSize(uniform.mSize));
		}
	}

	auto pipeline_layout_create_info
		= vk::PipelineLayoutCreateInfo().setSetLayouts(*descriptor_set_layout).setPushConstantRanges(push_constant_ranges);

	auto pipeline_layout = UtilityVk::GetDriver()->mDevice.createPipelineLayout(pipeline_layout_create_info);

	return { std::move(pipeline_layout), std::move(descriptor_set_layout), required_descriptor_bindings };
}

std::shared_ptr<IKIGAI::RENDER::ShaderVk> IKIGAI::RENDER::ShaderVk::CreateFromSource(const std::map<ShaderType, std::string>& source) {
	ShaderResource res;
	res.sources = source;
	return IKIGAI::MEMORY::MakeShared<ShaderVk, IKIGAI::MEMORY::Category::Shader>(res);
}

std::shared_ptr<IKIGAI::RENDER::ShaderVk> IKIGAI::RENDER::ShaderVk::CreateFromPath(const std::map<ShaderType, std::string>& paths) {
	ShaderResource res;
	res.setPaths(paths);
	return Create(res);
}

std::shared_ptr<IKIGAI::RENDER::ShaderVk> IKIGAI::RENDER::ShaderVk::Create(const ShaderResource& resource) {
	auto prepared = resource;
	auto dependencies = IKIGAI::RESOURCES::ShaderLoader::ResolveSources(prepared);
	auto& fs = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::FileSystem>();

	// Preserve source-less packaged SPIR-V. Source-backed stages use the content-keyed compiler cache.
	if (prepared.useBinary && prepared.sources.empty() && prepared.spirvSources.empty()) {
		const auto cacheJsonPath = resource.path + ".spirv.json";
		if (fs.isFileExist(cacheJsonPath)) {
			if (auto file = fs.getFile(cacheJsonPath); file && file->isOpened()) {
				const auto jsonStr = file->readStr();
				if (auto resJson = IKIGAI::UTILS::FromJsonStr<SpirvCache>(jsonStr); !resJson.isErr()) {
					const auto& cache = resJson.unwrap();
					try {
						std::map<ShaderType, std::vector<uint32_t>> loaded;
						for (const auto& [key, hexStr] : cache.stages) {
							loaded[static_cast<ShaderType>(std::stoi(key))] = HexToSpirv(hexStr);
						}
						prepared.spirvSources = std::move(loaded);
					} catch (const std::exception& e) {
						LOG_ERROR << "Corrupted SPIR-V cache " << cacheJsonPath << ": " << e.what();
						prepared.spirvSources.clear();
					}
				}
				else {
					LOG_INFO << "Failed to parse SPIR-V cache: " << cacheJsonPath;
				}
			}
		}
	}
	auto shader = IKIGAI::MEMORY::MakeShared<ShaderVk, IKIGAI::MEMORY::Category::Shader>(prepared);
	shader->mSourceDependencies = std::move(dependencies);

	return shader;
}

IKIGAI::RENDER::ShaderVk::ShaderVk(const ShaderResource& res) {
	create(res);
}

IKIGAI::RENDER::ShaderVk::~ShaderVk() {
	clear();
}

void IKIGAI::RENDER::ShaderVk::recompile(const ShaderResource& res) {
	requireOwnerThread();
	auto* driver = UtilityVk::GetDriver();
	auto replacement = std::make_shared<ShaderVk>(res);
	using std::swap;
	swapCompiledInterface(*replacement);
	swap(mDescriptorSetLayout, replacement->mDescriptorSetLayout);
	swap(mPipelineLayout, replacement->mPipelineLayout);
	swap(mVertexShaderModule, replacement->mVertexShaderModule);
	swap(mFragmentShaderModule, replacement->mFragmentShaderModule);
	swap(mGeometryShaderModule, replacement->mGeometryShaderModule);
	swap(mTessellationControlShaderModule, replacement->mTessellationControlShaderModule);
	swap(mTessellationEvaluationShaderModule, replacement->mTessellationEvaluationShaderModule);
	swap(mComputeShaderModule, replacement->mComputeShaderModule);
	swap(mRequiredDescriptorBindings, replacement->mRequiredDescriptorBindings);
#if IKIGAI_ENABLE_MEMORY_TRACKING
	swap(mGpuModules, replacement->mGpuModules);
#endif
	driver->destroyDeferred(std::shared_ptr<void>(std::move(replacement)));
	driver->retireShaderPipelines(mId);
}

void IKIGAI::RENDER::ShaderVk::clear() const { }

void IKIGAI::RENDER::ShaderVk::create(const ShaderResource& res) {
	UTILS::LoadTimer timing("shader", res.path);
	configureResourceInterface(res);
	if (!res.sources.empty()) {
		res.spirvSources.clear();
	}
	static size_t ID = 0;
	mId = ID++;
	mPath = res.path;
	mShaderPaths = res.getPaths();

	// create() is also used by recompile(), reflection must not accumulate
	mReflection = ShaderReflection();

	std::vector<std::string> defines;

	// SPIRV compilation
	if (!res.spirvSources.empty()) {
		for (auto& [type, spirv] : res.spirvSources) {
			GetReflection(mReflection, spirv, type);
		}
	}
	else {
		for (auto& [type, source] : res.sources) {
			bool isSpirv = false;
			if (source.size() >= 4) {
				uint32_t magic = *reinterpret_cast<const uint32_t*>(source.data());
				if (magic == 0x07230203) {
					isSpirv = true;
				}
			}

			if (isSpirv) {
				std::vector<uint32_t> spirv(source.size() / 4);
				std::memcpy(spirv.data(), source.data(), source.size());
				res.spirvSources[type] = spirv;
				GetReflection(mReflection, res.spirvSources[type], type);
			}
			else {
				res.spirvSources[type] = CompileGlslToSpirv(type, source, defines);
				if (!res.spirvSources[type].empty()) {
					GetReflection(mReflection, res.spirvSources[type], type);
				}
			}
		}
	}

	std::tie(mPipelineLayout, mDescriptorSetLayout, mRequiredDescriptorBindings) = createPipelineLayout();

	if (res.spirvSources.contains(ShaderType::VERTEX) && !res.spirvSources[ShaderType::VERTEX].empty()) {
		auto create_info = vk::ShaderModuleCreateInfo();
		create_info.codeSize = res.spirvSources[ShaderType::VERTEX].size() * sizeof(uint32_t);
		create_info.pCode = res.spirvSources[ShaderType::VERTEX].data();
		mVertexShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(create_info);
#if IKIGAI_ENABLE_MEMORY_TRACKING
		mGpuModules[0] = PROFILER::TrackGpuObject(PROFILER::GpuBackend::Vulkan,
			reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(*UtilityVk::GetDriver()->mDevice)),
			PROFILER::GpuHandleValue(static_cast<VkShaderModule>(*mVertexShaderModule)), PROFILER::GpuResourceKind::Shader, mPath.c_str());
#endif
	}
	if (res.spirvSources.contains(ShaderType::FRAGMENT) && !res.spirvSources[ShaderType::FRAGMENT].empty()) {
		auto create_info = vk::ShaderModuleCreateInfo();
		create_info.codeSize = res.spirvSources[ShaderType::FRAGMENT].size() * sizeof(uint32_t);
		create_info.pCode = res.spirvSources[ShaderType::FRAGMENT].data();
		mFragmentShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(create_info);
#if IKIGAI_ENABLE_MEMORY_TRACKING
		mGpuModules[1] = PROFILER::TrackGpuObject(PROFILER::GpuBackend::Vulkan,
			reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(*UtilityVk::GetDriver()->mDevice)),
			PROFILER::GpuHandleValue(static_cast<VkShaderModule>(*mFragmentShaderModule)), PROFILER::GpuResourceKind::Shader, mPath.c_str());
#endif
	}
	if (res.spirvSources.contains(ShaderType::GEOMETRY) && !res.spirvSources[ShaderType::GEOMETRY].empty()) {
		auto create_info = vk::ShaderModuleCreateInfo();
		create_info.codeSize = res.spirvSources[ShaderType::GEOMETRY].size() * sizeof(uint32_t);
		create_info.pCode = res.spirvSources[ShaderType::GEOMETRY].data();
		mGeometryShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(create_info);
#if IKIGAI_ENABLE_MEMORY_TRACKING
		mGpuModules[2] = PROFILER::TrackGpuObject(PROFILER::GpuBackend::Vulkan,
			reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(*UtilityVk::GetDriver()->mDevice)),
			PROFILER::GpuHandleValue(static_cast<VkShaderModule>(*mGeometryShaderModule)), PROFILER::GpuResourceKind::Shader, mPath.c_str());
#endif
	}
	if (res.spirvSources.contains(ShaderType::TESSELLATION_CONTROL) && !res.spirvSources[ShaderType::TESSELLATION_CONTROL].empty()) {
		auto create_info = vk::ShaderModuleCreateInfo();
		create_info.codeSize = res.spirvSources[ShaderType::TESSELLATION_CONTROL].size() * sizeof(uint32_t);
		create_info.pCode = res.spirvSources[ShaderType::TESSELLATION_CONTROL].data();
		mTessellationControlShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(create_info);
#if IKIGAI_ENABLE_MEMORY_TRACKING
		mGpuModules[3] = PROFILER::TrackGpuObject(PROFILER::GpuBackend::Vulkan,
			reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(*UtilityVk::GetDriver()->mDevice)),
			PROFILER::GpuHandleValue(static_cast<VkShaderModule>(*mTessellationControlShaderModule)), PROFILER::GpuResourceKind::Shader,
			mPath.c_str());
#endif
	}
	if (res.spirvSources.contains(ShaderType::TESSELLATION_EVALUATION) && !res.spirvSources[ShaderType::TESSELLATION_EVALUATION].empty()) {
		auto create_info = vk::ShaderModuleCreateInfo();
		create_info.codeSize = res.spirvSources[ShaderType::TESSELLATION_EVALUATION].size() * sizeof(uint32_t);
		create_info.pCode = res.spirvSources[ShaderType::TESSELLATION_EVALUATION].data();
		mTessellationEvaluationShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(create_info);
#if IKIGAI_ENABLE_MEMORY_TRACKING
		mGpuModules[4] = PROFILER::TrackGpuObject(PROFILER::GpuBackend::Vulkan,
			reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(*UtilityVk::GetDriver()->mDevice)),
			PROFILER::GpuHandleValue(static_cast<VkShaderModule>(*mTessellationEvaluationShaderModule)), PROFILER::GpuResourceKind::Shader,
			mPath.c_str());
#endif
	}
	if (res.spirvSources.contains(ShaderType::COMPUTE) && !res.spirvSources[ShaderType::COMPUTE].empty()) {
		auto create_info = vk::ShaderModuleCreateInfo();
		create_info.codeSize = res.spirvSources[ShaderType::COMPUTE].size() * sizeof(uint32_t);
		create_info.pCode = res.spirvSources[ShaderType::COMPUTE].data();
		mComputeShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(create_info);
#if IKIGAI_ENABLE_MEMORY_TRACKING
		mGpuModules[5] = PROFILER::TrackGpuObject(PROFILER::GpuBackend::Vulkan,
			reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(*UtilityVk::GetDriver()->mDevice)),
			PROFILER::GpuHandleValue(static_cast<VkShaderModule>(*mComputeShaderModule)), PROFILER::GpuResourceKind::Shader, mPath.c_str());
#endif
	}
}
#endif
