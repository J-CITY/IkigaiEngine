#pragma once

#include <unordered_map>
#include <string>
#include <optional>

#include "reflectionStructs.h"
#include "mathModule/math.h"


namespace IKIGAI::RENDER {
	struct ShaderResource;
	class PushConstantInterface;
	class UniformBufferInterface;
	class UniformVkInterface;
	struct UniformInform;



	class ShaderInterface {
	public:
		using Id = size_t;

	protected:
		ShaderReflection mReflection;
		Id mId = 0;

	public:

		// Path to resource
		std::string mPath;
		std::map<ShaderType, std::string> mShaderPaths;

		inline const ShaderReflection& getReflection() const {
			return mReflection;
		}

		const std::string& getPath() { return mPath; }
		Id getId() const { return mId; }

		virtual ~ShaderInterface() = default;

		virtual void bind() = 0;
		virtual void unbind() = 0;

		virtual void recompile(const ShaderResource& res) = 0;

		static std::string ConstructRealPath(const std::string& path);
		static void GetReflection(ShaderReflection& reflection, const std::vector<uint32_t>& shaderCode, ShaderType type);
	};


	std::vector<uint32_t> CompileGlslToSpirv(IKIGAI::RENDER::ShaderType stage, const std::string& code, const std::vector<std::string>& defines);
	std::string CompileSpirvToHlsl(const std::vector<uint32_t>& spirv, uint32_t version);
	std::string CompileSpirvToGlsl(const std::vector<uint32_t>& spirv, bool es, uint32_t version, bool enable_420pack_extension, bool force_flattened_io_blocks);
}

