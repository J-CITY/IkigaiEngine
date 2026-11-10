#pragma once

#ifdef VULKAN_BACKEND
#include <variant>
#include <memory>
#include <map>
#include <set>

#include "shaderVk.h"
#include "textureVk.h"
#include "uniformBufferVk.h"
#include "storageBufferVk.h"
#include <resourceModule/serviceManager.h>
#include "../interface/materialInterface.h"
#include "resourceModule/serializerInterface.h"

namespace IKIGAI::RENDER {
	class StorageBufferInterface;
	class UniformBufferInterface;

	class MaterialVk : public MaterialInterface {
	private:
		std::shared_ptr<ShaderVk> mShader;
		std::map<std::string, MaterialInterface::UniformData> mUniforms;

		std::set<std::string> mExternalBuffers;
		std::map<std::string, std::shared_ptr<UniformBufferVk>> mUniformBuffers;
		std::map<std::string, std::shared_ptr<StorageBufferVk>> mStorageBuffers;

		void generateUniformsData();
		void fillUniforms(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures);
		
	public:
		MaterialVk();
		MaterialVk(const MaterialResource& descriptor);
		~MaterialVk() override = default;

		void bind(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) override;
		void unbind() override;

		void setShader(std::shared_ptr<ShaderInterface> shader) override;
		bool hasShader() const override {
			return mShader != nullptr;
		}
		std::shared_ptr<ShaderInterface> getShader() const override {
			return mShader;
		}

		void create(const MaterialResource& res) override;
		bool adoptReloadedMaterial(MaterialInterface& replacement) noexcept override;

		void setExternalBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> buffer);
		void setExternalBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> buffer);

		void set(const std::string& name, const UniformData& data) override;
		UniformData& get(const std::string& name) override;
		const UniformData* findUniform(const std::string& name) const noexcept override;

		MaterialResource getDescriptor() override;
	};
}
#endif
