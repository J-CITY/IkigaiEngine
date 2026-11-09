#pragma once
#include "storageBufferInterface.h"
#include "textureInterface.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace IKIGAI::RENDER {
enum class ShaderBindingMode { Bound, Bindless };
enum class VertexFetchMode { Attributes, Pulling };
struct BindlessCapabilities {
	bool sampledTextures = false;
	bool storageBuffers = false;
	bool storageImages = false;
	bool nonUniformIndexing = false;
	bool runtimeArrays = false;
	uint32_t maxSampledTextures = 0;
	uint32_t maxStorageBuffers = 0;
};
struct BindlessResourceHandle {
	uint32_t binding = 0;
	uint32_t index = 0;
	uint32_t generation = 0;
};
struct BindlessResourceEntry {
	std::shared_ptr<TextureInterface> texture;
	std::shared_ptr<StorageBufferInterface> buffer;
	uint32_t generation = 1;
};
// A binding has one resource type and a fixed capacity. Slot zero is its default.
// Released indices must be removed from all future material and GPU records:
// generations validate CPU handles, but do not protect bare indices in GPU data.
struct BindlessResourceTable {
	uint32_t capacity = 0;
	std::vector<BindlessResourceEntry> entries;
	uint64_t revision = 0;
	std::vector<uint32_t> reusableIndices;

	BindlessResourceHandle add(uint32_t binding, BindlessResourceEntry value, uint32_t limit);
	const BindlessResourceEntry& get(uint32_t index) const;
	void release(BindlessResourceHandle handle);

private:
	friend class DriverInterface;
	// Call only after all CPU snapshots and GPU uses of the old index have retired.
	void recycle(BindlessResourceHandle handle);
};
}
