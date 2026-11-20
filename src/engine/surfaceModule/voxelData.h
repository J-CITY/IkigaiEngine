#pragma once
#include "surfaceSettings.h"
#include <unordered_map>

namespace IKIGAI::SURFACE {
class VoxelData {
public:
	explicit VoxelData(const VoxelSettings& settings);
	const VoxelSettings& settings() const;
	bool contains(int x, int y, int z) const;
	Block get(int x, int y, int z) const;
	bool set(int x, int y, int z, Block block);
	void reset();
	std::vector<VoxelEdit> edits() const;

private:
	uint64_t key(int x, int y, int z) const;
	VoxelSettings mSettings = {};
	std::unordered_map<uint64_t, Block> mEdits = {};
};
}
