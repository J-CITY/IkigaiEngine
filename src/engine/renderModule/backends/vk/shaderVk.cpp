#include "shaderVk.h"

#include <cassert>

#include "utilsModule/pathGetter.h"

#ifdef VULKAN_BACKEND
#include <fstream>
#include <stdexcept>

#include "driverVk.h"
#include "uniformBufferVk.h"
#include "textureVk.h"

#include <spirv_reflect.h>
#include <spirv.h>
#include <resourceModule/serviceManager.h>
#include "../interface/reflectionStructs.h"

static std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	//std::ifstream file(IKIGAI::UTILS::GetRealPath(filename), std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();

	return buffer;
}

const static std::unordered_map<IKIGAI::RENDER::ShaderReflection::UniformType, vk::DescriptorType> ShaderTypeMap = {
	{IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_2D, vk::DescriptorType::eCombinedImageSampler},
	{IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_2D_ARRAY, vk::DescriptorType::eCombinedImageSampler},
	{IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_3D, vk::DescriptorType::eCombinedImageSampler},
	{IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_CUBE, vk::DescriptorType::eCombinedImageSampler},
	{IKIGAI::RENDER::ShaderReflection::UniformType::UNIFORM_BUFFER, vk::DescriptorType::eUniformBuffer},
	{IKIGAI::RENDER::ShaderReflection::UniformType::STORAGE_BUFFER, vk::DescriptorType::eStorageImage},
};

std::tuple<vk::raii::PipelineLayout, vk::raii::DescriptorSetLayout, std::vector<vk::DescriptorSetLayoutBinding>> IKIGAI::RENDER::ShaderVk::createPipelineLayout() {
	std::vector<vk::DescriptorSetLayoutBinding> required_descriptor_bindings;

	for (const auto& uniform : mReflection.mUniforms) {
		vk::ShaderStageFlags stageFlag{};

		//TODO add other shader
		if (uniform.mShaderMask & (size_t)ShaderType::VERTEX) {
			stageFlag |= vk::ShaderStageFlagBits::eVertex;
		}
		if (uniform.mShaderMask & (size_t)ShaderType::FRAGMENT) {
			stageFlag |= vk::ShaderStageFlagBits::eFragment;
		}

		if (uniform.mType == IKIGAI::RENDER::ShaderReflection::UniformType::PUSH_CONSTANT) {
			continue;
		}

		auto descriptor_set_layout_binding = vk::DescriptorSetLayoutBinding()
			.setDescriptorType(ShaderTypeMap.at(uniform.mType))
			.setDescriptorCount(1)
			.setBinding(uniform.mBind)
			.setStageFlags(stageFlag);

		required_descriptor_bindings.push_back(descriptor_set_layout_binding);
	}

	auto descriptor_set_layout_create_info = vk::DescriptorSetLayoutCreateInfo()
		.setFlags(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR)
		.setBindings(required_descriptor_bindings);

	auto descriptor_set_layout = UtilityVk::GetDriver()->mDevice.createDescriptorSetLayout(descriptor_set_layout_create_info);

	std::vector<vk::PushConstantRange> push_constant_ranges;
	for (const auto& uniform : mReflection.mUniforms) {
		if (uniform.mType == IKIGAI::RENDER::ShaderReflection::UniformType::PUSH_CONSTANT) {
			vk::ShaderStageFlags stageFlag{};
			if (uniform.mShaderMask & (size_t)ShaderType::VERTEX) stageFlag |= vk::ShaderStageFlagBits::eVertex;
			if (uniform.mShaderMask & (size_t)ShaderType::FRAGMENT) stageFlag |= vk::ShaderStageFlagBits::eFragment;
			push_constant_ranges.push_back(vk::PushConstantRange().setStageFlags(stageFlag).setOffset(uniform.mBind).setSize(uniform.mSize));
		}
	}

	auto pipeline_layout_create_info = vk::PipelineLayoutCreateInfo()
		.setSetLayouts(*descriptor_set_layout)
		.setPushConstantRanges(push_constant_ranges);

	auto pipeline_layout = UtilityVk::GetDriver()->mDevice.createPipelineLayout(pipeline_layout_create_info);

	return {std::move(pipeline_layout), std::move(descriptor_set_layout), required_descriptor_bindings};
}

std::shared_ptr<IKIGAI::RENDER::ShaderVk> IKIGAI::RENDER::ShaderVk::CreateFromPath(std::map<ShaderType, std::string> path) {
	auto shader = std::make_shared<ShaderVk>(path);

	//auto vertexPath = IKIGAI::UTILS::GetRealPath(path[ShaderType::VERTEX]);
	//auto fragmentPath = IKIGAI::UTILS::GetRealPath(path[ShaderType::FRAGMENT]);
	

	
	return shader;
}

