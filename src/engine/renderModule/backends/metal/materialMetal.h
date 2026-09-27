#pragma once
#ifdef METAL_BACKEND
#include <map>
#include <set>
#include <string>
#include <memory>
#include <variant>

#include "../interface/materialInterface.h"
#include "shaderMetal.h"
#include "textureMetal.h"
#include "bufferMetal.h"

namespace IKIGAI::RENDER {
    class MaterialMetal : public MaterialInterface {
    public:
        MaterialMetal();
        MaterialMetal(const MaterialResource& descriptor);
        ~MaterialMetal() override = default;

        void bind(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) override;
        void unbind() override;

        void setShader(std::shared_ptr<ShaderInterface> shader) override;
        bool hasShader() const override { return mShader != nullptr; }
        std::shared_ptr<ShaderInterface> getShader() const override { return mShader; }

        void create(const MaterialResource& res) override;

        void setExternalBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> buffer);
        void setExternalBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> buffer);

        void set(const std::string& name, const UniformData& data) override;
        UniformData& get(const std::string& name) override;

        MaterialResource getDescriptor() override;

    private:
        std::shared_ptr<ShaderMetal> mShader;
        std::map<std::string, MaterialInterface::UniformData> mUniforms;

        std::set<std::string> mExternalBuffers;
        std::map<std::string, std::shared_ptr<UniformBufferMetal>> mUniformBuffers;
        std::map<std::string, std::shared_ptr<StorageBufferMetal>> mStorageBuffers;
        
        void generateUniformsData();
        void fillUniforms(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures);
    };
}
#endif
