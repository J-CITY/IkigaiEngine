#pragma once

#include <memory>

#include "mathModule/math.h"
#include "renderEnums.h"
#include "resourceStruct.h"
#include "shaderInterface.h"

namespace IKIGAI::RENDER {
class TextureInterface;

class MaterialInterface {
protected:
	std::string mPath;
	std::string mParentPath;
	bool mNeedFileWatch = false;

	bool mBlendable = false;
	bool mBackfaceCulling = true;
	bool mFrontfaceCulling = false;
	bool mDepthTest = true;
	bool mDepthWriting = true;
	bool mColorWriting = true;
	unsigned mGpuInstances = 1;
	bool mIsDeferred = false;

	bool mIsCastShadow = true;
	bool mIsBakedShadow = false;

	DepthFunction mDepthFunc = DepthFunction::LESS;

public:
	using UniformData = std::variant<float, int, bool, MATH::Vector2f, MATH::Vector3f, MATH::Vector4f, MATH::Matrix3f, MATH::Matrix4f,
		std::shared_ptr<TextureInterface>>;

protected:
	struct PreparedMaterial {
		std::shared_ptr<ShaderInterface> shader;
		std::map<std::string, UniformData> uniforms;
	};
	PbrLayersDescriptor mPbrLayers;
	PbrEmissiveDescriptor mEmissive;
	DeformationDescriptor mDeformation = {};
	PreparedMaterial prepareMaterial(const MaterialResource& resource);
	void applyMaterialState(const MaterialResource& resource);
	void completeDescriptor(MaterialResource& resource) const;

public:
	const DeformationDescriptor& deformation() const {
		return mDeformation;
	}
	const PbrLayersDescriptor& pbrLayers() const {
		return mPbrLayers;
	}
	const PbrEmissiveDescriptor& emissive() const {
		return mEmissive;
	}

	MaterialInterface() = default;
	MaterialInterface(const MaterialResource& res);

	virtual ~MaterialInterface() = default;

	virtual void bind(std::shared_ptr<TextureInterface> defaultTexture, bool useTextures) = 0;
	virtual void unbind() = 0;

	virtual std::shared_ptr<ShaderInterface> getShader() const = 0;
	virtual void setShader(std::shared_ptr<ShaderInterface> shader) = 0;
	virtual bool hasShader() const = 0;

	// void setIsDeferred(bool v, std::shared_ptr<ShaderInterface> shader) = 0;

	virtual MaterialResource getDescriptor() = 0;

	virtual void set(const std::string& name, const UniformData& data) = 0;
	BindlessResourceHandle setBindlessTexture(
		const std::string& tableName, const std::string& indexUniform, std::shared_ptr<TextureInterface> texture);
	virtual UniformData& get(const std::string& name) = 0;
	// Optional material parameters are common across render passes.
	// Return nullptr when absent without throwing or inserting a value.
	virtual const UniformData* findUniform(const std::string& name) const noexcept = 0;

	virtual void create(const MaterialResource& res) = 0;

	unsigned getGPUInstances() const {
		return mGpuInstances;
	}

	bool isDeferred() const {
		return mIsDeferred;
	}

	bool isBlendable() const {
		return mBlendable;
	}

	bool isCastShadow() const {
		return mIsCastShadow;
	}

	bool isShadowBaked() const {
		return mIsBakedShadow;
	}

	bool isBackfaceCulling() const {
		return mBackfaceCulling;
	}

	bool isFrontFaceCulling() const {
		return mFrontfaceCulling;
	}

	bool isDepthTest() const {
		return mDepthTest;
	}

	bool isDepthWriting() const {
		return mDepthWriting;
	}

	bool isColorWriting() const {
		return mColorWriting;
	}

	void setBlendable(bool v) {
		mBlendable = v;
	}

	void setBackfaceCulling(bool v) {
		mBackfaceCulling = v;
	}

	void setFrontfaceCulling(bool v) {
		mFrontfaceCulling = v;
	}

	void setDepthTest(bool v) {
		mDepthTest = v;
	}

	void setDepthWriting(bool v) {
		mDepthWriting = v;
	}

	void setColorWriting(bool v) {
		mColorWriting = v;
	}

	void setGpuInstances(unsigned v) {
		mGpuInstances = v;
	}

	void setIsDeferred(bool v) {
		mIsDeferred = v;
	}

	void setIsCastShadow(bool v) {
		mIsCastShadow = v;
	}

	void setIsBakedShadow(bool v) {
		mIsBakedShadow = v;
	}

	DepthFunction getDepthFunc() const {
		return mDepthFunc;
	}

	bool isEngineUniform(const std::string& uniformName) const {
		return uniformName.find("engine_", 0) == 0 || uniformName.find("Engine", 0) == 0;
	}

	const std::string& getPath() const {
		return mPath;
	}
};
}
