#include "surfaceGenerator.h"
#include "surfaceJob.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace IKIGAI::SURFACE {
uint32_t hash(uint32_t value) {
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ (value >> 16);
}
float random01(uint32_t value) {
	return float(hash(value) >> 8) / 16777216.0f;
}
namespace {
	float noise(float x, float z, uint32_t seed) {
		const int cellX = int(std::floor(x));
		const int cellZ = int(std::floor(z));
		const float fractionX = x - float(cellX);
		const float fractionZ = z - float(cellZ);
		const float blendX = fractionX * fractionX * (3 - 2 * fractionX);
		const float blendZ = fractionZ * fractionZ * (3 - 2 * fractionZ);
		auto corner = [&](int dx, int dz) { return random01(uint32_t(cellX + dx) * 73856093u ^ uint32_t(cellZ + dz) * 19349663u ^ seed) * 2 - 1; };
		return std::lerp(std::lerp(corner(0, 0), corner(1, 0), blendX), std::lerp(corner(0, 1), corner(1, 1), blendX), blendZ);
	}
	MATH::Vector3f normal(const Settings& settings, float x, float z) {
		const float step = settings.TileSize / float(settings.Resolution);
		return MATH::Vector3f::Normalize({ height(settings, x - step, z) - height(settings, x + step, z), 2 * step,
			height(settings, x, z - step) - height(settings, x, z + step) });
	}
	unsigned classify(const Settings& settings, float elevation, float slope) {
		unsigned result = 0;
		for (unsigned index = 1; index < settings.Layers.size(); ++index) {
			const auto& layer = settings.Layers[index];
			if (elevation >= layer.MinHeight && elevation <= layer.MaxHeight && slope >= layer.MinSlope && slope <= layer.MaxSlope) {
				result = index;
			}
		}
		return result;
	}
	Vertex vertex(const Settings& settings, float x, float z) {
		const auto surfaceNormal = normal(settings, x, z);
		const auto tangent = MATH::Vector3f::Normalize({ surfaceNormal.y, -surfaceNormal.x, 0 });
		return { { x, height(settings, x, z), z }, { x, z }, surfaceNormal, tangent, MATH::Vector3f::Cross(surfaceNormal, tangent) };
	}
	void triangle(MeshPart& part, Vertex a, Vertex b, Vertex c, float textureScale) {
		const unsigned offset = unsigned(part.Vertices.size());
		for (auto value : { a, b, c }) {
			value.texCoord *= textureScale;
			part.Vertices.push_back(value);
		}
		part.Indices.insert(part.Indices.end(), { offset, offset + 1, offset + 2 });
	}
	void terrainTriangle(const Settings& settings, Geometry& geometry, const Vertex& a, const Vertex& b, const Vertex& c) {
		if (settings.Blend.Enabled) {
			triangle(geometry.Parts[0], a, b, c, 1.0f);
			return;
		}
		const auto center = (a.position + b.position + c.position) / 3.0f;
		const auto surfaceNormal = MATH::Vector3f::Normalize(a.normal + b.normal + c.normal);
		const float slope = MATH::TO_DEGREES(std::acos(std::clamp(surfaceNormal.y, -1.0f, 1.0f)));
		const unsigned layer = classify(settings, center.y, slope);
		triangle(geometry.Parts[layer], a, b, c, settings.Layers[layer].TextureScale);
	}
}
float height(const NoiseSettings& settings, float x, float z) {
	x *= settings.Frequency;
	z *= settings.Frequency;
	const float warpX = noise(x, z, settings.Seed ^ 0x1357u) * settings.Warp * settings.Frequency;
	const float warpZ = noise(x, z, settings.Seed ^ 0x2468u) * settings.Warp * settings.Frequency;
	x += warpX;
	z += warpZ;
	float sum = 0;
	float weight = 1;
	float totalWeight = 0;
	for (unsigned octave = 0; octave < settings.Octaves; ++octave) {
		const float value = noise(x, z, hash(settings.Seed + octave));
		sum += std::lerp(value, 1 - 2 * std::abs(value), settings.Ridge) * weight;
		totalWeight += weight;
		weight *= settings.Persistence;
		x *= settings.Lacunarity;
		z *= settings.Lacunarity;
	}
	float elevation = settings.BaseHeight + settings.Amplitude * sum / totalWeight;
	if (settings.TerraceHeight > 0) {
		elevation = std::floor(elevation / settings.TerraceHeight) * settings.TerraceHeight;
	}
	return elevation;
}
float height(const Settings& settings, float x, float z) {
	float result = height(settings.Noise, x, z);
	for (const auto& edit : settings.Edits) {
		if (!edit.Enabled || edit.Opacity == 0) {
			continue;
		}
		for (const auto& stroke : edit.Strokes) {
			if (stroke.Operation == BrushOperation::AddHeight || stroke.Operation == BrushOperation::FlattenHeight) {
				const float weight = edit.Opacity * brushInfluence(stroke, x, z);
				result = stroke.Operation == BrushOperation::AddHeight ? result + weight * stroke.Value : std::lerp(result, stroke.Value, weight);
			}
		}
	}
	return result;
}
std::pair<float, float> heightBounds(const Settings& settings) {
	float low = settings.Noise.BaseHeight - settings.Noise.Amplitude - settings.Noise.TerraceHeight;
	float high = settings.Noise.BaseHeight + settings.Noise.Amplitude;
	for (const auto& edit : settings.Edits) {
		if (!edit.Enabled || edit.Opacity == 0) {
			continue;
		}
		for (const auto& stroke : edit.Strokes) {
			if (stroke.Operation == BrushOperation::AddHeight) {
				const float delta = stroke.Value * stroke.Strength * edit.Opacity;
				low += std::min(0.0f, delta);
				high += std::max(0.0f, delta);
			}
			else if (stroke.Operation == BrushOperation::FlattenHeight) {
				low = std::min(low, stroke.Value);
				high = std::max(high, stroke.Value);
			}
		}
	}
	return { std::min(low - settings.SkirtDepth, settings.Water.Enabled ? settings.Water.Level : low),
		std::max(high, settings.Water.Enabled ? settings.Water.Level : high) };
}
bool contains(const Settings& settings, float x, float z) {
	return std::isfinite(x) && std::isfinite(z) && std::abs(x) <= settings.TilesX * settings.TileSize * .5f
		&& std::abs(z) <= settings.TilesZ * settings.TileSize * .5f;
}
Sample project(const Settings& settings, float x, float z, unsigned lod) {
	if (!contains(settings, x, z) || lod > settings.LodDistances.size()) {
		throw std::out_of_range("Surface sample is outside the surface");
	}
	const unsigned resolution = settings.Resolution >> lod;
	const float step = settings.TileSize / float(resolution);
	const float originX = -float(settings.TilesX) * settings.TileSize * .5f;
	const float originZ = -float(settings.TilesZ) * settings.TileSize * .5f;
	const float gridX = (x - originX) / step;
	const float gridZ = (z - originZ) / step;
	const float cellX = std::min(std::floor(gridX), float(settings.TilesX * resolution - 1));
	const float cellZ = std::min(std::floor(gridZ), float(settings.TilesZ * resolution - 1));
	const float fractionX = gridX - cellX;
	const float fractionZ = gridZ - cellZ;
	const float startX = originX + cellX * step;
	const float startZ = originZ + cellZ * step;
	const auto a = vertex(settings, startX, startZ);
	const auto b = vertex(settings, startX, startZ + step);
	const auto c = vertex(settings, startX + step, startZ);
	const auto d = vertex(settings, startX + step, startZ + step);
	Sample result;
	MATH::Vector3f center;
	MATH::Vector3f centerNormal;
	if (fractionX + fractionZ <= 1) {
		result.Position = a.position * (1 - fractionX - fractionZ) + b.position * fractionZ + c.position * fractionX;
		result.Normal = MATH::Vector3f::Normalize(a.normal * (1 - fractionX - fractionZ) + b.normal * fractionZ + c.normal * fractionX);
		center = (a.position + b.position + c.position) / 3.0f;
		centerNormal = MATH::Vector3f::Normalize(a.normal + b.normal + c.normal);
	}
	else {
		result.Position = d.position * (fractionX + fractionZ - 1) + c.position * (1 - fractionZ) + b.position * (1 - fractionX);
		result.Normal = MATH::Vector3f::Normalize(d.normal * (fractionX + fractionZ - 1) + c.normal * (1 - fractionZ) + b.normal * (1 - fractionX));
		center = (d.position + c.position + b.position) / 3.0f;
		centerNormal = MATH::Vector3f::Normalize(d.normal + c.normal + b.normal);
	}
	result.Slope = MATH::TO_DEGREES(std::acos(std::clamp(result.Normal.y, -1.0f, 1.0f)));
	result.LayerId = classify(settings, center.y, MATH::TO_DEGREES(std::acos(std::clamp(centerNormal.y, -1.0f, 1.0f))));
	result.Submerged = settings.Water.Enabled && result.Position.y <= settings.Water.Level;
	return result;
}
Sample sample(const Settings& settings, float x, float z, unsigned lod) {
	auto result = project(settings, x, z, lod);
	if (settings.Blend.Enabled || !settings.Edits.empty()) {
		result.Cover = coverage(settings, x, z);
		if (settings.Blend.Enabled) {
			result.LayerId = unsigned(std::max_element(result.Cover.Weights.begin(), result.Cover.Weights.end()) - result.Cover.Weights.begin());
		}
	}
	return result;
}
Geometry buildTile(const Settings& settings, unsigned tileX, unsigned tileZ, unsigned lod, const std::atomic_bool* cancellation) {
	if (tileX >= settings.TilesX || tileZ >= settings.TilesZ || lod > settings.LodDistances.size()) {
		throw std::out_of_range("Invalid surface tile/LOD");
	}
	Geometry result;
	for (unsigned layer = 0; layer < (settings.Blend.Enabled ? 1u : unsigned(settings.Layers.size())); ++layer) {
		result.Parts.push_back({ layer });
	}
	const unsigned resolution = settings.Resolution >> lod;
	const float step = settings.TileSize / float(resolution);
	const float originX = (float(tileX) - settings.TilesX * .5f) * settings.TileSize;
	const float originZ = (float(tileZ) - settings.TilesZ * .5f) * settings.TileSize;
	for (unsigned z = 0; z < resolution; ++z) {
		checkBuildCancellation(cancellation);
		for (unsigned x = 0; x < resolution; ++x) {
			const auto a = vertex(settings, originX + x * step, originZ + z * step);
			const auto b = vertex(settings, originX + x * step, originZ + (z + 1) * step);
			const auto c = vertex(settings, originX + (x + 1) * step, originZ + z * step);
			const auto d = vertex(settings, originX + (x + 1) * step, originZ + (z + 1) * step);
			terrainTriangle(settings, result, a, b, c);
			terrainTriangle(settings, result, d, c, b);
		}
	}
	// Vertical skirts hide cracks where adjacent tiles select different LODs.
	if (settings.SkirtDepth > 0) {
		for (unsigned edge = 0; edge < 4; ++edge) {
			for (unsigned index = 0; index < resolution; ++index) {
				const std::array<MATH::Vector2f, 4> starts
					= { MATH::Vector2f { originX, originZ + index * step }, { originX + index * step, originZ + settings.TileSize },
						  { originX + settings.TileSize, originZ + (index + 1) * step }, { originX + (index + 1) * step, originZ } };
				const std::array<MATH::Vector2f, 4> directions = { MATH::Vector2f { 0, step }, { step, 0 }, { 0, -step }, { -step, 0 } };
				const auto start = starts[edge];
				const auto end = start + directions[edge];
				auto a = vertex(settings, start.x, start.y);
				auto b = vertex(settings, end.x, end.y);
				auto c = a;
				auto d = b;
				c.position.y -= settings.SkirtDepth;
				d.position.y -= settings.SkirtDepth;
				const auto topNormal = MATH::Vector3f::Normalize(a.normal + b.normal);
				const unsigned layer = settings.Blend.Enabled
					? 0u
					: classify(settings, (a.position.y + b.position.y) * .5f, MATH::TO_DEGREES(std::acos(std::clamp(topNormal.y, -1.0f, 1.0f))));
				const auto sideNormal = MATH::Vector3f::Normalize(MATH::Vector3f::Cross(c.position - a.position, b.position - a.position));
				const auto sideTangent = MATH::Vector3f::Normalize(b.position - a.position);
				for (auto* value : { &a, &b, &c, &d }) {
					value->normal = sideNormal;
					value->tangent = sideTangent;
					value->bitangent = MATH::Vector3f::Cross(sideNormal, sideTangent);
					if (!settings.Blend.Enabled) {
						value->texCoord = { (value == &b || value == &d) ? (index + 1) * step : index * step, value->position.y };
					}
				}
				triangle(result.Parts[layer], a, c, b, settings.Blend.Enabled ? 1.0f : settings.Layers[layer].TextureScale);
				triangle(result.Parts[layer], b, c, d, settings.Blend.Enabled ? 1.0f : settings.Layers[layer].TextureScale);
			}
		}
	}
	return result;
}
Geometry buildWater(const Settings& settings, unsigned tileX, unsigned tileZ) {
	Geometry result;
	result.Parts.push_back({ unsigned(settings.Layers.size()) });
	if (!settings.Water.Enabled) {
		return result;
	}
	const float startX = (float(tileX) - settings.TilesX * .5f) * settings.TileSize;
	const float startZ = (float(tileZ) - settings.TilesZ * .5f) * settings.TileSize;
	auto waterVertex
		= [&](float x, float z) -> Vertex { return { { x, settings.Water.Level, z }, { x, z }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } }; };
	const auto a = waterVertex(startX, startZ);
	const auto b = waterVertex(startX, startZ + settings.TileSize);
	const auto c = waterVertex(startX + settings.TileSize, startZ);
	const auto d = waterVertex(startX + settings.TileSize, startZ + settings.TileSize);
	triangle(result.Parts[0], a, b, c, settings.Water.TextureScale);
	triangle(result.Parts[0], d, c, b, settings.Water.TextureScale);
	return result;
}
MATH::Vector3f voxelOrigin(const VoxelSettings& settings) {
	return { -float(settings.ChunksX * settings.ChunkSize) * settings.BlockSize * .5f,
		-float(settings.ChunksY * settings.ChunkSize) * settings.BlockSize * .5f,
		-float(settings.ChunksZ * settings.ChunkSize) * settings.BlockSize * .5f };
}
Block generatedBlock(const VoxelSettings& settings, int x, int y, int z) {
	if (x < 0 || y < 0 || z < 0 || x >= int(settings.ChunksX * settings.ChunkSize) || y >= int(settings.ChunksY * settings.ChunkSize)
		|| z >= int(settings.ChunksZ * settings.ChunkSize)) {
		return Block::Air;
	}
	const auto origin = voxelOrigin(settings);
	const float worldX = origin.x + (x + .5f) * settings.BlockSize;
	const float worldY = origin.y + (y + .5f) * settings.BlockSize;
	const float worldZ = origin.z + (z + .5f) * settings.BlockSize;
	const float elevation = height(settings.Noise, worldX, worldZ);
	if (worldY > elevation) {
		return settings.Water && worldY <= settings.SeaLevel ? Block::Water : Block::Air;
	}
	if (settings.CaveThreshold < 1 && worldY < elevation - settings.SoilDepth
		&& noise(worldX * settings.CaveFrequency + worldY * .37f, worldZ * settings.CaveFrequency + worldY * .53f, settings.Noise.Seed ^ 0xcafeu)
			> settings.CaveThreshold) {
		return Block::Air;
	}
	if (worldY < elevation - settings.SoilDepth) {
		return Block::Rock;
	}
	return elevation <= settings.SandHeight ? Block::Sand : elevation >= settings.SnowHeight ? Block::Snow : Block::Ground;
}
Geometry buildVoxelChunk(const VoxelSettings& settings, unsigned chunkX, unsigned chunkY, unsigned chunkZ, const BlockReader& read) {
	Geometry result;
	for (unsigned material = 0; material < 5; ++material) {
		result.Parts.push_back({ material });
	}
	const auto origin = voxelOrigin(settings);
	const std::array<MATH::Vector3f, 6> normals = { MATH::Vector3f { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	const std::array<MATH::Vector3f, 6> tangents = { MATH::Vector3f { 0, 0, -1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 }, { -1, 0, 0 } };
	for (unsigned z = 0; z < settings.ChunkSize; ++z) {
		for (unsigned y = 0; y < settings.ChunkSize; ++y) {
			for (unsigned x = 0; x < settings.ChunkSize; ++x) {
				const int gridX = int(chunkX * settings.ChunkSize + x);
				const int gridY = int(chunkY * settings.ChunkSize + y);
				const int gridZ = int(chunkZ * settings.ChunkSize + z);
				const Block block = read(gridX, gridY, gridZ);
				if (block == Block::Air) {
					continue;
				}
				auto& part = result.Parts[unsigned(block) - 1];
				const auto center = origin + MATH::Vector3f { gridX + .5f, gridY + .5f, gridZ + .5f } * settings.BlockSize;
				for (unsigned face = 0; face < 6; ++face) {
					const auto surfaceNormal = normals[face];
					const Block neighbour = read(gridX + int(surfaceNormal.x), gridY + int(surfaceNormal.y), gridZ + int(surfaceNormal.z));
					if (neighbour != Block::Air && (neighbour != Block::Water || block == Block::Water)) {
						continue;
					}
					const auto tangent = tangents[face];
					const auto bitangent = MATH::Vector3f::Cross(surfaceNormal, tangent);
					const auto faceCenter = center + surfaceNormal * (.5f * settings.BlockSize);
					const unsigned offset = unsigned(part.Vertices.size());
					for (const auto uv : { MATH::Vector2f { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }) {
						const auto position = faceCenter + (tangent * (uv.x - .5f) + bitangent * (uv.y - .5f)) * settings.BlockSize;
						part.Vertices.emplace_back(position, uv * settings.TextureScale, surfaceNormal, tangent, bitangent);
					}
					part.Indices.insert(part.Indices.end(), { offset, offset + 1, offset + 2, offset, offset + 2, offset + 3 });
				}
			}
		}
	}
	return result;
}
}