IKIGAI::RENDER::ShaderVk::ShaderVk(std::map<ShaderType, std::string> shaderCode) {
	static size_t ID = 0;
	mId = ID;
	++ID;

	_getReflection(shaderCode[ShaderType::VERTEX], ShaderType::VERTEX);
	_getReflection(shaderCode[ShaderType::FRAGMENT], ShaderType::FRAGMENT);

	auto _vertex_shader_spirv = readFile((shaderCode[ShaderType::VERTEX]));
	auto _fragment_shader_spirv = readFile((shaderCode[ShaderType::FRAGMENT]));

	//std::vector<uint32_t> vertex_shader_spirv(_vertex_shader_spirv.begin(), _vertex_shader_spirv.end());
	//std::vector<uint32_t> fragment_shader_spirv(_fragment_shader_spirv.begin(), _fragment_shader_spirv.end());

	std::tie(mPipelineLayout, mDescriptorSetLayout, mRequiredDescriptorBindings) = createPipelineLayout();

	auto vertex_shader_module_create_info = vk::ShaderModuleCreateInfo();
	vertex_shader_module_create_info.codeSize = _vertex_shader_spirv.size();
	vertex_shader_module_create_info.pCode = reinterpret_cast<const uint32_t*>(_vertex_shader_spirv.data());

	auto fragment_shader_module_create_info = vk::ShaderModuleCreateInfo();
	fragment_shader_module_create_info.codeSize = _fragment_shader_spirv.size();
	fragment_shader_module_create_info.pCode = reinterpret_cast<const uint32_t*>(_fragment_shader_spirv.data());


	mVertexShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(vertex_shader_module_create_info);
	mFragmentShaderModule = UtilityVk::GetDriver()->mDevice.createShaderModule(fragment_shader_module_create_info);

}


