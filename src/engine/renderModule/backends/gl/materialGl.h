#pragma once

#ifdef OPENGL_BACKEND
#include <variant>
#include <memory>

#include "shaderGl.h"
#include "textureGl.h"
#include "../interface/materialInterface.h"

namespace IKIGAI {
	namespace RENDER {
		class StorageBufferInterface;
		class StorageBufferGl;
		class UniformBufferGl;
		class UniformBuffer;

		class MaterialGl: public MaterialInterface {
		public:
			std::shared_ptr<ShaderGl> mShader;
			std::map<std::string, UniformData> mUniforms;

			std::set<std::string> mExternalBuffers;
			std::map<std::string, std::shared_ptr<UniformBufferGl>> mUniformBuffers;
			std::map<std::string, std::shared_ptr<StorageBufferGl>> mStorageBuffers;

			MaterialGl() = default;
			MaterialGl(const MaterialResource& res);
			~MaterialGl() override;

			void bind(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) override;
			void unbind() override;

			void setShader(std::shared_ptr<ShaderInterface> shader) override;
			bool hasShader() const override;
			std::shared_ptr<ShaderInterface> getShader() const override;

			void setExternalBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> buffer);
			void setExternalBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> buffer);

			void set(const std::string& name, const UniformData& data) override;
			UniformData& get(const std::string& name) override;
			const UniformData* findUniform(const std::string& name) const noexcept override;


			MaterialResource getDescriptor() override;

			void create(const MaterialResource& res) override;
			bool adoptReloadedMaterial(MaterialInterface& replacement) noexcept override;
			auto& getUniforms() { return mUniforms; }
		private:
			void generateUniformsData();
			void fillUniforms(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures);

			int textureSlot = 0;
		};
	}
}

#endif
