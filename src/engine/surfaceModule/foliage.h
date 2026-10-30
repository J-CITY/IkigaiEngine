#pragma once
#include "surfaceGenerator.h"

namespace IKIGAI::SURFACE {
struct FoliageMaterialBinding {
	std::string Slot = {};
	std::string Material = {};
	template <class C> static void serde(C& context, FoliageMaterialBinding& value) {
		serde::serde_struct(context, value).field(&FoliageMaterialBinding::Slot, "Slot").field(&FoliageMaterialBinding::Material, "Material");
	}
};
struct FoliageLod {
	float Distance = 50.0f;
	std::string Model = {};
	std::string Material = {};
	bool Billboard = false;
	template <class C> static void serde(C& context, FoliageLod& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&FoliageLod::Distance, "Distance", default_ { 50.0f })
			.field(&FoliageLod::Model, "Model")
			.field(&FoliageLod::Material, "Material", default_ { std::string {} })
			.field(&FoliageLod::Billboard, "Billboard", default_ { false });
	}
};
struct FoliageSpecies {
	std::string Name = "Grass";
	uint32_t Seed = 1;
	std::vector<std::string> Layers = {};
	std::vector<unsigned> LayerIds = {};
	std::vector<FoliageLod> Lods = {};
	std::vector<std::string> Materials = {};
	std::vector<FoliageMaterialBinding> MaterialBindings = {};
	float Density = 0.1f;
	unsigned MaxInstances = 4096;
	float MinHeight = -100000.0f;
	float MaxHeight = 100000.0f;
	float MaxSlope = 35.0f;
	float MinScale = 0.8f;
	float MaxScale = 1.2f;
	float Offset = 0.0f;
	float ViewDistance = 150.0f;
	bool AlignToNormal = false;
	bool AllowUnderwater = false;
	bool Instancing = true;
	MATH::Vector3f MinTint = { 1, 1, 1 };
	MATH::Vector3f MaxTint = { 1, 1, 1 };
	float FadeDistance = 0;
	void validate() const;
	template <class C> static void serde(C& context, FoliageSpecies& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&FoliageSpecies::Name, "Name", default_ { std::string { "Grass" } })
			.field(&FoliageSpecies::Seed, "Seed", default_ { 1u })
			.field(&FoliageSpecies::Layers, "Layers", default_ { std::vector<std::string> {} })
			.field(&FoliageSpecies::LayerIds, "LayerIds", default_ { std::vector<unsigned> {} })
			.field(&FoliageSpecies::Lods, "Lods", default_ { std::vector<FoliageLod> {} })
			.field(&FoliageSpecies::Materials, "Materials", default_ { std::vector<std::string> {} })
			.field(&FoliageSpecies::MaterialBindings, "MaterialBindings", default_ { std::vector<FoliageMaterialBinding> {} })
			.field(&FoliageSpecies::Density, "Density", default_ { 0.1f })
			.field(&FoliageSpecies::MaxInstances, "MaxInstances", default_ { 4096u })
			.field(&FoliageSpecies::MinHeight, "MinHeight", default_ { -100000.0f })
			.field(&FoliageSpecies::MaxHeight, "MaxHeight", default_ { 100000.0f })
			.field(&FoliageSpecies::MaxSlope, "MaxSlope", default_ { 35.0f })
			.field(&FoliageSpecies::MinScale, "MinScale", default_ { 0.8f })
			.field(&FoliageSpecies::MaxScale, "MaxScale", default_ { 1.2f })
			.field(&FoliageSpecies::Offset, "Offset", default_ { 0.0f })
			.field(&FoliageSpecies::ViewDistance, "ViewDistance", default_ { 150.0f })
			.field(&FoliageSpecies::AlignToNormal, "AlignToNormal", default_ { false })
			.field(&FoliageSpecies::AllowUnderwater, "AllowUnderwater", default_ { false })
			.field(&FoliageSpecies::Instancing, "Instancing", default_ { true })
			.field(&FoliageSpecies::MinTint, "MinTint", default_ { MATH::Vector3f { 1, 1, 1 } })
			.field(&FoliageSpecies::MaxTint, "MaxTint", default_ { MATH::Vector3f { 1, 1, 1 } })
			.field(&FoliageSpecies::FadeDistance, "FadeDistance", default_ { 0.0f });
	}
};
struct Placement {
	MATH::Vector3f Position = {};
	MATH::Vector3f Normal = { 0, 1, 0 };
	float Scale = 1.0f;
	float Yaw = 0.0f;
	unsigned TileId = 0;
	uint32_t Rank = 0;
	MATH::Vector3f Tint = { 1, 1, 1 };
	std::array<float, 8> LodHeights = {};
};
std::vector<Placement> scatter(const Settings& surface, const FoliageSpecies& species, const std::atomic_bool* cancellation = nullptr);
void validateScatter(const Settings& surface, const FoliageSpecies& species);
}
