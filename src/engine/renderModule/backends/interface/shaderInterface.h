#pragma once
#include "renderResourceOwner.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "bindlessInterface.h"
#include "mathModule/math.h"
#include "reflectionStructs.h"

namespace IKIGAI::RENDER {
struct ShaderResource;
class PushConstantInterface;
class UniformBufferInterface;
class UniformVkInterface;
struct UniformInform;

class ShaderInterface : protected RenderResourceOwner {
public:
	using Id = size_t;

protected:
	ShaderReflection mReflection;
	Id mId = 0;
	ShaderBindingMode mBindingMode = ShaderBindingMode::Bound;
	VertexFetchMode mVertexFetchMode = VertexFetchMode::Attributes;
	uint32_t mBindlessCapacity = 64;
	uint64_t mGeneration = 0;
	bool mSupportsPbrLayers = false;
	unsigned mCompiledPbrLayerCount = 0;
	std::string mPbrLayerVariant;
	void swapCompiledInterface(ShaderInterface& replacement);

public:
	// Path to resource
	std::string mPath;
	std::map<ShaderType, std::string> mShaderPaths;
	std::unordered_set<std::string> mSourceDependencies;

	inline const ShaderReflection& getReflection() const {
		return mReflection;
	}

	const std::string& getPath() {
		return mPath;
	}
	Id getId() const {
		return mId;
	}
	ShaderBindingMode bindingMode() const {
		return mBindingMode;
	}
	VertexFetchMode vertexFetchMode() const {
		return mVertexFetchMode;
	}
	uint32_t bindlessCapacity() const {
		return mBindlessCapacity;
	}
	uint64_t generation() const {
		return mGeneration;
	}
	bool supportsPbrLayers() const {
		return mSupportsPbrLayers;
	}
	unsigned compiledPbrLayerCount() const {
		return mCompiledPbrLayerCount;
	}
	const std::string& pbrLayerVariant() const {
		return mPbrLayerVariant;
	}
	void configureResourceInterface(const ShaderResource& res);
	bool isBindlessResource(const ShaderReflection::Uniform& uniform) const {
		return mBindingMode == ShaderBindingMode::Bindless && (uniform.mRuntimeArray || uniform.mDescriptorCount > 1);
	}
	uint32_t descriptorCount(const ShaderReflection::Uniform& uniform) const {
		return uniform.mRuntimeArray ? mBindlessCapacity : uniform.mDescriptorCount;
	}

	virtual ~ShaderInterface() = default;

	virtual void bind() = 0;
	virtual void unbind() = 0;

	virtual void recompile(const ShaderResource& res) = 0;

	static void GetReflection(ShaderReflection& reflection, const std::vector<uint32_t>& shaderCode, ShaderType type);
};

std::vector<uint32_t> CompileGlslToSpirv(IKIGAI::RENDER::ShaderType stage, const std::string& code, const std::vector<std::string>& defines);
std::string CompileSpirvToHlsl(const std::vector<uint32_t>& spirv, uint32_t version);
std::string CompileSpirvToGlsl(
	const std::vector<uint32_t>& spirv, bool es, uint32_t version, bool enable_420pack_extension, bool force_flattened_io_blocks);
}
