#include "shaderVk.h"

#include <cassert>

#include "utilsModule/pathGetter.h"

#ifdef VULKAN_BACKEND
#include <cstring>
#include <exception>
#include <map>
#include <iomanip>
#include <sstream>

#include "driverVk.h"
#include "uniformBufferVk.h"
#include "textureVk.h"

#include <spirv_reflect.h>
#include <spirv.h>
#include <resourceModule/serviceManager.h>
#include <resourceModule/fileSystem/fileSystem.h>
#include <renderModule/backends/interface/resourceStruct.h>
#include "utilsModule/jsonLoader.h"
#include "utilsModule/log/loggerDefine.h"
#include "../interface/reflectionStructs.h"

namespace {
// Конвертация vector<uint32_t> (SPIR-V) в hex-строку
std::string SpirvToHex(const std::vector<uint32_t> &spirv) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (uint32_t word : spirv) {
    oss << std::setw(8) << word;
  }
  return oss.str();
}

// Конвертация hex-строки обратно в vector<uint32_t>
std::vector<uint32_t> HexToSpirv(const std::string &hex) {
  std::vector<uint32_t> result;
  result.reserve(hex.size() / 8);
  for (size_t i = 0; i + 8 <= hex.size(); i += 8) {
    uint32_t word =
        static_cast<uint32_t>(std::stoul(hex.substr(i, 8), nullptr, 16));
    result.push_back(word);
  }
  return result;
}

struct SpirvCache {
  std::map<std::string, std::string> stages;

  template <class Context>
  constexpr static auto serde(Context &context, SpirvCache &value) {
    using Self = SpirvCache;
    serde::serde_struct(context, value).field(&Self::stages, "Stages");
  }
};
} // namespace

const static std::unordered_map<IKIGAI::RENDER::ShaderReflection::UniformType, vk::DescriptorType> ShaderTypeMap = {
	{IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_2D, vk::DescriptorType::eCombinedImageSampler},
	{IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_2D_ARRAY, vk::DescriptorType::eCombinedImageSampler},
	{IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_3D, vk::DescriptorType::eCombinedImageSampler},
	{IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_CUBE, vk::DescriptorType::eCombinedImageSampler},
	{IKIGAI::RENDER::ShaderReflection::UniformType::UNIFORM_BUFFER, vk::DescriptorType::eUniformBuffer},
	{IKIGAI::RENDER::ShaderReflection::UniformType::STORAGE_BUFFER, vk::DescriptorType::eStorageBuffer},
};

