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
		vk::raii::ShaderModule mGeometryShaderModule = nullptr;
		vk::raii::ShaderModule mTessellationControlShaderModule = nullptr;
		vk::raii::ShaderModule mTessellationEvaluationShaderModule = nullptr;
		vk::raii::ShaderModule mComputeShaderModule = nullptr;
		std::vector<vk::DescriptorSetLayoutBinding> mRequiredDescriptorBindings;

		static std::shared_ptr<ShaderVk> CreateFromSource(const std::map<ShaderType, std::string>& source);
		static std::shared_ptr<ShaderVk> CreateFromPath(const std::map<ShaderType, std::string>& paths);
		static std::shared_ptr<ShaderVk> Create(const ShaderResource& resource);

		ShaderVk() = default;
		ShaderVk(const ShaderResource& res);
		~ShaderVk() override;

		std::tuple<vk::raii::PipelineLayout, vk::raii::DescriptorSetLayout, std::vector<vk::DescriptorSetLayoutBinding>> createPipelineLayout();

		void bind() override{};
		void unbind() override{};
		void recompile(const ShaderResource& res) override;

	private:
		void create(const ShaderResource& res);
		void clear() const;
	};
}
#endif
