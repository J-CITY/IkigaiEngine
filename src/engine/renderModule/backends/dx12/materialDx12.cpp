#include "materialDx12.h"

#ifdef DX12_BACKEND

#include "d3dUtil.h"
#include "driverDx12.h"
#include "renderModule/render.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/shaderManager.h"
#include "resourceModule/textureManager.h"
#include "storageBufferDx12.h"
#include "uniformBufferDx12.h"
#include <algorithm>
#include <utilsModule/jsonLoader.h>

// TODO: add dirty flag for update buffers

using namespace IKIGAI;
using namespace IKIGAI::RENDER;

MaterialDx12::MaterialDx12() { }

MaterialDx12::MaterialDx12(const MaterialResource& res)
	: MaterialInterface(res) {
	create(res);
}

void MaterialDx12::create(const MaterialResource& res) {
	auto prepared = prepareMaterial(res);
	setShader(prepared.shader);
	for (auto& [name, value] : prepared.uniforms) {
		mUniforms[name] = std::move(value);
	}
	applyMaterialState(res);
}

bool MaterialDx12::adoptReloadedMaterial(MaterialInterface& prepared) noexcept {
	auto* replacement = dynamic_cast<MaterialDx12*>(&prepared);
	if (!replacement) {
		return false;
	}
	swapMaterialState(*replacement);
	mShader.swap(replacement->mShader);
	mUniforms.swap(replacement->mUniforms);
	mExternalBuffers.swap(replacement->mExternalBuffers);
	mUniformBuffers.swap(replacement->mUniformBuffers);
	mStorageBuffers.swap(replacement->mStorageBuffers);
	return true;
}

void MaterialDx12::setShader(std::shared_ptr<ShaderInterface> shader) {
	mShader = std::static_pointer_cast<ShaderDx12>(shader);
	generateUniformsData();
}

MaterialResource MaterialDx12::getDescriptor() {
	MaterialResource res;
	res.path = mPath;
	res.ShaderPath = mShader->mPath;
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
		std::visit(
			[&](auto& arg) {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, std::shared_ptr<TextureInterface>>) {
					if (arg) {
						res.Uniforms[name] = arg->getPath();
					}
				}
				else {
					res.Uniforms[name] = arg;
				}
			},
			value);
	}

	completeDescriptor(res);
	return res;
}

void MaterialDx12::setExternalBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> buffer) {
	if (!buffer) {
		mExternalBuffers.erase(name);
		mUniformBuffers.erase(name);
		return;
	}
	mUniformBuffers[name] = std::static_pointer_cast<UniformBufferDx12>(buffer);
}

void MaterialDx12::setExternalBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> buffer) {
	if (!buffer) {
		mExternalBuffers.erase(name);
		mStorageBuffers.erase(name);
		return;
	}
	mStorageBuffers[name] = std::static_pointer_cast<StorageBufferDx12>(buffer);
}

void MaterialDx12::generateUniformsData() {
	mUniforms.clear();
	mUniformBuffers.clear();
	mStorageBuffers.clear();
	mExternalBuffers.clear();
	const auto& shaderInfo = mShader->getReflection();
	for (const auto& uniform : shaderInfo.mUniforms) {
		if (mShader->isBindlessResource(uniform)) {
			continue;
		}
		if (isEngineUniform(uniform.mName)) { // this pass from render
			continue;
		}
		switch (uniform.mType) {
		case ShaderReflection::UniformType::SAMPLER_2D:
		case ShaderReflection::UniformType::SAMPLER_CUBE:
		case ShaderReflection::UniformType::SAMPLER_3D:
		case ShaderReflection::UniformType::SAMPLER_2D_ARRAY: {
			std::shared_ptr<TextureDx12> t;
			mUniforms[uniform.mName] = t;
		} break;
		case ShaderReflection::UniformType::UNIFORM_BUFFER: {
			mUniformBuffers[uniform.mName] = std::make_shared<UniformBufferDx12>(nullptr, uniform.mSize);
			// TODO: array is not support yet
			for (const auto& member : uniform.mMembers) {
				switch (member.mType) {
				case ShaderReflection::UniformType::MAT4:
					mUniforms[uniform.mName + member.mName] = MATH::Matrix4f();
					break;
				case ShaderReflection::UniformType::MAT3:
					mUniforms[uniform.mName + member.mName] = MATH::Matrix3f();
					break;
				case ShaderReflection::UniformType::VEC4:
					mUniforms[uniform.mName + member.mName] = MATH::Vector4f();
					break;
				case ShaderReflection::UniformType::VEC3:
					mUniforms[uniform.mName + member.mName] = MATH::Vector3f();
					break;
				case ShaderReflection::UniformType::VEC2:
					mUniforms[uniform.mName + member.mName] = MATH::Vector2f();
					break;
				case ShaderReflection::UniformType::INT:
					mUniforms[uniform.mName + member.mName] = 0;
					break;
				case ShaderReflection::UniformType::FLOAT:
					mUniforms[uniform.mName + member.mName] = 0.0f;
					break;
				case ShaderReflection::UniformType::BOOL:
					mUniforms[uniform.mName + member.mName] = false;
					break;
				}
			}
		} break;
		case ShaderReflection::UniformType::STORAGE_BUFFER: {
			// TODO: save count elements of ssbo and use mSize * mCount
			mStorageBuffers[uniform.mName] = std::make_shared<StorageBufferDx12>(nullptr, uniform.mSize, 1);
		} break;
		}
	}
}

