#pragma once
#include "surfaceEdits.h"
#include <cstdint>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>
#include <string>
#include <vector>

namespace IKIGAI::SURFACE {
struct NoiseSettings {
	uint32_t Seed = 1;
	float BaseHeight = 0.0f;
	float Amplitude = 12.0f;
	float Frequency = 0.015f;
	unsigned Octaves = 5;
	float Persistence = 0.5f;
	float Lacunarity = 2.0f;
	float Ridge = 0.0f;
	float Warp = 0.0f;
	float TerraceHeight = 0.0f;
	void validate() const;
	template <class C> static void serde(C& context, NoiseSettings& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&NoiseSettings::Seed, "Seed", default_ { 1u })
			.field(&NoiseSettings::BaseHeight, "BaseHeight", default_ { 0.0f })
			.field(&NoiseSettings::Amplitude, "Amplitude", default_ { 12.0f })
			.field(&NoiseSettings::Frequency, "Frequency", default_ { 0.015f })
			.field(&NoiseSettings::Octaves, "Octaves", default_ { 5u })
			.field(&NoiseSettings::Persistence, "Persistence", default_ { 0.5f })
			.field(&NoiseSettings::Lacunarity, "Lacunarity", default_ { 2.0f })
			.field(&NoiseSettings::Ridge, "Ridge", default_ { 0.0f })
			.field(&NoiseSettings::Warp, "Warp", default_ { 0.0f })
			.field(&NoiseSettings::TerraceHeight, "TerraceHeight", default_ { 0.0f });
	}
};

struct Layer {
	std::string Name = "Ground";
	std::string Material = {};
	float MinHeight = -100000.0f;
	float MaxHeight = 100000.0f;
	float MinSlope = 0.0f;
	float MaxSlope = 90.0f;
	float TextureScale = 0.1f;
	unsigned Id = 0;
	float HeightTransition = 1.0f;
	float SlopeTransition = 5.0f;
	template <class C> static void serde(C& context, Layer& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Layer::Name, "Name", default_ { std::string { "Ground" } })
			.field(&Layer::Material, "Material", default_ { std::string {} })
			.field(&Layer::MinHeight, "MinHeight", default_ { -100000.0f })
			.field(&Layer::MaxHeight, "MaxHeight", default_ { 100000.0f })
			.field(&Layer::MinSlope, "MinSlope", default_ { 0.0f })
			.field(&Layer::MaxSlope, "MaxSlope", default_ { 90.0f })
			.field(&Layer::TextureScale, "TextureScale", default_ { 0.1f })
			.field(&Layer::Id, "Id", default_ { 0u })
			.field(&Layer::HeightTransition, "HeightTransition", default_ { 1.0f })
			.field(&Layer::SlopeTransition, "SlopeTransition", default_ { 5.0f });
	}
};

struct BlendSettings {
	bool Enabled = false;
	unsigned MapResolution = 256;
	float BoundaryNoise = 0.5f;
	float NoiseFrequency = 0.08f;
	float HeightInfluence = 0.25f;
	float TransitionWidth = 0.8f;
	template <class C> static void serde(C& context, BlendSettings& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&BlendSettings::Enabled, "Enabled", default_ { false })
			.field(&BlendSettings::MapResolution, "MapResolution", default_ { 256u })
			.field(&BlendSettings::BoundaryNoise, "BoundaryNoise", default_ { 0.5f })
			.field(&BlendSettings::NoiseFrequency, "NoiseFrequency", default_ { 0.08f })
			.field(&BlendSettings::HeightInfluence, "HeightInfluence", default_ { 0.25f })
			.field(&BlendSettings::TransitionWidth, "TransitionWidth", default_ { 0.8f });
	}
};

struct WaterSettings {
	bool Enabled = false;
	float Level = 0.0f;
	float TextureScale = 0.05f;
	std::string Material = {};
	template <class C> static void serde(C& context, WaterSettings& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&WaterSettings::Enabled, "Enabled", default_ { false })
			.field(&WaterSettings::Level, "Level", default_ { 0.0f })
			.field(&WaterSettings::TextureScale, "TextureScale", default_ { 0.05f })
			.field(&WaterSettings::Material, "Material", default_ { std::string {} });
	}
};

