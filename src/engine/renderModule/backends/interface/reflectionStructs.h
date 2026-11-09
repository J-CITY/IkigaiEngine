#pragma once
#include <string>
#include <vector>
#include "utilsModule/containers/internedNameMap.h"
#include <nlohmann/json.hpp>

#include "renderEnums.h"
#include "vertexLayout.h"

namespace IKIGAI::RENDER {
	enum class ShaderType: int {
		NONE = 0,
		VERTEX = 1,
		FRAGMENT = 2,
		GEOMETRY = 4,
		TESSELLATION_CONTROL = 8,
		TESSELLATION_EVALUATION = 16,
		COMPUTE = 32,
        RAYGEN = 64, MISS = 128, CLOSEST_HIT = 256, ANY_HIT = 512,
        INTERSECTION = 1024, CALLABLE = 2048
	};

	inline void from_json(const nlohmann::json& j, ShaderType& s) {
		const auto& str = j.get<std::string>();
		if      (str == "Vertex"              || str == "VERTEX")               s = ShaderType::VERTEX;
		else if (str == "Fragment"            || str == "FRAGMENT")             s = ShaderType::FRAGMENT;
		else if (str == "Geometry"            || str == "GEOMETRY")             s = ShaderType::GEOMETRY;
		else if (str == "TessellationControl" || str == "TESSELLATION_CONTROL") s = ShaderType::TESSELLATION_CONTROL;
		else if (str == "TessellationEvaluation" || str == "TESSELLATION_EVALUATION") s = ShaderType::TESSELLATION_EVALUATION;
		else if (str == "Compute"             || str == "COMPUTE")              s = ShaderType::COMPUTE;
        else if(str=="RAYGEN") s=ShaderType::RAYGEN;
        else if(str=="MISS") s=ShaderType::MISS;
        else if(str=="CLOSEST_HIT") s=ShaderType::CLOSEST_HIT;
        else if(str=="ANY_HIT") s=ShaderType::ANY_HIT;
        else if(str=="INTERSECTION") s=ShaderType::INTERSECTION;
        else if(str=="CALLABLE") s=ShaderType::CALLABLE;
		else s = ShaderType::NONE;
	}

	inline void to_json(nlohmann::json& j, const ShaderType& s) {
		switch (s) {
			case ShaderType::VERTEX:                 j = "Vertex"; break;
			case ShaderType::FRAGMENT:               j = "Fragment"; break;
			case ShaderType::GEOMETRY:               j = "Geometry"; break;
			case ShaderType::TESSELLATION_CONTROL:   j = "TessellationControl"; break;
			case ShaderType::TESSELLATION_EVALUATION:j = "TessellationEvaluation"; break;
			case ShaderType::COMPUTE:                j = "Compute"; break;
            case ShaderType::RAYGEN: j="RAYGEN"; break;
            case ShaderType::MISS: j="MISS"; break;
            case ShaderType::CLOSEST_HIT: j="CLOSEST_HIT"; break;
            case ShaderType::ANY_HIT: j="ANY_HIT"; break;
            case ShaderType::INTERSECTION: j="INTERSECTION"; break;
            case ShaderType::CALLABLE: j="CALLABLE"; break;
			default:                                 j = "None"; break;
		}
	}

	enum class UNIFORM_TYPE {
		NONE, MAT4, MAT3, VEC4, VEC3, VEC2, INT, FLOAT, BOOL, SAMPLER_2D,
#ifndef USING_GLES
		SAMPLER_3D,
#endif
		SAMPLER_CUBE,
#ifndef USING_GLES
		SAMPLER_2D_ARRAY,
		IMAGE_3D
#endif
	};
	struct UniformInform {
		enum class TYPE {
			NONE,
			UNIFORM,
			UNIFORM_SAMPLER2D,
			UNIFORM_BUFFER,
			STORAGE_BUFFER,
			SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
			SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE
		};
		struct Member {
			UNIFORM_TYPE type = UNIFORM_TYPE::NONE;
			std::string name;
			int offset = 0;
			int size = 0;
			int arraySize = 0;
		};
		TYPE type = TYPE::NONE;
		size_t binding = 0;
		size_t set = 0;
		int size = 0;
		unsigned shaderType = 0;
		std::string name;
		std::vector<Member> members;
	};

	struct ShaderReflection {
        uint32_t mLocalSize[3] = {1,1,1};
		struct InputParam {
			VertexScalarType mScalarType = VertexScalarType::Float;
			uint32_t mComponents = 0;
			uint32_t mBitWidth = 32;
			std::string mName;
			size_t mIndex = 0;
			size_t mLocation = 0;
			PixelFormat mFormat = PixelFormat::RGBA_FLOAT;
			size_t mOffset = 0;
			size_t mSize = 0;
		};
		enum class UniformType {
			NONE, MAT4, MAT3, VEC4, VEC3, VEC2, INT, FLOAT, BOOL,
			SAMPLER_2D,
			SAMPLER_CUBE,
			SAMPLER_3D,
			SAMPLER_2D_ARRAY,
			UNIFORM_BUFFER,
			STORAGE_BUFFER,
			IMAGE_2D,
			IMAGE_3D,
			IMAGE_2D_ARRAY,
			IMAGE_CUBE,
			PUSH_CONSTANT,
			//TODO: use bits and make TEXTURE mask
		};
		struct UniformMember {
			std::string mName;
			UniformType mType = UniformType::NONE;
			int mOffset = 0;
			int mSize = 0;
			int mArraySize = 0;
		};
		struct Uniform {
			uint32_t mDescriptorCount = 1;
			uint32_t mSamplerRootId = UINT32_MAX;
			bool mRuntimeArray = false;
			bool mReadOnly = false;
			bool mWriteOnly = false;
			std::string mName;
			UniformType mType = UniformType::NONE;
			size_t mRootId = 0;
			size_t mBind = 0;
			size_t mSet = 0;
			size_t mShaderMask = 0;
			size_t mSize = 0;
			std::vector<UniformMember> mMembers;

		};
		struct OutputParam {
			PixelFormat mFormat = PixelFormat::RGBA_FLOAT;
		};
		std::vector<InputParam> mInputParams;
		std::vector<OutputParam> mOutputParam;
		std::vector<Uniform> mUniforms;
		UTILS::InternedNameMap<size_t> mNameToUniforms;
	};
}
