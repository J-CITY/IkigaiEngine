#include "voxelData.h"
#include "surfaceGenerator.h"
#include <algorithm>
#include <stdexcept>

namespace IKIGAI::SURFACE {
VoxelData::VoxelData(const VoxelSettings& settings)
	: mSettings(settings) {
	settings.validate();
}
const VoxelSettings& VoxelData::settings() const {
	return mSettings;
}
bool VoxelData::contains(int x, int y, int z) const {
	return x >= 0 && y >= 0 && z >= 0 && x < int(mSettings.ChunksX * mSettings.ChunkSize) && y < int(mSettings.ChunksY * mSettings.ChunkSize)
		&& z < int(mSettings.ChunksZ * mSettings.ChunkSize);
}
uint64_t VoxelData::key(int x, int y, int z) const {
	return (uint64_t(x) * mSettings.ChunksY * mSettings.ChunkSize + uint64_t(y)) * mSettings.ChunksZ * mSettings.ChunkSize + uint64_t(z);
}
Block VoxelData::get(int x, int y, int z) const {
	if (!contains(x, y, z)) {
		return Block::Air;
	}
	const auto found = mEdits.find(key(x, y, z));
	return found == mEdits.end() ? generatedBlock(mSettings, x, y, z) : found->second;
}
bool VoxelData::set(int x, int y, int z, Block block) {
	if (!contains(x, y, z) || unsigned(block) > unsigned(Block::Water)) {
		throw std::out_of_range("Invalid voxel coordinate/block type");
	}
	if (get(x, y, z) == block) {
		return false;
	}
	const auto editKey = key(x, y, z);
	if (!mEdits.contains(editKey) && mEdits.size() >= 1000000) {
		throw std::runtime_error("Voxel edit budget exceeded");
	}
	if (block == generatedBlock(mSettings, x, y, z)) {
		mEdits.erase(editKey);
	}
	else {
		mEdits.insert_or_assign(editKey, block);
	}
	return true;
}
void VoxelData::reset() {
	mEdits.clear();
}
std::vector<VoxelEdit> VoxelData::edits() const {
	std::vector<std::pair<uint64_t, Block>> ordered(mEdits.begin(), mEdits.end());
	std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	std::vector<VoxelEdit> result;
	const uint64_t depth = mSettings.ChunksZ * mSettings.ChunkSize;
	const uint64_t height = mSettings.ChunksY * mSettings.ChunkSize;
	for (const auto& [key, block] : ordered) {
		result.push_back({ int(key / depth / height), int(key / depth % height), int(key % depth), block });
	}
	return result;
}
}
