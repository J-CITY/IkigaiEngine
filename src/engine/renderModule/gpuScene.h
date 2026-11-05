#pragma once
#include "instanceBatch.h"
#include "objects/boundingSphere.h"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace IKIGAI::RENDER {
class Frustum;
class MaterialInterface;
class MeshInterface;
class Renderer;
struct Drawable;

struct GpuSceneInstance {
	int Owner = -1;
	std::shared_ptr<MeshInterface> Mesh;
	std::shared_ptr<MaterialInterface> Material;
	InstanceTransform Transform;
	BoundingSphere ModelBounds;
	BoundingSphere MeshBounds;
};

struct GpuSceneSnapshot {
	bool UseBvhBounds = false;
	std::vector<GpuSceneInstance> Instances;
};

struct GpuSceneDraw {
	std::shared_ptr<StorageBufferInterface> Transforms;
	std::shared_ptr<StorageBufferInterface> Arguments;
};

class GpuScene {
public:
	GpuScene();
	~GpuScene();

	bool available(Renderer& renderer);
	void prepare(Renderer& renderer, std::span<const GpuSceneInstance> instances, const Frustum* frustum);
	bool contains(const Drawable& drawable) const;

	struct Draw {
		std::shared_ptr<MeshInterface> Mesh;
		std::shared_ptr<MaterialInterface> Material;
		GpuSceneDraw Buffers;
	};
	std::span<const Draw> draws() const;

private:
	struct State;
	std::unique_ptr<State> mState;
};
}
