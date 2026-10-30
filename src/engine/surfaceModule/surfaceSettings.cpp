#include "surfaceSettings.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace IKIGAI::SURFACE {
void from_json(const nlohmann::json& json, Block& block) {
	const auto name = json.get<std::string>();
	const std::array<std::string, 6> names = { "Air", "Ground", "Rock", "Sand", "Snow", "Water" };
	const auto found = std::find(names.begin(), names.end(), name);
	if (found == names.end()) {
		throw std::invalid_argument("Unknown voxel block type: " + name);
	}
	block = Block(found - names.begin());
}
void to_json(nlohmann::json& json, const Block& block) {
	const std::array<std::string, 6> names = { "Air", "Ground", "Rock", "Sand", "Snow", "Water" };
	if (unsigned(block) >= names.size()) {
		throw std::invalid_argument("Invalid voxel block type");
	}
	json = names[unsigned(block)];
}
namespace {
	void require(bool valid, const char* message) {
		if (!valid) {
			throw std::invalid_argument(message);
		}
	}
	bool positive(float value) {
		return std::isfinite(value) && value > 0;
	}
}
void NoiseSettings::validate() const {
	require(std::isfinite(BaseHeight) && std::abs(BaseHeight) <= 100000 && std::isfinite(Amplitude) && Amplitude >= 0 && Amplitude <= 10000,
		"Surface noise height is out of range");
	require(positive(Frequency) && Frequency <= 1 && Octaves >= 1 && Octaves <= 10, "Surface noise frequency/octaves are out of range");
	require(std::isfinite(Persistence) && Persistence >= 0 && Persistence <= 1 && std::isfinite(Lacunarity) && Lacunarity >= 1 && Lacunarity <= 3,
		"Surface noise persistence/lacunarity are out of range");
	require(std::isfinite(Ridge) && Ridge >= 0 && Ridge <= 1 && std::isfinite(Warp) && Warp >= 0 && Warp <= 1000 && std::isfinite(TerraceHeight)
			&& TerraceHeight >= 0 && TerraceHeight <= 1000,
		"Surface noise shape is out of range");
}
void validateLodDistances(const std::vector<float>& thresholds) {
	float previous = 0;
	for (const float distance : thresholds) {
		require(positive(distance) && distance > previous, "LOD distances must be finite, positive and strictly increasing");
		previous = distance;
	}
}
unsigned selectLod(float distance, const std::vector<float>& thresholds) {
	return static_cast<unsigned>(std::upper_bound(thresholds.begin(), thresholds.end(), distance) - thresholds.begin());
}
void Settings::validate() const {
	Noise.validate();
	require(Version == 1, "Unsupported surface settings version");
	require(PendingBuilds >= 1 && PendingBuilds <= 16 && positive(UploadMilliseconds) && UploadMilliseconds <= 50,
		"Invalid surface background queue or upload budget");
	require(TilesX >= 1 && TilesX <= 64 && TilesZ >= 1 && TilesZ <= 64 && TilesX * TilesZ <= 1024, "Surface tile count is out of range");
	require(
		positive(TileSize) && TileSize >= .01f && TileSize <= 1024 && Resolution >= 2 && Resolution <= 256 && (Resolution & (Resolution - 1)) == 0,
		"Surface requires a power-of-two resolution from 2 to 256 and a positive tile size");
	require(Layers.size() >= 1 && Layers.size() <= 32, "Surface requires 1 to 32 layers");
	std::unordered_set<std::string> names;
	std::unordered_set<unsigned> identifiers;
	for (const auto& layer : Layers) {
		require(!layer.Name.empty() && names.insert(layer.Name).second, "Surface layer names must be nonempty and unique");
		require(std::isfinite(layer.MinHeight) && std::isfinite(layer.MaxHeight) && layer.MinHeight <= layer.MaxHeight
				&& std::isfinite(layer.MinSlope) && std::isfinite(layer.MaxSlope) && layer.MinSlope >= 0 && layer.MaxSlope <= 90
				&& layer.MinSlope <= layer.MaxSlope && positive(layer.TextureScale) && layer.TextureScale <= 10000,
			"Invalid surface layer range/texture scale");
		require(std::isfinite(layer.HeightTransition) && layer.HeightTransition >= 0 && layer.HeightTransition <= 10000
				&& std::isfinite(layer.SlopeTransition) && layer.SlopeTransition >= 0 && layer.SlopeTransition <= 90,
			"Invalid surface layer transition");
		const unsigned index = unsigned(&layer - Layers.data());
		require(identifiers.insert(stableLayerId(index)).second, "Surface layer IDs must be unique");
	}
	require(!Blend.Enabled || Layers.size() <= 4, "Smooth surfaces support up to four coatings");
	require(Blend.MapResolution >= 16 && Blend.MapResolution <= 1024 && std::isfinite(Blend.BoundaryNoise) && Blend.BoundaryNoise >= 0
			&& Blend.BoundaryNoise <= 1000 && positive(Blend.NoiseFrequency) && Blend.NoiseFrequency <= 10 && std::isfinite(Blend.HeightInfluence)
			&& Blend.HeightInfluence >= 0 && Blend.HeightInfluence <= 1 && positive(Blend.TransitionWidth) && Blend.TransitionWidth <= 1,
		"Invalid surface blend settings");
	require(Edits.size() <= 32, "Surface supports up to 32 edit layers");
	std::unordered_set<unsigned> editIds;
	size_t strokeCount = 0;
	for (const auto& edit : Edits) {
		require(edit.Id > 0 && editIds.insert(edit.Id).second && !edit.Name.empty() && std::isfinite(edit.Opacity) && edit.Opacity >= 0
				&& edit.Opacity <= 1,
			"Invalid surface edit layer");
		strokeCount += edit.Strokes.size();
		for (const auto& stroke : edit.Strokes) {
			stroke.validate();
			if (stroke.Operation == BrushOperation::PaintLayer) {
				require(Blend.Enabled, "Painting coatings requires smooth blending");
				layerIndex(stroke.LayerId);
			}
		}
	}
	require(strokeCount <= 4096, "Surface brush stroke budget exceeded");
	validateLodDistances(LodDistances);
	require(LodDistances.size() <= 7 && (1u << LodDistances.size()) <= Resolution, "Surface has too many LODs for its resolution");
	require(positive(ViewDistance) && std::isfinite(SkirtDepth) && SkirtDepth >= 0 && SkirtDepth <= 100000 && BuildBudget >= 1 && BuildBudget <= 64
			&& CacheMegabytes >= 1 && CacheMegabytes <= 2048,
		"Invalid surface view distance, skirt depth or build budget");
	require(std::isfinite(Water.Level) && std::abs(Water.Level) <= 100000 && positive(Water.TextureScale) && Water.TextureScale <= 10000,
		"Invalid water level or texture scale");
}
unsigned Settings::layerId(const std::string& name) const {
	for (unsigned index = 0; index < Layers.size(); ++index) {
		if (Layers[index].Name == name) {
			return index;
		}
	}
	throw std::invalid_argument("Unknown surface layer: " + name);
}
unsigned Settings::stableLayerId(unsigned index) const {
	return Layers.at(index).Id ? Layers[index].Id : index + 1;
}
unsigned Settings::layerIndex(unsigned stableId) const {
	for (unsigned index = 0; index < Layers.size(); ++index) {
		if (stableLayerId(index) == stableId) {
			return index;
		}
	}
	throw std::invalid_argument("Unknown surface coating ID: " + std::to_string(stableId));
}
void VoxelSettings::validate() const {
	Noise.validate();
	require(ChunksX >= 1 && ChunksX <= 32 && ChunksY >= 1 && ChunksY <= 16 && ChunksZ >= 1 && ChunksZ <= 32
			&& uint64_t(ChunksX) * ChunksY * ChunksZ <= 1024,
		"Voxel chunk count is out of range");
	require(ChunkSize >= 4 && ChunkSize <= 32 && (ChunkSize & (ChunkSize - 1)) == 0 && positive(BlockSize) && BlockSize >= .001f && BlockSize <= 100,
		"Invalid voxel chunk/block size");
	require(
		positive(ViewDistance) && positive(TextureScale) && BuildBudget >= 1 && BuildBudget <= 64 && CacheMegabytes >= 1 && CacheMegabytes <= 2048,
		"Invalid voxel render settings");
	require(std::isfinite(SeaLevel) && std::isfinite(SandHeight) && std::isfinite(SnowHeight) && SandHeight <= SnowHeight && std::isfinite(SoilDepth)
			&& SoilDepth >= 0,
		"Invalid voxel layer heights");
	require(positive(CaveFrequency) && CaveFrequency <= 1 && std::isfinite(CaveThreshold) && CaveThreshold >= 0 && CaveThreshold <= 1,
		"Invalid voxel cave settings");
}
}
