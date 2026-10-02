#include "vertex.h"

using namespace IKIGAI;

Vertex::Vertex(const MATH::Vector3f& position, const MATH::Vector2f& texCoord, const MATH::Vector3f& normal,
	const MATH::Vector3f& tangent, const MATH::Vector3f& bitangent, std::array<int, 4> boneIDs, const MATH::Vector4f& weights)
{
	this->position = position;
	this->texCoord = texCoord;
	this->normal = normal;
	this->tangent = tangent;
	this->bitangent = bitangent;
#if (defined(USING_GLES) || defined(OPENGL_SIMPLE_RENDER)) && !defined(__EMSCRIPTEN__)
	m_BoneIDs = { (float)boneIDs[0], (float)boneIDs[1], (float)boneIDs[2], (float)boneIDs[3]};
#else
	m_BoneIDs = boneIDs;
#endif
	m_Weights = weights;
}
