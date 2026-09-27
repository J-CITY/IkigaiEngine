#ifdef METAL_BACKEND
#include "materialMetal.h"
#include <resourceModule/serviceManager.h>
#include <resourceModule/shaderManager.h>
#include <resourceModule/textureManager.h>
#include <resourceModule/fileSystem/fileSystem.h>
#include <utilsModule/jsonLoader.h>
#include <renderModule/render.h>

namespace IKIGAI::RENDER {
    
    MaterialMetal::MaterialMetal() {}
    
    MaterialMetal::MaterialMetal(const MaterialResource& res) : MaterialInterface(res) {
        create(res);
    }
    
    void MaterialMetal::create(const MaterialResource& res) {
        // Basic creation logic similar to Vulkan backend
        auto resData = UTILS::FromJson<RENDER::ShaderResource>(res.ShaderPath);
        if (resData.isOk()) {
            auto resDataVal = resData.unwrap();
            resDataVal.path = res.ShaderPath;
            auto shader = std::static_pointer_cast<RENDER::ShaderMetal>(
                IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::ShaderLoader>().CreateFromResource(resDataVal));
            setShader(shader);
        }
        
        auto& textureLoader = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::TextureLoader>();
        for (const auto& [k, v] : res.Uniforms) {
            std::visit([&](auto& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    const auto ext = IKIGAI::RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFileExtension(arg);
                    if (ext == ".texture") {
                        mUniforms[k] = textureLoader.createFromResource(arg);
                    } else {
                        mUniforms[k] = textureLoader.createFromFile(arg, true);
                    }
                } else {
                    mUniforms[k] = arg;
                }
            }, v);
        }
    }
    
    void MaterialMetal::setShader(std::shared_ptr<ShaderInterface> shader) {
        mShader = std::static_pointer_cast<ShaderMetal>(shader);
        generateUniformsData();
    }
    
    MaterialResource MaterialMetal::getDescriptor() {
        MaterialResource res;
        res.path = mPath;
        res.ShaderPath = mShader ? mShader->mPath : "";
        res.Blendable = mBlendable;
        res.BackfaceCulling = mBackfaceCulling;
        res.FrontfaceCulling = mFrontfaceCulling;
        res.DepthTest = mDepthTest;
        res.DepthWriting = mDepthWriting;
        res.ColorWriting = mColorWriting;
        res.GpuInstances = mGpuInstances;
        res.IsDeferred = mIsDeferred;
        res.DepthFunc = mDepthFunc;
        
        for (const auto& [name, value] : mUniforms) {
            std::visit([&](auto& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::shared_ptr<TextureInterface>>) {
                    if (arg) res.Uniforms[name] = arg->getPath();
                } else {
                    res.Uniforms[name] = arg;
                }
            }, value);
        }
        return res;
    }
    
    void MaterialMetal::setExternalBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> buffer) {
        if (!buffer) {
            mExternalBuffers.erase(name);
            mUniformBuffers.erase(name);
            return;
        }
        mUniformBuffers[name] = std::static_pointer_cast<UniformBufferMetal>(buffer);
    }
    
    void MaterialMetal::setExternalBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> buffer) {
        if (!buffer) {
            mExternalBuffers.erase(name);
            mStorageBuffers.erase(name);
            return;
        }
        mStorageBuffers[name] = std::static_pointer_cast<StorageBufferMetal>(buffer);
    }
    
    void MaterialMetal::set(const std::string& name, const UniformData& data) {
        mUniforms[name] = data;
    }
    
    MaterialInterface::UniformData& MaterialMetal::get(const std::string& name) {
        return mUniforms[name];
    }
    
    void MaterialMetal::generateUniformsData() {
        // TODO: Extract uniform buffer layout and types using spirv-cross reflection.
        // For now, this is a stub. 
    }
    
    void MaterialMetal::fillUniforms(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) {
        // TODO: Using reflection info, fill the allocated UniformBufferMetal instances 
        // with the data from mUniforms, and bind textures to DriverMetal.
    }
    
    void MaterialMetal::bind(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) {
        fillUniforms(defaultTexture, useTextures);
    }
    
    void MaterialMetal::unbind() {}
}
#endif
