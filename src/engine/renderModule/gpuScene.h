#pragma once
#include "instanceBatch.h"
#include "objects/boundingSphere.h"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace IKIGAI::RENDER {
class Frustum;
class MaterialInterface;
class MeshInterface;
class Renderer;
class TextureInterface;
struct Drawable;

struct GpuSceneInstance {
	int Owner = -1;
	std::shared_ptr<MeshInterface> Mesh;
	std::shared_ptr<MaterialInterface> Material;
	InstanceTransform Transform;
	BoundingSphere ModelBounds;
	BoundingSphere MeshBounds;
	uint32_t Lod = UINT32_MAX;
};

struct GpuSceneLod {
	static constexpr size_t MaxLevels = 4;
	int Owner = -1;
	MATH::Vector3f Position {};
	std::array<float, MaxLevels> Distances {};
	uint32_t Levels = 0;
	std::vector<GpuSceneInstance> Instances;
};

struct GpuSceneSnapshot {
	bool UseBvhBounds = false;
	std::vector<GpuSceneInstance> Instances;
	std::vector<GpuSceneLod> Lods;
};

struct GpuSceneDraw {
	std::shared_ptr<StorageBufferInterface> Transforms;
	std::shared_ptr<StorageBufferInterface> Arguments;
};

struct GpuSceneOcclusion {
	std::shared_ptr<TextureInterface> Hzb;
	std::shared_ptr<TextureInterface> Fallback;
	MATH::Matrix4f ViewProjection = MATH::Matrix4f::Identity;
	MATH::Matrix4f PreviousViewProjection = MATH::Matrix4f::Identity;
	bool HistoryValid = false;
};

class GpuScene {
public:
	GpuScene();
	~GpuScene();

	bool available(Renderer& renderer);
	void prepare(Renderer& renderer, const GpuSceneSnapshot& snapshot, const Frustum* frustum, const MATH::Vector3f& cameraPosition,
		const GpuSceneOcclusion& occlusion);
	bool recordOcclusionPost(Renderer& renderer, const std::shared_ptr<TextureInterface>& hzb);
	bool recordShadow(Renderer& renderer, const Frustum& frustum);
	bool contains(const Drawable& drawable) const;
	bool containsShadow(const Drawable& drawable) const;

	struct Draw {
		std::shared_ptr<MeshInterface> Mesh;
		std::shared_ptr<MaterialInterface> Material;
		GpuSceneDraw Buffers;
	};
	std::span<const Draw> draws() const;
	std::span<const Draw> shadowDraws() const;

private:
	struct State;
	std::unique_ptr<State> mState;
};
}