struct Settings {
	unsigned TilesX = 4;
	unsigned TilesZ = 4;
	float TileSize = 32.0f;
	unsigned Resolution = 32;
	NoiseSettings Noise = {};
	std::vector<Layer> Layers = { Layer {} };
	WaterSettings Water = {};
	std::vector<float> LodDistances = { 50.0f, 120.0f, 250.0f };
	float ViewDistance = 600.0f;
	float SkirtDepth = 4.0f;
	unsigned BuildBudget = 2;
	unsigned CacheMegabytes = 128;
	unsigned Version = 1;
	BlendSettings Blend = {};
	std::vector<EditLayer> Edits = {};
	bool BackgroundUpdates = true;
	unsigned PendingBuilds = 4;
	float UploadMilliseconds = 2.0f;
	void validate() const;
	unsigned layerId(const std::string& name) const;
	unsigned stableLayerId(unsigned index) const;
	unsigned layerIndex(unsigned stableId) const;
	template <class C> static void serde(C& context, Settings& value) {
		if constexpr (!C::is_serialize) {
			value.Layers.clear();
			value.LodDistances.clear();
			value.Edits.clear();
		}
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Settings::TilesX, "TilesX", default_ { 4u })
			.field(&Settings::TilesZ, "TilesZ", default_ { 4u })
			.field(&Settings::TileSize, "TileSize", default_ { 32.0f })
			.field(&Settings::Resolution, "Resolution", default_ { 32u })
			.field(&Settings::Noise, "Noise", default_ { NoiseSettings {} })
			.field(&Settings::Layers, "Layers", default_ { std::vector<Layer> { Layer {} } })
			.field(&Settings::Water, "Water", default_ { WaterSettings {} })
			.field(&Settings::LodDistances, "LodDistances", default_ { std::vector<float> { 50.0f, 120.0f, 250.0f } })
			.field(&Settings::ViewDistance, "ViewDistance", default_ { 600.0f })
			.field(&Settings::SkirtDepth, "SkirtDepth", default_ { 4.0f })
			.field(&Settings::BuildBudget, "BuildBudget", default_ { 2u })
			.field(&Settings::CacheMegabytes, "CacheMegabytes", default_ { 128u })
			.field(&Settings::Version, "Version", default_ { 1u })
			.field(&Settings::Blend, "Blend", default_ { BlendSettings {} })
			.field(&Settings::Edits, "Edits", default_ { std::vector<EditLayer> {} })
			.field(&Settings::BackgroundUpdates, "BackgroundUpdates", default_ { true })
			.field(&Settings::PendingBuilds, "PendingBuilds", default_ { 4u })
			.field(&Settings::UploadMilliseconds, "UploadMilliseconds", default_ { 2.0f });
	}
};

enum class Block : uint8_t { Air, Ground, Rock, Sand, Snow, Water };
void from_json(const nlohmann::json& json, Block& block);
void to_json(nlohmann::json& json, const Block& block);
struct VoxelEdit {
	int X = 0;
	int Y = 0;
	int Z = 0;
	Block Type = Block::Air;
	template <class C> static void serde(C& context, VoxelEdit& value) {
		serde::serde_struct(context, value)
			.field(&VoxelEdit::X, "X")
			.field(&VoxelEdit::Y, "Y")
			.field(&VoxelEdit::Z, "Z")
			.field(&VoxelEdit::Type, "Type");
	}
};
struct VoxelSettings {
	unsigned ChunksX = 4;
	unsigned ChunksY = 2;
	unsigned ChunksZ = 4;
	unsigned ChunkSize = 16;
	float BlockSize = 1.0f;
	NoiseSettings Noise = {};
	float SeaLevel = 0.0f;
	float SandHeight = 1.0f;
	float SnowHeight = 10.0f;
	float SoilDepth = 3.0f;
	bool Water = true;
	float CaveFrequency = 0.06f;
	float CaveThreshold = 1.0f;
	float TextureScale = 1.0f;
	float ViewDistance = 300.0f;
	unsigned BuildBudget = 2;
	unsigned CacheMegabytes = 128;
	void validate() const;
	template <class C> static void serde(C& context, VoxelSettings& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&VoxelSettings::ChunksX, "ChunksX", default_ { 4u })
			.field(&VoxelSettings::ChunksY, "ChunksY", default_ { 2u })
			.field(&VoxelSettings::ChunksZ, "ChunksZ", default_ { 4u })
			.field(&VoxelSettings::ChunkSize, "ChunkSize", default_ { 16u })
			.field(&VoxelSettings::BlockSize, "BlockSize", default_ { 1.0f })
			.field(&VoxelSettings::Noise, "Noise", default_ { NoiseSettings {} })
			.field(&VoxelSettings::SeaLevel, "SeaLevel", default_ { 0.0f })
			.field(&VoxelSettings::SandHeight, "SandHeight", default_ { 1.0f })
			.field(&VoxelSettings::SnowHeight, "SnowHeight", default_ { 10.0f })
			.field(&VoxelSettings::SoilDepth, "SoilDepth", default_ { 3.0f })
			.field(&VoxelSettings::Water, "Water", default_ { true })
			.field(&VoxelSettings::CaveFrequency, "CaveFrequency", default_ { 0.06f })
			.field(&VoxelSettings::CaveThreshold, "CaveThreshold", default_ { 1.0f })
			.field(&VoxelSettings::TextureScale, "TextureScale", default_ { 1.0f })
			.field(&VoxelSettings::ViewDistance, "ViewDistance", default_ { 300.0f })
			.field(&VoxelSettings::BuildBudget, "BuildBudget", default_ { 2u })
			.field(&VoxelSettings::CacheMegabytes, "CacheMegabytes", default_ { 128u });
	}
};
// Shared upper-bound convention: an exact threshold selects the next LOD;
// the final model remains visible beyond the final threshold.
unsigned selectLod(float distance, const std::vector<float>& thresholds);
void validateLodDistances(const std::vector<float>& thresholds);
}