std::tuple<vk::raii::PipelineLayout, vk::raii::DescriptorSetLayout, std::vector<vk::DescriptorSetLayoutBinding>> IKIGAI::RENDER::ShaderVk::createPipelineLayout() {
	std::vector<vk::DescriptorSetLayoutBinding> required_descriptor_bindings;

	for (const auto& uniform : mReflection.mUniforms) {
		vk::ShaderStageFlags stageFlag{};
		
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
			LOG_ERROR << "Unsupported uniform type for descriptor '" << uniform.mName << "'";
			continue;
		}

		auto descriptor_set_layout_binding = vk::DescriptorSetLayoutBinding()
			.setDescriptorType(typeIt->second)
			.setDescriptorCount(1)
			.setBinding(uniform.mBind)
			.setStageFlags(stageFlag);

		required_descriptor_bindings.push_back(descriptor_set_layout_binding);
	}

	// Not a push-descriptor layout. MoltenVK writes spvBufferSizeConstants only
	// inside vkCmdBindDescriptorSets; vkCmdPushDescriptorSet leaves that Metal
	// buffer unbound and validation aborts in vkQueueSubmit.
	auto descriptor_set_layout_create_info = vk::DescriptorSetLayoutCreateInfo()
		.setBindings(required_descriptor_bindings);

	auto descriptor_set_layout = UtilityVk::GetDriver()->mDevice.createDescriptorSetLayout(descriptor_set_layout_create_info);

	std::vector<vk::PushConstantRange> push_constant_ranges;
	for (const auto& uniform : mReflection.mUniforms) {
		if (uniform.mType == IKIGAI::RENDER::ShaderReflection::UniformType::PUSH_CONSTANT) {
			vk::ShaderStageFlags stageFlag{};
			if (uniform.mShaderMask & (size_t)ShaderType::VERTEX) stageFlag |= vk::ShaderStageFlagBits::eVertex;
			if (uniform.mShaderMask & (size_t)ShaderType::FRAGMENT) stageFlag |= vk::ShaderStageFlagBits::eFragment;
			if (uniform.mShaderMask & (size_t)ShaderType::GEOMETRY) stageFlag |= vk::ShaderStageFlagBits::eGeometry;
			if (uniform.mShaderMask & (size_t)ShaderType::TESSELLATION_CONTROL) stageFlag |= vk::ShaderStageFlagBits::eTessellationControl;
			if (uniform.mShaderMask & (size_t)ShaderType::TESSELLATION_EVALUATION) stageFlag |= vk::ShaderStageFlagBits::eTessellationEvaluation;
			if (uniform.mShaderMask & (size_t)ShaderType::COMPUTE) stageFlag |= vk::ShaderStageFlagBits::eCompute;
			
			push_constant_ranges.push_back(vk::PushConstantRange().setStageFlags(stageFlag).setOffset(uniform.mBind).setSize(uniform.mSize));
		}
	}

	auto pipeline_layout_create_info = vk::PipelineLayoutCreateInfo()
		.setSetLayouts(*descriptor_set_layout)
		.setPushConstantRanges(push_constant_ranges);

	auto pipeline_layout = UtilityVk::GetDriver()->mDevice.createPipelineLayout(pipeline_layout_create_info);

	return {std::move(pipeline_layout), std::move(descriptor_set_layout), required_descriptor_bindings};
}

std::shared_ptr<IKIGAI::RENDER::ShaderVk> IKIGAI::RENDER::ShaderVk::CreateFromSource(const std::map<ShaderType, std::string>& source) {
	ShaderResource res;
	res.sources = source;
	return std::make_shared<ShaderVk>(res);
}

std::shared_ptr<IKIGAI::RENDER::ShaderVk> IKIGAI::RENDER::ShaderVk::CreateFromPath(const std::map<ShaderType, std::string>& paths) {
	ShaderResource res;
	res.setPaths(paths);
	return Create(res);
}

std::shared_ptr<IKIGAI::RENDER::ShaderVk> IKIGAI::RENDER::ShaderVk::Create(const ShaderResource& resource) {
	const auto useBinary = resource.useBinary;
	bool needSaveBinarySource = false;
	auto& fs = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::FileSystem>();

	// Try load spirv bin
	if (useBinary) {
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
						resource.spirvSources = std::move(loaded);
						return std::make_shared<ShaderVk>(resource);
					} catch (const std::exception& e) {
						LOG_ERROR << "Corrupted SPIR-V cache " << cacheJsonPath << ": " << e.what();
						resource.spirvSources.clear();
					}
				} else {
					LOG_INFO << "Failed to parse SPIR-V cache: " << cacheJsonPath;
				}
			}
		} else {
			needSaveBinarySource = true;
		}
	}

	// Load sources
	for (const auto& [type, path] : resource.paths) {
		if (!path.empty()) {
			auto file = fs.getFile(path);
			if (file && file->isOpened()) {
				resource.sources[ShaderResource::toEnum[type]] = file->readStr();
			} else {
				LOG_ERROR << "Failed to load: " << path;
			}
		}
	}

	auto shader = std::make_shared<ShaderVk>(resource);

	// Save spirv bin
	if (needSaveBinarySource && !shader->mPath.empty() && !resource.spirvSources.empty()) {
		const auto cacheJsonPath = shader->mPath + ".spirv.json";
		if (!fs.isFileExist(cacheJsonPath)) {
			SpirvCache cache;
			for (const auto& [type, spirv] : resource.spirvSources) {
				cache.stages[std::to_string(static_cast<int>(type))] = SpirvToHex(spirv);
			}
			if (auto resStr = IKIGAI::UTILS::ToJsonStr(cache, 2); !resStr.isErr()) {
				if (auto file = fs.getFile(cacheJsonPath, IKIGAI::RESOURCES::FileMode::WRITE); file) {
					file->write(resStr.unwrap());
				}
			} else {
				LOG_ERROR << "Failed to serialize SPIR-V cache: " << cacheJsonPath;
			}
		}
	}

	return shader;
}

