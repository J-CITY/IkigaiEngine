#include "spriteBatcher.h"

#include "renderModule/backends/interface/atlasInterface.h"
#include "renderModule/backends/interface/shaderInterface.h"
#include "renderModule/backends/meshFactory.h"
#include "renderModule/render.h"
#include "resourceModule/materialManager.h"
#include "utilsModule/serviceManager.h"
#include "windowModule/window/window.h"

using namespace IKIGAI;
using namespace IKIGAI::ECS;

SpriteBatcher::SpriteBatcher() = default;

SpriteBatcher::~SpriteBatcher() = default;

void SpriteBatcher::Draw(const std::array<MATH::Vector4f, 6>& verts, const RENDER::AtlasRect& uv, MATH::Vector4f color,
	std::shared_ptr<RENDER::TextureInterface> texture, std::shared_ptr<RENDER::ShaderInterface> shader, bool is3D) {
	if (mTexture != texture || mShader != shader || mIs3D != is3D) {
		Flush();
		mTexture = texture;
		mShader = shader;
		mIs3D = is3D;
	}

	mVertexBuffer.push_back(Vertex({ verts[0].x, verts[0].y, verts[0].z }, { uv.mX, 1.0f - uv.mY }, MATH::Vector3f(), MATH::Vector3f(),
		MATH::Vector3f(), { -1, -1, -1, -1 }, color));
	mVertexBuffer.push_back(Vertex({ verts[1].x, verts[1].y, verts[1].z }, { uv.mX + uv.mW, 1.0f - uv.mY }, MATH::Vector3f(), MATH::Vector3f(),
		MATH::Vector3f(), { -1, -1, -1, -1 }, color));
	mVertexBuffer.push_back(Vertex({ verts[2].x, verts[2].y, verts[2].z }, { uv.mX, 1.0f - uv.mY - uv.mH }, MATH::Vector3f(), MATH::Vector3f(),
		MATH::Vector3f(), { -1, -1, -1, -1 }, color));

	mVertexBuffer.push_back(Vertex({ verts[3].x, verts[3].y, verts[3].z }, { uv.mX, 1.0f - uv.mY - uv.mH }, MATH::Vector3f(), MATH::Vector3f(),
		MATH::Vector3f(), { -1, -1, -1, -1 }, color));
	mVertexBuffer.push_back(Vertex({ verts[4].x, verts[4].y, verts[4].z }, { uv.mX + uv.mW, 1.0f - uv.mY - uv.mH }, MATH::Vector3f(),
		MATH::Vector3f(), MATH::Vector3f(), { -1, -1, -1, -1 }, color));
	mVertexBuffer.push_back(Vertex({ verts[5].x, verts[5].y, verts[5].z }, { uv.mX + uv.mW, 1.0f - uv.mY }, MATH::Vector3f(), MATH::Vector3f(),
		MATH::Vector3f(), { -1, -1, -1, -1 }, color));
}

void SpriteBatcher::Draw(const std::array<Vertex, 6>& verts, std::shared_ptr<RENDER::TextureInterface> texture,
	std::shared_ptr<RENDER::ShaderInterface> shader, bool is3D) {
	if (mTexture != texture || mShader != shader || mIs3D != is3D) {
		Flush();
		mTexture = texture;
		mShader = shader;
		mIs3D = is3D;
	}

	mVertexBuffer.push_back(verts[0]);
	mVertexBuffer.push_back(verts[1]);
	mVertexBuffer.push_back(verts[2]);
	mVertexBuffer.push_back(verts[3]);
	mVertexBuffer.push_back(verts[4]);
	mVertexBuffer.push_back(verts[5]);
}

void SpriteBatcher::Flush() {
	if (mVertexBuffer.empty() || !mTexture) {
		return;
	}

	auto& renderer = RESOURCES::ServiceManager::Get<RENDER::Renderer>();
	auto& loader = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>();
	const bool text = mShader && mShader->mPath.find("text") != std::string::npos;

	auto material = loader.loadResource(text ? "/materials/render/portable/text.mat" : "/materials/render/portable/gui.mat");

	const auto size = RESOURCES::ServiceManager::Get<WINDOW::Window>().getPixelSize();
	auto projection = MATH::Matrix4f::CreateOrthographic(0, size.x, size.y, 0, -1, 1);

	if (RENDER::DriverInterface::settings.backend != RENDER::RenderSettings::Backend::OPENGL) {
		auto c = MATH::Matrix4f::Identity;
		c(2, 2) = 0.5f;
		c(2, 3) = 0.5f;
		projection = c * projection;
	}
	renderer.setShader(material->getShader());
	material->bind(mTexture, true);
	renderer.setUniformBuffer("engine_Gui", renderer.createUniformBuffer<MATH::Matrix4f>(MATH::Matrix4f::Transpose(projection)));
	renderer.setTexture("Image", mTexture);
	renderer.setDepth({ RENDER::DepthFunction::ALWAYS, false });
	renderer.setCull(RENDER::CullFace::NONE);
	renderer.setBlending({ RENDER::BlendMode::SRC_ALPHA, RENDER::BlendMode::ONE_MINUS_SRC_ALPHA });
	std::vector<unsigned> indices;
	for (unsigned i = 0; i < mVertexBuffer.size(); ++i) {
		indices.push_back(i);
	}
	renderer.draw(RENDER::CreateMesh(mVertexBuffer, indices), RENDER::PrimitiveMode::TRIANGLES, 1);
	mVertexBuffer.clear();
}
