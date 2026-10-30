#include "foliage.h"
#include "surfaceJob.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <stdexcept>
#include <unordered_set>

namespace IKIGAI::SURFACE {
void FoliageSpecies::validate() const {
	auto validTint = [](float low, float high) { return std::isfinite(low) && std::isfinite(high) && low >= 0 && high <= 1 && low <= high; };
	if (!validTint(MinTint.x, MaxTint.x) || !validTint(MinTint.y, MaxTint.y) || !validTint(MinTint.z, MaxTint.z) || !std::isfinite(FadeDistance)
		|| FadeDistance < 0 || FadeDistance > ViewDistance) {
		throw std::invalid_argument("Invalid foliage tint/fade settings");
	}
	std::unordered_set<std::string> slots;
	if (MaterialBindings.size() > 32) {
		throw std::invalid_argument("Too many named foliage material bindings");
	}
	for (const auto& binding : MaterialBindings) {
		if (binding.Slot.empty() || binding.Material.empty() || !slots.insert(binding.Slot).second) {
			throw std::invalid_argument("Foliage material bindings require unique slot names and material assets");
		}
	}
	if (Name.empty() || Lods.empty() || Lods.size() > 8 || Materials.size() > 32 || MaxInstances > 20000 || !std::isfinite(Density) || Density < 0
		|| Density > 100 || !std::isfinite(ViewDistance) || ViewDistance <= 0 || !std::isfinite(MinHeight) || !std::isfinite(MaxHeight)
		|| MinHeight > MaxHeight || !std::isfinite(MaxSlope) || MaxSlope < 0 || MaxSlope > 90 || !std::isfinite(MinScale) || !std::isfinite(MaxScale)
		|| MinScale <= 0 || MinScale > MaxScale || MaxScale > 1000 || !std::isfinite(Offset) || std::abs(Offset) > 1000) {
		throw std::invalid_argument("Invalid foliage species: " + Name);
	}
	std::vector<float> distances;
	for (const auto& lod : Lods) {
		if (lod.Model.empty()) {
			throw std::invalid_argument("Foliage LOD requires a model: " + Name);
		}
		distances.push_back(lod.Distance);
	}
	validateLodDistances(distances);
}
void validateScatter(const Settings& surface, const FoliageSpecies& species) {
	species.validate();
	for (const auto& name : species.Layers) {
		surface.layerId(name);
	}
	for (unsigned id : species.LayerIds) {
		surface.layerIndex(id);
	}
	if (species.Density > 0 && species.MaxInstances) {
		const double spacing = 1 / std::sqrt(double(species.Density));
		const double columns = std::ceil(surface.TilesX * double(surface.TileSize) / spacing);
		const double rows = std::ceil(surface.TilesZ * double(surface.TileSize) / spacing);
		if (columns * rows > 500000) {
			throw std::invalid_argument("Foliage candidate budget exceeded: " + species.Name);
		}
	}
}
std::vector<Placement> scatter(const Settings& surface, const FoliageSpecies& species, const std::atomic_bool* cancellation) {
	species.validate();
	std::vector<unsigned> layers;
	for (const auto& name : species.Layers) {
		const unsigned index = surface.layerId(name);
		if (std::find(layers.begin(), layers.end(), index) == layers.end()) {
			layers.push_back(index);
		}
	}
	for (const unsigned id : species.LayerIds) {
		const unsigned index = surface.layerIndex(id);
		if (std::find(layers.begin(), layers.end(), index) == layers.end()) {
			layers.push_back(index);
		}
	}
	if (species.Density == 0 || species.MaxInstances == 0) {
		return {};
	}
	const float width = surface.TilesX * surface.TileSize;
	const float depth = surface.TilesZ * surface.TileSize;
	const float spacing = 1.0f / std::sqrt(species.Density);
	const double columns = std::ceil(double(width) / spacing);
	const double rows = std::ceil(double(depth) / spacing);
	if (columns * rows > 500000) {
		throw std::invalid_argument("Foliage candidate budget exceeded; reduce Density or split the surface: " + species.Name);
	}
	auto rankLess = [](const Placement& a, const Placement& b) { return a.Rank < b.Rank; };
	std::priority_queue<Placement, std::vector<Placement>, decltype(rankLess)> selected(rankLess);
	for (unsigned z = 0; z < unsigned(rows); ++z) {
		checkBuildCancellation(cancellation);
		for (unsigned x = 0; x < unsigned(columns); ++x) {
			const uint32_t seed = hash(x * 73856093u ^ z * 19349663u ^ species.Seed ^ surface.Noise.Seed);
			const float localX = -width * .5f + (x + .1f + .8f * random01(seed)) * spacing;
			const float localZ = -depth * .5f + (z + .1f + .8f * random01(seed ^ 0xabcdu)) * spacing;
			if (!contains(surface, localX, localZ)) {
				continue;
			}
			const auto ground = sample(surface, localX, localZ);
			if ((ground.Submerged && !species.AllowUnderwater) || ground.Position.y < species.MinHeight || ground.Position.y > species.MaxHeight
				|| ground.Slope > species.MaxSlope
				|| (!surface.Blend.Enabled && !layers.empty() && std::find(layers.begin(), layers.end(), ground.LayerId) == layers.end())) {
				continue;
			}
			float probability = 1.0f;
			if (surface.Blend.Enabled && !layers.empty()) {
				probability = 0;
				for (const auto layer : layers) {
					probability += ground.Cover.Weights[layer];
				}
			}
			if (random01(seed ^ 0x77bbu) >= std::min(1.0f, probability) * ground.Cover.FoliageDensity) {
				continue;
			}
			Placement placement;
			placement.Position = ground.Position + ground.Normal * species.Offset;
			placement.Normal = ground.Normal;
			placement.Scale = std::lerp(species.MinScale, species.MaxScale, random01(seed ^ 0x5678u));
			placement.Yaw = random01(seed ^ 0x1234u) * MATH::PI * 2;
			const unsigned tileX = std::min(unsigned((localX + width * .5f) / surface.TileSize), surface.TilesX - 1);
			const unsigned tileZ = std::min(unsigned((localZ + depth * .5f) / surface.TileSize), surface.TilesZ - 1);
			placement.TileId = tileZ * surface.TilesX + tileX;
			placement.Rank = hash(seed ^ 0xdeadbeefu);
			placement.Tint = species.MinTint + (species.MaxTint - species.MinTint) * random01(seed ^ 0x2945u);
			if (selected.size() < species.MaxInstances) {
				selected.push(placement);
			}
			else if (placement.Rank < selected.top().Rank) {
				selected.pop();
				selected.push(placement);
			}
		}
	}
	std::vector<Placement> result;
	while (!selected.empty()) {
		result.push_back(selected.top());
		selected.pop();
	}
	for (auto& placement : result) {
		checkBuildCancellation(cancellation);
		// Root projection follows the actual terrain triangle at every LOD.
		const auto root = placement.Position - placement.Normal * species.Offset;
		for (unsigned lod = 0; lod <= surface.LodDistances.size(); ++lod) {
			placement.LodHeights[lod] = project(surface, root.x, root.z, lod).Position.y + placement.Normal.y * species.Offset;
		}
	}
	std::sort(
		result.begin(), result.end(), [](const auto& a, const auto& b) { return a.TileId != b.TileId ? a.TileId < b.TileId : a.Rank < b.Rank; });
	return result;
}
}
