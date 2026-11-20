#pragma once

#include <memory>
#include <vector>

#include "renderModule/drawable.h"

namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}

namespace IKIGAI::RENDER {
class Camera;
class Frustum;
class MeshInterface;
class ModelInterface;
enum class CullingOptions;
struct BoundingSphere;
struct GpuSceneSnapshot;
struct LightOGL;

// Parallel bypasses the size threshold when a worker pool is available.
enum class FrustumCullMode { Automatic, Serial, Parallel };

class SceneRenderData {
public:
	SceneRenderData();
	~SceneRenderData();
	SceneRenderData(const SceneRenderData&) = delete;
	SceneRenderData& operator=(const SceneRenderData&) = delete;

	static std::vector<std::shared_ptr<RENDER::MeshInterface>> getMeshesInFrustum(const RENDER::ModelInterface& model,
		const RENDER::BoundingSphere& modelBoundingSphere, const ECS::Transform& modelTransform, const RENDER::Frustum& frustum,
		RENDER::CullingOptions cullingOptions);

	std::vector<IKIGAI::RENDER::RenderChunk> findDrawables(SCENE_SYSTEM::Scene& scene, const MATH::Vector3f& p_cameraPosition,
		const RENDER::Camera& p_camera, const RENDER::Frustum* p_customFrustum, std::shared_ptr<RENDER::MaterialInterface> p_defaultMaterial,
		std::shared_ptr<MEMORY::IAllocator> allocationDomain = {});

	std::vector<IKIGAI::RENDER::RenderChunk> findAndSortFrustumCulledBVHDrawables(SCENE_SYSTEM::Scene& scene, const MATH::Vector3f& cameraPosition,
		const RENDER::Frustum& frustum, std::shared_ptr<RENDER::MaterialInterface> defaultMaterial,
		std::shared_ptr<MEMORY::IAllocator> allocationDomain = {});

	std::vector<IKIGAI::RENDER::RenderChunk> findAndSortFrustumCulledDrawables(SCENE_SYSTEM::Scene& scene, const MATH::Vector3f& cameraPosition,
		const RENDER::Frustum& frustum, std::shared_ptr<RENDER::MaterialInterface> defaultMaterial, FrustumCullMode mode = FrustumCullMode::Automatic,
		std::shared_ptr<MEMORY::IAllocator> allocationDomain = {});

	std::vector<IKIGAI::RENDER::RenderChunk> findAndSortDrawables(SCENE_SYSTEM::Scene& scene, const MATH::Vector3f& cameraPosition,
		std::shared_ptr<RENDER::MaterialInterface> defaultMaterial, RENDER::GpuSceneSnapshot* gpuScene = nullptr,
		std::shared_ptr<MEMORY::IAllocator> allocationDomain = {});

	static std::vector<RENDER::LightOGL> findLightData(const SCENE_SYSTEM::Scene& scene, std::vector<int>* objectIds = nullptr);
	static std::vector<RENDER::LightOGL> findLightDataInFrustum(
		const SCENE_SYSTEM::Scene& scene, const RENDER::Frustum& frustum, std::vector<int>* objectIds = nullptr);

private:
	struct SpatialIndex;
	std::unique_ptr<SpatialIndex> mSpatial;
	SpatialIndex& spatial(SCENE_SYSTEM::Scene& scene);
};
}
