#include "materialGl.h"

#ifdef OPENGL_BACKEND
#include "../interface/reflectionStructs.h"
#include "../interface/resourceStruct.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/shaderManager.h"
#include "resourceModule/textureManager.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/visitorHelper.h"
#include "storageBufferGl.h"
#include "uniformBufferGl.h"
#include "renderModule/render.h"
#include "resourceModule/fileSystem/fileSystem.h"

IKIGAI::RENDER::MaterialGl::MaterialGl(const MaterialResource& res): MaterialInterface(res){
	create(res);
}

void IKIGAI::RENDER::MaterialGl::create(const MaterialResource& res) {
	//TODO: do it not in constructor (add var in Material resource) 
	auto resData = UTILS::FromJson<RENDER::ShaderResource>(res.ShaderPath);
	if (resData.isErr()) {

	}
	auto resDataVal = resData.unwrap();
	resDataVal.path = res.ShaderPath;
	auto shader = std::static_pointer_cast<RENDER::ShaderGl>(
		IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::ShaderLoader>().CreateFromResource(resDataVal));
	MaterialGl::setShader(shader);

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

IKIGAI::RENDER::MaterialGl::~MaterialGl() = default;

void IKIGAI::RENDER::MaterialGl::setShader(std::shared_ptr<ShaderInterface> shader) {
	mShader = std::static_pointer_cast<ShaderGl>(shader);
	generateUniformsData();
}

bool IKIGAI::RENDER::MaterialGl::hasShader() const {
	return mShader != nullptr;
}

std::shared_ptr<IKIGAI::RENDER::ShaderInterface> IKIGAI::RENDER::MaterialGl::getShader() const {
	return mShader;
}

void IKIGAI::RENDER::MaterialGl::setExternalBuffer(const std::string& name, std::shared_ptr<IKIGAI::RENDER::UniformBufferInterface> buffer) {
	if (!buffer) {
		mExternalBuffers.erase(name);
		mUniformBuffers.erase(name);
		return;
	}
	mUniformBuffers[name] = std::static_pointer_cast<UniformBufferGl>(buffer);
}

void IKIGAI::RENDER::MaterialGl::setExternalBuffer(const std::string& name, std::shared_ptr<IKIGAI::RENDER::StorageBufferInterface> buffer) {
	if (!buffer) {
		mExternalBuffers.erase(name);
		mStorageBuffers.erase(name);
		return;
	}
	mStorageBuffers[name] = std::static_pointer_cast<StorageBufferGl>(buffer);
}

void IKIGAI::RENDER::MaterialGl::generateUniformsData() {
	mUniforms.clear();
	mUniformBuffers.clear();
	mStorageBuffers.clear();
	mExternalBuffers.clear();

	const auto& shaderInfo = mShader->getReflection();
	for (const auto& uniform : shaderInfo.mUniforms) {
		if (isEngineUniform(uniform.mName)) {//this pass from render
			continue;
		}
		switch (uniform.mType) {
		case ShaderReflection::UniformType::SAMPLER_2D:
		case ShaderReflection::UniformType::SAMPLER_CUBE:
		case ShaderReflection::UniformType::SAMPLER_3D:
		case ShaderReflection::UniformType::SAMPLER_2D_ARRAY: {
			std::shared_ptr<TextureGl> t;
			mUniforms[uniform.mName] = t;
		} break;
		case ShaderReflection::UniformType::UNIFORM_BUFFER: {
			mUniformBuffers[uniform.mName] = std::make_shared<UniformBufferGl>(nullptr, uniform.mSize);
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
			mStorageBuffers[uniform.mName] = std::make_shared<StorageBufferGl>(nullptr, uniform.mSize, 1);
		} break;
		}
	}
}

void IKIGAI::RENDER::MaterialGl::fillUniforms(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) {
	textureSlot = 0;

	auto& shaderInfo = mShader->getReflection();
	for (const auto& uniform : shaderInfo.mUniforms) {
		if (uniform.mName.starts_with("engine")) {
			continue;
		}
		switch (uniform.mType) {
		case ShaderReflection::UniformType::SAMPLER_2D:
		case ShaderReflection::UniformType::SAMPLER_CUBE:
		case ShaderReflection::UniformType::SAMPLER_3D:
		case ShaderReflection::UniformType::SAMPLER_2D_ARRAY: {
			if (useTextures) {
				if (auto tex = std::get<std::shared_ptr<TextureInterface>>(mUniforms.at(uniform.mName))) {
					std::static_pointer_cast<TextureGl>(tex)->bind(textureSlot);
				}
				else if (defaultTexture) {
					reinterpret_cast<TextureGl*>(defaultTexture.get())->bind(textureSlot);
				}
				mShader->setInt(uniform.mName, textureSlot);
				textureSlot++;
			}
		} break;
		case ShaderReflection::UniformType::UNIFORM_BUFFER: {
			//TODO: support array
			if (!mExternalBuffers.contains(uniform.mName)) {
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

				auto& render = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RENDER::Renderer>();
				render.setUniformBuffer(uniform.mBind, std::static_pointer_cast<UniformBufferInterface>(mUniformBuffers[uniform.mName]));
			}
				//TODO:
			//driver->setUniformBuffer(uniform.mBind, mUniformBuffers[uniform.mName]);
		} break;
		case ShaderReflection::UniformType::BOOL: {
			mShader->setBool(uniform.mName, std::get<bool>(mUniforms.at(uniform.mName))); break;
		}
		case ShaderReflection::UniformType::INT: {
			mShader->setInt(uniform.mName, std::get<int>(mUniforms.at(uniform.mName))); break;
		}
		case ShaderReflection::UniformType::FLOAT: {
			mShader->setFloat(uniform.mName, std::get<float>(mUniforms.at(uniform.mName))); break;
		}
		case ShaderReflection::UniformType::VEC2: {
			mShader->setVec2(uniform.mName, std::get<MATH::Vector2f>(mUniforms.at(uniform.mName))); break;
		} break;
		case ShaderReflection::UniformType::VEC3: {
			mShader->setVec3(uniform.mName, std::get<MATH::Vector3f>(mUniforms.at(uniform.mName))); break;
		}
		case ShaderReflection::UniformType::VEC4: {
			mShader->setVec4(uniform.mName, std::get<MATH::Vector4f>(mUniforms.at(uniform.mName))); break;
		}
		case ShaderReflection::UniformType::MAT3: {
			mShader->setMat3(uniform.mName, std::get<MATH::Matrix3f>(mUniforms.at(uniform.mName))); break;
		}
		case ShaderReflection::UniformType::MAT4: {
			mShader->setMat4(uniform.mName, std::get<MATH::Matrix4f>(mUniforms.at(uniform.mName))); break;
		}
		case ShaderReflection::UniformType::STORAGE_BUFFER: {
			//TODO: material now support only set ssbo external
			//driver->mStorageBuffers[uniform.mBind] = mUniformBuffers[uniform.mName];
		} break;
		}
	}
}

void IKIGAI::RENDER::MaterialGl::bind(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) {
	mShader->bind();
	fillUniforms(defaultTexture, useTextures);
}

void IKIGAI::RENDER::MaterialGl::unbind() {
	mShader->unbind();
}

void IKIGAI::RENDER::MaterialGl::set(const std::string& name, const UniformData& data) {
	mUniforms.at(name) = data;
}

IKIGAI::RENDER::MaterialInterface::UniformData& IKIGAI::RENDER::MaterialGl::get(const std::string& name) {
	return mUniforms.at(name);
}

IKIGAI::RENDER::MaterialResource IKIGAI::RENDER::MaterialGl::getDescriptor() {
	MaterialResource d;
	d.NeedFileWatch = false;//TODO

	d.ShaderPath = mShader ? mShader->mPath : "";
	d.BackfaceCulling = isBackfaceCulling();
	d.Blendable = isBlendable();
	d.ColorWriting = isColorWriting();
	d.DepthFunc = getDepthFunc();
	d.DepthTest = isDepthTest();
	d.DepthWriting = isDepthWriting();
	d.FrontfaceCulling = isFrontFaceCulling();
	d.IsDeferred = isDeferred();
	d.GpuInstances = getGPUInstances();
	for (auto& [k, v] : mUniforms) {
		std::visit(overloaded{
		[&d, &k](std::shared_ptr<TextureInterface> arg) {
			d.Uniforms[k] = arg->getPath();
		},
		[&d, &k](auto& arg) {
			d.Uniforms[k] = arg;
		}}, v);
	}
	return d;
}
#endif
