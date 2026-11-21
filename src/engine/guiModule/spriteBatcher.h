#pragma once

#include "renderModule/vertex.h"
#include <array>
#include <memory>
#include <vector>

namespace IKIGAI::RENDER {
struct AtlasRect;
class ShaderInterface;
class TextureInterface;
class VertexArray;
class VertexBufferInterface;
}

namespace IKIGAI::ECS {
struct BatchVertex {
	MATH::Vector3f position;
	MATH::Vector2f texCoord;
	MATH::Vector4f color;
};

class SpriteBatcher {

public:
	SpriteBatcher();
	~SpriteBatcher();

	void Draw(const std::array<MATH::Vector4f, 6>& verts, const RENDER::AtlasRect& uv, MATH::Vector4f color,
		std::shared_ptr<RENDER::TextureInterface> texture, std::shared_ptr<RENDER::ShaderInterface> shader, bool is3D = false);
	void Draw(const std::array<Vertex, 6>& verts, std::shared_ptr<RENDER::TextureInterface> texture, std::shared_ptr<RENDER::ShaderInterface> shader,
		bool is3D = false);
	void Flush();

private:
	// Collection of vertices
	std::vector<Vertex> mVertexBuffer;

	std::shared_ptr<RENDER::VertexBufferInterface> mVbo;
	std::shared_ptr<RENDER::VertexArray> mVertexArray;

	// unsigned m_vbo = 0;

	// Shaders.
	// Shader m_vertexShader;
	// Shader m_fragmentShader;

	// GL index for the shader program
	// unsigned m_shaderProgram = 0;

	// Texture and uniform location
	std::shared_ptr<RENDER::TextureInterface> mTexture;
	std::shared_ptr<RENDER::ShaderInterface> mShader;
	bool mIs3D = false;
	unsigned m_textureUniform = 0;

	// stuff to send word matrices
	// MATHGL::Matrix4 m_screenTransform;
	// unsigned m_screenTransformUniform = 0;
};
}
