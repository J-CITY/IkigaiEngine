#pragma once

#include "backends/interface/bindlessInterface.h"
#include "backends/interface/materialInterface.h"
#include "gpuScene.h"

namespace IKIGAI::RENDER {
class GpuSceneMaterials {
public:
	explicit GpuSceneMaterials(Renderer& renderer, MaterialInterface& geometryPass);
	GpuSceneMaterialIds update(Renderer& renderer, const GpuSceneSnapshot& snapshot,
		const std::map<std::string, MaterialInterface::UniformData>& defaults, const std::shared_ptr<TextureInterface>& fallback);
	void clear(Renderer& renderer);
	void loadGeometryPass(Renderer& renderer, MaterialInterface& geometryPass);
	void bind(Renderer& renderer, const std::shared_ptr<StorageBufferInterface>& materialIds) const;
	const std::shared_ptr<MaterialInterface>& pass(bool pooledGeometry) const {
		return pooledGeometry ? mGeometryPass : mPass;
	}

private:
	struct alignas(16) Row {
		std::array<float, 4> Albedo {};
		std::array<float, 4> UvTransform {};
		std::array<float, 4> Surface {};
		std::array<uint32_t, 4> Textures {};
		bool operator==(const Row&) const = default;
	};
	static_assert(sizeof(Row) == 64);

	struct TextureSlot {
		std::shared_ptr<TextureInterface> Texture;
		BindlessResourceHandle Handle {};
	};
	void resolveBindings();

	std::shared_ptr<MaterialInterface> mPass;
	std::shared_ptr<MaterialInterface> mGeometryPass;
	std::shared_ptr<StorageBufferInterface> mRowsBuffer;
	std::vector<Row> mRows;
	std::map<const TextureInterface*, TextureSlot> mTextureSlots;
	size_t mIdsBinding = 0;
	size_t mRowsBinding = 0;
	uint32_t mTexturesBinding = 0;
	uint32_t mTextureCapacity = 0;
	uint64_t mGeneration = 0;
	bool mFallbackRegistered = false;
};
}
