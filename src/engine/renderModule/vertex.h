#pragma once

#include <array>

#include "mathModule/math.h"


namespace IKIGAI {
	constexpr int MAX_BONE_INFLUENCE = 4;
	class Vertex {
	public:
		MATH::Vector3f position;
		MATH::Vector2f texCoord;
		MATH::Vector3f normal;
		MATH::Vector3f tangent;
		MATH::Vector3f bitangent;

		//bone indexes which will influence this vertex
		// WebGL2 has integer attribs (shader uses ivec4). GLES2 stored IDs as float.
#if (defined(USING_GLES) || defined(OPENGL_SIMPLE_RENDER)) && !defined(__EMSCRIPTEN__)
		std::array<float, 4> m_BoneIDs = {-1, -1, -1, -1};
#else
		std::array<int, 4> m_BoneIDs = {-1, -1, -1, -1};
#endif

		//weights from each bone
		//std::array<float, 4>  m_Weights = {0.0f,0.0f,0.0f,0.0f};
		MATH::Vector4f m_Weights = {0.0f,0.0f,0.0f,0.0f};

		Vertex() = default;

		Vertex(const MATH::Vector3f& position, const MATH::Vector2f& texCoord = MATH::Vector2f(0, 0),
			const  MATH::Vector3f& normal = MATH::Vector3f(0, 0, 0), const  MATH::Vector3f& tangent = MATH::Vector3f(0, 0, 0),
			const  MATH::Vector3f& bitangent = MATH::Vector3f(0, 0, 0), std::array<int, 4> boneIDs = {-1, -1, -1, -1}, const MATH::Vector4f& weights = {0.0f, 0.0f, 0.0f, 0.0f});
	};
}
