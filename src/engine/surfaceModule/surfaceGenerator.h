#pragma once
#include "renderModule/vertex.h"
#include "surfaceCoverage.h"
#include "surfaceSettings.h"
#include <atomic>
#include <functional>

namespace IKIGAI::SURFACE {
struct Sample {
	MATH::Vector3f Position = {};
	MATH::Vector3f Normal = { 0, 1, 0 };
	float Slope = 0.0f;
	unsigned LayerId = 0;
	bool Submerged = false;
	Coverage Cover = {};
};
struct MeshPart {
	unsigned MaterialId = 0;
	std::vector<Vertex> Vertices = {};
	std::vector<unsigned> Indices = {};
};
struct Geometry {
	std::vector<MeshPart> Parts = {};
};
uint32_t hash(uint32_t value);
float random01(uint32_t value);
float height(const NoiseSettings& settings, float x, float z);
float height(const Settings& settings, float x, float z);
std::pair<float, float> heightBounds(const Settings& settings);
bool contains(const Settings& settings, float x, float z);
Sample sample(const Settings& settings, float x, float z, unsigned lod = 0);
// Geometry-only projection for repeated LOD/root queries; avoids coverage evaluation.
Sample project(const Settings& settings, float x, float z, unsigned lod = 0);
Geometry buildTile(const Settings& settings, unsigned tileX, unsigned tileZ, unsigned lod, const std::atomic_bool* cancellation = nullptr);
Geometry buildWater(const Settings& settings, unsigned tileX, unsigned tileZ);
using BlockReader = std::function<Block(int, int, int)>;
Block generatedBlock(const VoxelSettings& settings, int x, int y, int z);
Geometry buildVoxelChunk(const VoxelSettings& settings, unsigned chunkX, unsigned chunkY, unsigned chunkZ, const BlockReader& read);
MATH::Vector3f voxelOrigin(const VoxelSettings& settings);
}
