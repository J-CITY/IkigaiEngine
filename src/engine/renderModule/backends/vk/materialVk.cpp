#include "materialVk.h"

#ifdef VULKAN_BACKEND
#include "resourceModule/serviceManager.h"
#include "resourceModule/shaderManager.h"
#include "resourceModule/textureManager.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "utilsModule/jsonLoader.h"
#include "renderModule/render.h"

using namespace IKIGAI;
using namespace IKIGAI::RENDER;

MaterialVk::MaterialVk() {
}

MaterialVk::MaterialVk(const MaterialResource& res) : MaterialInterface(res) {
	create(res);
}

void MaterialVk::create(const MaterialResource& res) {
	auto resData = UTILS::FromJson<RENDER::ShaderResource>(res.ShaderPath);
	if (resData.isErr()) {

	}
	auto resDataVal = resData.unwrap();
	resDataVal.path = res.ShaderPath;
	auto shader = std::static_pointer_cast<RENDER::ShaderVk>(
		IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::ShaderLoader>().CreateFromResource(resDataVal));
	MaterialVk::setShader(shader);

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
			}
			else {
				mUniforms[k] = arg;
			}
		}, v);
	}
}

void MaterialVk::setShader(std::shared_ptr<ShaderInterface> shader) {
	mShader = std::static_pointer_cast<ShaderVk>(shader);
	generateUniformsData();
}

MaterialResource MaterialVk::getDescriptor() {
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
			}
			else {
				res.Uniforms[name] = arg;
			}
		}, value);
	}

	return res;
}

void MaterialVk::setExternalBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> buffer) {
	if (!buffer) {
		mExternalBuffers.erase(name);
		mUniformBuffers.erase(name);
		return;
	}
	mUniformBuffers[name] = std::static_pointer_cast<UniformBufferVk>(buffer);
}

void MaterialVk::setExternalBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> buffer) {
	if (!buffer) {
		mExternalBuffers.erase(name);
		mStorageBuffers.erase(name);
		return;
	}
	mStorageBuffers[name] = std::static_pointer_cast<StorageBufferVk>(buffer);
}

void MaterialVk::generateUniformsData() {
	mUniforms.clear();
	mUniformBuffers.clear();
	mStorageBuffers.clear();
	mExternalBuffers.clear();

	if (!mShader) return;

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
			std::shared_ptr<TextureVk> t;
			mUniforms[uniform.mName] = t;
		} break;
		case ShaderReflection::UniformType::UNIFORM_BUFFER: {
			mUniformBuffers[uniform.mName] = std::make_shared<UniformBufferVk>(nullptr, uniform.mSize);
			//TODO: array is not support yet
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
				}
			}
		} break;
		case ShaderReflection::UniformType::STORAGE_BUFFER: {
			//TODO: save count elements of ssbo and use mSize * mCount
			mStorageBuffers[uniform.mName] = std::make_shared<StorageBufferVk>(nullptr, uniform.mSize, 1);
		} break;
		}
	}
}

void MaterialVk::fillUniforms(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) {
	if (!mShader) return;

	auto& shaderInfo = mShader->getReflection();
	auto& renderer = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RENDER::Renderer>();

	for (const auto& uniform : shaderInfo.mUniforms) {
		switch (uniform.mType) {
		case ShaderReflection::UniformType::SAMPLER_2D:
		case ShaderReflection::UniformType::SAMPLER_CUBE:
		case ShaderReflection::UniformType::SAMPLER_3D:
		case ShaderReflection::UniformType::SAMPLER_2D_ARRAY: {
			std::shared_ptr<TextureInterface> tex = nullptr;
			if (mUniforms.contains(uniform.mName)) {
				tex = std::get<std::shared_ptr<TextureInterface>>(mUniforms[uniform.mName]);
			}
			if (!tex && defaultTexture) {
				tex = defaultTexture;
			}
			if (tex) {
				renderer.setTexture(uniform.mBind, tex);
			}
		} break;
		case ShaderReflection::UniformType::UNIFORM_BUFFER: {
			//TODO: support array
			if (!mExternalBuffers.contains(uniform.mName) && mUniformBuffers.contains(uniform.mName)) {
				std::vector<std::byte> bufferData(uniform.mSize);
				for (const auto& member : uniform.mMembers) {
					if (mUniforms.contains(uniform.mName + member.mName)) {
						std::visit([&](auto& arg) {
							using T = std::decay_t<decltype(arg)>;
							memcpy((void*)(bufferData.data() + member.mOffset), &arg, sizeof(T));
						}, mUniforms[uniform.mName + member.mName]);
					}
				}
				mUniformBuffers[uniform.mName]->setData(bufferData.data(), bufferData.size());
			}
			if (mUniformBuffers.contains(uniform.mName)) {
				renderer.setUniformBuffer(uniform.mBind, mUniformBuffers[uniform.mName]);
			}
		} break;
		case ShaderReflection::UniformType::STORAGE_BUFFER: {
			//TODO: material now support only set ssbo external
			//if (mStorageBuffers.contains(uniform.mName)) {
			//	renderer.setStorageBuffer(uniform.mBind, mStorageBuffers[uniform.mName]);
			//}
		} break;
		}
	}
}

void MaterialVk::bind(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) {
	if (mShader) mShader->bind();
	fillUniforms(defaultTexture, useTextures);
}

void MaterialVk::unbind() {
	if (mShader) mShader->unbind();
}

void MaterialVk::set(const std::string& name, const UniformData& data) {
	mUniforms.at(name) = data;
}

MaterialInterface::UniformData& MaterialVk::get(const std::string& name) {
	return mUniforms.at(name);
}

#endif
