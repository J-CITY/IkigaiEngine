#pragma once
#include "resourceModule/serializerInterface.h"

#ifdef DX12_BACKEND
#include <variant>
#include <memory>

#include "shaderDx12.h"
#include "textureDx12.h"
//#include "uniformBufferVk.h"
#include "../interface/materialInterface.h"


namespace IKIGAI::RENDER {
	class StorageBufferInterface;
	class StorageBufferDx12;
	class UniformBufferDx12;
	class UniformBuffer;

	class MaterialDx12 : public MaterialInterface {
	private:
		std::shared_ptr<ShaderDx12> mShader;
		std::map<std::string, MaterialInterface::UniformData> mUniforms;

		std::set<std::string> mExternalBuffers;
		std::map<std::string, std::shared_ptr<UniformBufferDx12>> mUniformBuffers;
		std::map<std::string, std::shared_ptr<StorageBufferDx12>> mStorageBuffers;

		void generateUniformsData();
		void fillUniforms(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures);
		
	public:
		MaterialDx12();
		MaterialDx12(const MaterialResource& descriptor);

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
