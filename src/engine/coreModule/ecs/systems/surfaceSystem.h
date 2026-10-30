#pragma once
#include "mathModule/math.h"
#include <memory>

namespace IKIGAI::RENDER {
struct RenderChunk;
class Frustum;
class MaterialInterface;
}
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::ECS {
class SurfaceComponent;
class ChunkModelRenderer;
class FoliageComponent;
class SurfaceSystem {
public:
	static void beginFrame(SCENE_SYSTEM::Scene& scene);
	static void append(SurfaceComponent& component, RENDER::RenderChunk& output, MATH::Vector3f camera, const RENDER::Frustum* frustum,
		std::shared_ptr<RENDER::MaterialInterface> fallback);
	static void append(ChunkModelRenderer& component, RENDER::RenderChunk& output, MATH::Vector3f camera, const RENDER::Frustum* frustum,
		std::shared_ptr<RENDER::MaterialInterface> fallback);
	static void append(SCENE_SYSTEM::Scene& scene, FoliageComponent& component, RENDER::RenderChunk& output, MATH::Vector3f camera,
		const RENDER::Frustum* frustum, std::shared_ptr<RENDER::MaterialInterface> fallback);
};
}
