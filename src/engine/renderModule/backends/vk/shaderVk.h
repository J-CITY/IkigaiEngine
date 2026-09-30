#pragma once

#ifdef VULKAN_BACKEND
#include <volk.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

#include "../interface/shaderInterface.h"
#include "../interface/uniformBufferInterface.h"
#include "../interface/reflectionStructs.h"

namespace IKIGAI::RENDER {
	class TextureVk;
	class UniformVkInterface;



	class ShaderVk : public ShaderInterface {
	public:

		vk::raii::DescriptorSetLayout mDescriptorSetLayout = nullptr;
		vk::raii::PipelineLayout mPipelineLayout = nullptr;
		vk::raii::ShaderModule mVertexShaderModule = nullptr;
		vk::raii::ShaderModule mFragmentShaderModule = nullptr;
		std::vector<vk::DescriptorSetLayoutBinding> mRequiredDescriptorBindings;

		ShaderVk(std::map<ShaderType, std::string> shaderCode);

		void _getReflection(std::string path, ShaderType type);
		std::tuple<vk::raii::PipelineLayout, vk::raii::DescriptorSetLayout, std::vector<vk::DescriptorSetLayoutBinding>> createPipelineLayout();
		static std::shared_ptr<ShaderVk> CreateFromPath(std::map<ShaderType, std::string> path);
		void bind() override{};
		void unbind() override{};
		void recompile(const ShaderResource& res) override {}
	};
}
#endif