void MaterialDx12::fillUniforms(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) {
	if (!mShader) {
		return;
	}
	auto& shaderInfo = mShader->getReflection();
	auto& renderer = RESOURCES::ServiceManager::Get<Renderer>();
	for (const auto& uniform : shaderInfo.mUniforms) {
		if (mShader->isBindlessResource(uniform)) {
			continue;
		}
		switch (uniform.mType) {
		case ShaderReflection::UniformType::SAMPLER_2D:
		case ShaderReflection::UniformType::SAMPLER_CUBE:
		case ShaderReflection::UniformType::SAMPLER_3D:
		case ShaderReflection::UniformType::SAMPLER_2D_ARRAY: {
			if (!useTextures) {
				break;
			}
			std::shared_ptr<TextureInterface> tex;
			if (mUniforms.contains(uniform.mName)) {
				if (const auto* stored = std::get_if<std::shared_ptr<TextureInterface>>(&mUniforms.at(uniform.mName))) {
					tex = *stored;
				}
			}
			if (!tex) {
				tex = defaultTexture;
			}
			if (tex) {
				renderer.setTexture(uniform.mBind, tex);
			}
		} break;
		case ShaderReflection::UniformType::UNIFORM_BUFFER: {
			// Engine buffers (engine_UBO, engine_Bones) are bound by the renderer after bind().
			if (!mUniformBuffers.contains(uniform.mName)) {
				break;
			}
			auto buffer = mUniformBuffers.at(uniform.mName);
			if (!buffer) {
				break;
			}
			if (!mExternalBuffers.contains(uniform.mName)) {
				size_t bytes = uniform.mSize;
				for (const auto& member : uniform.mMembers) {
					if (member.mOffset < 0 || member.mSize < 0) {
						continue;
					}
					bytes = std::max(bytes, static_cast<size_t>(member.mOffset) + static_cast<size_t>(member.mSize));
				}
				if (bytes > 0) {
					std::vector<std::byte> bufferData(bytes);
					for (const auto& member : uniform.mMembers) {
						const auto key = uniform.mName + member.mName;
						if (!mUniforms.contains(key) || member.mOffset < 0 || member.mSize <= 0) {
							continue;
						}
						std::visit(
							[&](auto& arg) {
								using T = std::decay_t<decltype(arg)>;
								const size_t copySize = std::min(sizeof(T), static_cast<size_t>(member.mSize));
								if (static_cast<size_t>(member.mOffset) + copySize > bufferData.size()) {
									return;
								}
								memcpy(bufferData.data() + member.mOffset, &arg, copySize);
							},
							mUniforms.at(key));
					}
					renderer.updateUniformBuffer(buffer, bufferData.data(), bufferData.size());
				}
			}
			renderer.setUniformBuffer(uniform.mBind, buffer);
		} break;
		case ShaderReflection::UniformType::STORAGE_BUFFER: {
			// TODO: material now support only set ssbo external
			// TODO:
			// driver->mStorageBuffers[uniform.mBind] = mUniformBuffers[uniform.mName];
		} break;
		}
	}
}

void MaterialDx12::bind(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) {
	mShader->bind();
	fillUniforms(defaultTexture, useTextures);
}

void MaterialDx12::unbind() {
	mShader->unbind();
}

void MaterialDx12::set(const std::string& name, const UniformData& data) {
	mUniforms.at(name) = data;
}

MaterialInterface::UniformData& MaterialDx12::get(const std::string& name) {
	return mUniforms.at(name);
}

const MaterialInterface::UniformData* MaterialDx12::findUniform(const std::string& name) const noexcept {
	const auto found = mUniforms.find(name);
	return found == mUniforms.end() ? nullptr : &found->second;
}
#endif