IKIGAI::RENDER::ShaderVk::ShaderVk(const ShaderResource& res) {
	create(res);
}

IKIGAI::RENDER::ShaderVk::~ShaderVk() {
	clear();
}

void IKIGAI::RENDER::ShaderVk::recompile(const ShaderResource& res) {
	clear();
	// New sources must not be shadowed by SPIR-V compiled from the previous version
	if (!res.sources.empty()) {
		res.spirvSources.clear();
	}
	create(res);
}

void IKIGAI::RENDER::ShaderVk::clear() const {
}

void IKIGAI::RENDER::ShaderVk::create(const ShaderResource& res) {
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
	} else {
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
			} else {
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
	}
	if (res.spirvSources.contains(ShaderType::FRAGMENT) && !res.spirvSources[ShaderType::FRAGMENT].empty()) {
		auto create_info = vk::ShaderModuleCreateInfo();
		create_info.codeSize = res.spirvSources[ShaderType::FRAGMENT].size() * sizeof(uint32_t);
		create_info.pCode = res.spirvSources[ShaderType::FRAGMENT].data();
		mFragmentShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(create_info);
	}
	if (res.spirvSources.contains(ShaderType::GEOMETRY) && !res.spirvSources[ShaderType::GEOMETRY].empty()) {
		auto create_info = vk::ShaderModuleCreateInfo();
		create_info.codeSize = res.spirvSources[ShaderType::GEOMETRY].size() * sizeof(uint32_t);
		create_info.pCode = res.spirvSources[ShaderType::GEOMETRY].data();
		mGeometryShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(create_info);
	}
	if (res.spirvSources.contains(ShaderType::TESSELLATION_CONTROL) && !res.spirvSources[ShaderType::TESSELLATION_CONTROL].empty()) {
		auto create_info = vk::ShaderModuleCreateInfo();
		create_info.codeSize = res.spirvSources[ShaderType::TESSELLATION_CONTROL].size() * sizeof(uint32_t);
		create_info.pCode = res.spirvSources[ShaderType::TESSELLATION_CONTROL].data();
		mTessellationControlShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(create_info);
	}
	if (res.spirvSources.contains(ShaderType::TESSELLATION_EVALUATION) && !res.spirvSources[ShaderType::TESSELLATION_EVALUATION].empty()) {
		auto create_info = vk::ShaderModuleCreateInfo();
		create_info.codeSize = res.spirvSources[ShaderType::TESSELLATION_EVALUATION].size() * sizeof(uint32_t);
		create_info.pCode = res.spirvSources[ShaderType::TESSELLATION_EVALUATION].data();
		mTessellationEvaluationShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(create_info);
	}
	if (res.spirvSources.contains(ShaderType::COMPUTE) && !res.spirvSources[ShaderType::COMPUTE].empty()) {
		auto create_info = vk::ShaderModuleCreateInfo();
		create_info.codeSize = res.spirvSources[ShaderType::COMPUTE].size() * sizeof(uint32_t);
		create_info.pCode = res.spirvSources[ShaderType::COMPUTE].data();
		mComputeShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(create_info);
	}
}
#endif
