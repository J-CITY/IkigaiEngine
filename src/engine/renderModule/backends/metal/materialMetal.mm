#ifdef METAL_BACKEND
#include "materialMetal.h"

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <vector>

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
        mExternalBuffers.insert(name);
        mUniformBuffers[name] = std::static_pointer_cast<UniformBufferMetal>(buffer);
    }
    
    void MaterialMetal::setExternalBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> buffer) {
        if (!buffer) {
            mExternalBuffers.erase(name);
            mStorageBuffers.erase(name);
            return;
        }
        mExternalBuffers.insert(name);
        mStorageBuffers[name] = std::static_pointer_cast<StorageBufferMetal>(buffer);
    }
    
    void MaterialMetal::set(const std::string& name, const UniformData& data) {
        mUniforms.at(name) = data;
    }
    
    MaterialInterface::UniformData& MaterialMetal::get(const std::string& name) {
        return mUniforms.at(name);
    }
    
    void MaterialMetal::generateUniformsData() {
        mUniforms.clear();
        mUniformBuffers.clear();
        mStorageBuffers.clear();
        mExternalBuffers.clear();
        if (!mShader) {
            return;
        }

        const auto& shaderInfo = mShader->getReflection();
        for (const auto& uniform : shaderInfo.mUniforms) {
            if (isEngineUniform(uniform.mName)) {
                continue;
            }
            switch (uniform.mType) {
            case ShaderReflection::UniformType::SAMPLER_2D:
            case ShaderReflection::UniformType::SAMPLER_CUBE:
            case ShaderReflection::UniformType::SAMPLER_3D:
            case ShaderReflection::UniformType::SAMPLER_2D_ARRAY: {
                std::shared_ptr<TextureMetal> texture;
                mUniforms[uniform.mName] = texture;
            } break;
            case ShaderReflection::UniformType::UNIFORM_BUFFER: {
                mUniformBuffers[uniform.mName] = std::make_shared<UniformBufferMetal>(uniform.mSize);
                for (const auto& member : uniform.mMembers) {
                    switch (member.mType) {
                    case ShaderReflection::UniformType::MAT4: mUniforms[uniform.mName + member.mName] = MATH::Matrix4f(); break;
                    case ShaderReflection::UniformType::MAT3: mUniforms[uniform.mName + member.mName] = MATH::Matrix3f(); break;
                    case ShaderReflection::UniformType::VEC4: mUniforms[uniform.mName + member.mName] = MATH::Vector4f(); break;
                    case ShaderReflection::UniformType::VEC3: mUniforms[uniform.mName + member.mName] = MATH::Vector3f(); break;
                    case ShaderReflection::UniformType::VEC2: mUniforms[uniform.mName + member.mName] = MATH::Vector2f(); break;
                    case ShaderReflection::UniformType::INT: mUniforms[uniform.mName + member.mName] = 0; break;
                    case ShaderReflection::UniformType::FLOAT: mUniforms[uniform.mName + member.mName] = 0.0f; break;
                    case ShaderReflection::UniformType::BOOL: mUniforms[uniform.mName + member.mName] = false; break;
                    default: break;
                    }
                }
            } break;
            case ShaderReflection::UniformType::STORAGE_BUFFER: {
                mStorageBuffers[uniform.mName] = std::make_shared<StorageBufferMetal>(uniform.mSize, 1);
            } break;
            default: break;
            }
        }
    }
    
    void MaterialMetal::fillUniforms(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) {
        (void)useTextures;
        if (!mShader) {
            return;
        }
        auto& renderer = RESOURCES::ServiceManager::Get<Renderer>();
        for (const auto& uniform : mShader->getReflection().mUniforms) {
            switch (uniform.mType) {
            case ShaderReflection::UniformType::SAMPLER_2D:
            case ShaderReflection::UniformType::SAMPLER_CUBE:
            case ShaderReflection::UniformType::SAMPLER_3D:
            case ShaderReflection::UniformType::SAMPLER_2D_ARRAY: {
                std::shared_ptr<TextureInterface> texture;
                if (mUniforms.contains(uniform.mName)) {
                    texture = std::get<std::shared_ptr<TextureInterface>>(mUniforms[uniform.mName]);
                }
                if (!texture && defaultTexture) {
                    texture = defaultTexture;
                }
                if (texture) {
                    renderer.setTexture(uniform.mBind, texture);
                }
            } break;
            case ShaderReflection::UniformType::UNIFORM_BUFFER: {
                if (!mExternalBuffers.contains(uniform.mName) && mUniformBuffers.contains(uniform.mName)) {
                    std::vector<std::byte> bufferData(uniform.mSize);
                    for (const auto& member : uniform.mMembers) {
                        const std::string memberName = uniform.mName + member.mName;
                        if (!mUniforms.contains(memberName)) {
                            continue;
                        }
                        std::visit([&](auto& arg) {
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (!std::is_same_v<T, std::shared_ptr<TextureInterface>>) {
                                std::memcpy(bufferData.data() + member.mOffset, &arg, sizeof(T));
                            }
                        }, mUniforms[memberName]);
                    }
                    mUniformBuffers[uniform.mName]->setData(bufferData.data(), bufferData.size());
                }
                if (mUniformBuffers.contains(uniform.mName)) {
                    renderer.setUniformBuffer(uniform.mBind, mUniformBuffers[uniform.mName]);
                }
            } break;
            case ShaderReflection::UniformType::STORAGE_BUFFER:
                break;
            default:
                break;
            }
        }
    }
    
    void MaterialMetal::bind(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) {
        fillUniforms(defaultTexture, useTextures);
    }
    
    void MaterialMetal::unbind() {}
}
#endif