void IKIGAI::RENDER::ShaderVk::_getReflection(std::string path, ShaderType type) {
	auto vertShaderCode = readFile(path);

	SpvReflectShaderModule module;
	SpvReflectResult result = spvReflectCreateShaderModule(vertShaderCode.size(), vertShaderCode.data(), &module);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);

	for (auto i = 0; i < module.descriptor_set_count; i++) {
		const auto& descr = module.descriptor_sets[i];
		for (auto j = 0; j < descr.binding_count; j++) {
			const auto spvUniformInfo = descr.bindings[j];

			if (mReflection.mNameToUniforms.contains(spvUniformInfo->name)) {
				mReflection.mUniforms[mReflection.mNameToUniforms.at(spvUniformInfo->name)].mShaderMask |= (size_t)type;
				continue;
			}

			ShaderReflection::Uniform uniform;
			uniform.mBind = spvUniformInfo->binding;
			uniform.mSet = spvUniformInfo->set;
			uniform.mName = spvUniformInfo->name;
			uniform.mShaderMask = (size_t)type;
			if (spvUniformInfo->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
				uniform.mType = ShaderReflection::UniformType::UNIFORM_BUFFER;
			} else if (spvUniformInfo->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
				uniform.mType = ShaderReflection::UniformType::STORAGE_BUFFER;
			} else if (spvUniformInfo->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
				//TODO: set curten sempler type
				uniform.mType = ShaderReflection::UniformType::SAMPLER_2D;
			} else {
				int a = 0;
			}
			//TODO: support array

			mReflection.mNameToUniforms[uniform.mName] = mReflection.mUniforms.size();
			mReflection.mUniforms.push_back(uniform);


			size_t sz = 0;
			for (int k = 0; k < spvUniformInfo->block.member_count; k++) {
				auto& member = spvUniformInfo->block.members[k];
				ShaderReflection::UniformMember m;
				m.mName = member.name;
				m.mOffset = member.offset;
				m.mSize = member.size;
				sz += m.mSize;
				m.mArraySize = (member.array.dims_count == 0 ? 0 : member.array.dims[0]);
				switch (member.type_description->op) {
				case SpvOpTypeFloat:
					m.mType = ShaderReflection::UniformType::FLOAT; break;
				case SpvOpTypeInt:
					m.mType = ShaderReflection::UniformType::INT; break;
				case SpvOpTypeBool:
					m.mType = ShaderReflection::UniformType::BOOL; break;
				case SpvOpTypeVector:
					if (m.mSize == 16)
						m.mType = ShaderReflection::UniformType::VEC4;
					if (m.mSize == 12)
						m.mType = ShaderReflection::UniformType::VEC3;
					if (m.mSize == 8)
						m.mType = ShaderReflection::UniformType::VEC2;
					break;
				case SpvOpTypeMatrix:
					if (m.mSize == 16 * 4)
						m.mType = ShaderReflection::UniformType::MAT4;
					if (m.mSize == 9 * 4)
						m.mType = ShaderReflection::UniformType::MAT3;
					break;
				default:
					m.mType = ShaderReflection::UniformType::NONE; break;
				}
				uniform.mMembers.push_back(m);
			}
			uniform.mSize = sz;
		}
	}

	for (auto i = 0; i < module.push_constant_block_count; i++) {
		const auto& block = module.push_constant_blocks[i];
		
		if (mReflection.mNameToUniforms.contains(block.name)) {
			mReflection.mUniforms[mReflection.mNameToUniforms.at(block.name)].mShaderMask |= (size_t)type;
			continue;
		}

		ShaderReflection::Uniform uniform;
		uniform.mType = ShaderReflection::UniformType::PUSH_CONSTANT;
		uniform.mName = block.name;
		uniform.mSize = block.size;
		uniform.mBind = block.offset;
		uniform.mShaderMask = (size_t)type;
		
		mReflection.mNameToUniforms[uniform.mName] = mReflection.mUniforms.size();
		mReflection.mUniforms.push_back(uniform);
	}

	if (type == ShaderType::VERTEX) {
		for (auto i = 0; i < module.input_variable_count; i++) {
			auto& descr = module.input_variables[i];
			ShaderReflection::InputParam member;
			member.mName = descr->name;
			member.mIndex = 0;
			member.mLocation = descr->location;

			//TODO: add int vector support
			int size = 0;
			if (descr->type_description->op == SpvOp::SpvOpTypeVector) {
				if (descr->type_description->traits.numeric.vector.component_count == 2) {
					member.mFormat = PixelFormat::RG_FLOAT;
					member.mSize = sizeof(float) * 2;
					member.mOffset = size;
					size += member.mSize;
				} else if (descr->type_description->traits.numeric.vector.component_count == 3) {
					member.mFormat = PixelFormat::RGB_FLOAT;
					member.mSize = sizeof(float) * 3;
					member.mOffset = size;
					size += member.mSize;
				} else if (descr->type_description->traits.numeric.vector.component_count == 4) {
					member.mFormat = PixelFormat::RGBA_FLOAT;
					member.mSize = sizeof(float) * 4;
					member.mOffset = size;
					size += member.mSize;
				}
			} else if (descr->type_description->op == SpvOp::SpvOpTypeMatrix) {
				//if (descr->type_description->traits.numeric.matrix.column_count == 3) {
				//	member.type = VertexDescriptor::Type::MAT3;
				//	member.size = sizeof(float) * 3 * 3;
				//	descriptor.size += sizeof(float) * 3 * 3;
				//} else if (descr->type_description->traits.numeric.matrix.column_count == 4) {
				//	member.type = VertexDescriptor::Type::MAT4;
				//	member.size = sizeof(float) * 4 * 4;
				//	descriptor.size += sizeof(float) * 4 * 4;
				//}
			} else if (descr->type_description->op == SpvOp::SpvOpTypeFloat) {
				member.mFormat = PixelFormat::R_FLOAT;
				member.mSize = sizeof(float);
				member.mOffset = size;
				size += member.mSize;
			} else if (descr->type_description->op == SpvOp::SpvOpTypeInt) {
				member.mFormat = PixelFormat::R_INT;
				member.mSize = sizeof(int);
				member.mOffset = size;
				size += member.mSize;
			} else if (descr->type_description->op == SpvOp::SpvOpTypeBool) {
				member.mFormat = PixelFormat::R_INT;
				member.mSize = sizeof(bool);
				member.mOffset = size;
				size += member.mSize;
			}

			mReflection.mInputParams.push_back(member);
		}

		std::sort(mReflection.mInputParams.begin(), mReflection.mInputParams.end(), [](const auto& a, const auto& b) {
			return a.mLocation < b.mLocation;
		});

		//Set offsets
		int  sz = 0;
		for (auto& e : mReflection.mInputParams) {
			e.mOffset = sz;
			sz += e.mSize;
		}
	}
	//TODO:
	if (type == ShaderType::FRAGMENT) {
		auto attachmentsCount = module.output_variable_count;
		for (auto i = 0; i < attachmentsCount; i++) {
			auto a = module.output_variables[i];
			if (std::string(a->name) == "gl_FragDepth") {
				attachmentsCount--;
			}
		}
	}

	spvReflectDestroyShaderModule(&module);
}
#endif
