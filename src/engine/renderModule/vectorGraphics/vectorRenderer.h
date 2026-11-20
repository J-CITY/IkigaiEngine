#pragma once
#include "renderModule/backends/interface/renderEnums.h"
#include "renderModule/vertex.h"
#include "vectorGraphicsModule/text/fontResource.h"
#include <map>
#include <memory>

namespace IKIGAI::ECS {
class Object;
class ComponentBase;
}
namespace IKIGAI::RENDER {
class Renderer;
class TextureInterface;
class MeshInterface;
class ShaderInterface;
class UniformBufferInterface;
}
namespace IKIGAI::VECTOR {
struct SdfEffects {
	MATH::Vector4f metrics { }, outline { }, shadow { };
	bool operator==(const SdfEffects& b) const {
		return metrics == b.metrics && outline == b.outline && shadow == b.shadow;
	}
};
struct PreparedDraw {
	const Mesh* mesh = nullptr;
	std::shared_ptr<RENDER::TextureInterface> texture;
	bool sdf = false;
	SdfEffects effects;
};
// One ordered batch stream serves both existing GUI and new vector components.
class VectorRenderer {
public:
	struct Statistics {
		size_t geometryBuilds = 0, textLayouts = 0, atlasUploads = 0, meshUploads = 0, batches = 0;
	};
	Statistics statistics() const {
		return mStatistics;
	}
	void beginFrame(bool world = false);
	std::vector<PreparedDraw> prepare(ECS::Object&, float pixelsPerUnit, RENDER::Renderer&);
	void enqueue(std::vector<::IKIGAI::Vertex> vertices, const std::vector<unsigned>& indices, std::shared_ptr<RENDER::TextureInterface> texture,
		std::shared_ptr<RENDER::ShaderInterface> shader, const MATH::Matrix4f& projection, RENDER::Blending blend, RENDER::Depth depth,
		const RENDER::Scissor& clip, bool clipped, bool sdf = false, SdfEffects effects = { });
	void flush(RENDER::Renderer&);
	void flushPending(RENDER::Renderer& renderer) {
		if (!mBatches.empty()) {
			flush(renderer);
			mBatches.clear();
		}
	}
	size_t batchCount() const {
		return mBatches.size();
	}

private:
	struct ComponentCache {
		std::string key;
		Mesh mesh;
		TextLayout text;
		std::shared_ptr<FontResource> font;
		uint64_t seen = 0;
	};

	struct FontPage {
		uint64_t revision = 0;
		std::shared_ptr<RENDER::TextureInterface> texture;
	};

	struct FontCache {
		std::shared_ptr<FontResource> font;
		std::vector<FontPage> pages;
		uint64_t seen = 0, generation = 0;
	};

	struct Batch {
		std::vector<::IKIGAI::Vertex> vertices;
		std::vector<unsigned> indices;
		std::shared_ptr<RENDER::TextureInterface> texture;
		std::shared_ptr<RENDER::ShaderInterface> shader;
		MATH::Matrix4f projection;
		RENDER::Blending blend { RENDER::BlendMode::ONE, RENDER::BlendMode::ONE_MINUS_SRC_ALPHA };
		RENDER::Depth depth;
		RENDER::Scissor clip;
		bool clipped = false, sdf = false;
		SdfEffects effects;
	};

	struct BatchCache {
		uint64_t hash = 0;
		std::shared_ptr<RENDER::MeshInterface> mesh;
		std::shared_ptr<RENDER::UniformBufferInterface> projection, effects;
	};

	std::shared_ptr<RENDER::TextureInterface> pageTexture(const std::shared_ptr<FontResource>&, unsigned, RENDER::Renderer&);
	std::map<const ECS::ComponentBase*, ComponentCache> mComponents;
	std::map<const FontResource*, FontCache> mFonts;
	std::vector<Batch> mBatches;
	std::array<std::vector<BatchCache>, 2> mBatchCaches;
	bool mWorld = false;
	std::shared_ptr<RENDER::TextureInterface> mWhite;
	uint64_t mFrame = 0;
	Statistics mStatistics;
};
}
