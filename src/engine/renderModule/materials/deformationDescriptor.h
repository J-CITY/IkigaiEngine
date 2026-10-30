#pragma once
#include "mathModule/math.h"
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/serializer.hpp>

namespace IKIGAI::RENDER {
enum class DeformationMode { None, Grass, Foliage, Fur };
void from_json(const nlohmann::json& json, DeformationMode& value);
void to_json(nlohmann::json& json, const DeformationMode& value);
struct DeformationDescriptor {
	DeformationMode Mode = DeformationMode::None;
	float Height = 1.0f;
	float RootHeight = 0.0f;
	float Stiffness = 0.5f;
	float MaxBend = 0.8f;
	float WindStrength = 0.15f;
	float Flutter = 0.05f;
	MATH::Vector3f Pivot = {};
	float FurLength = 0.08f;
	unsigned FurShells = 12;
	float FurDensity = 0.7f;
	float WeatherResponse = 1.0f;
	bool ProceduralCoverage = true;
	void validate() const;
	template <class C> static void serde(C& context, DeformationDescriptor& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&DeformationDescriptor::Mode, "Mode", default_ { DeformationMode::None })
			.field(&DeformationDescriptor::Height, "Height", default_ { 1.0f })
			.field(&DeformationDescriptor::RootHeight, "RootHeight", default_ { 0.0f })
			.field(&DeformationDescriptor::Stiffness, "Stiffness", default_ { 0.5f })
			.field(&DeformationDescriptor::MaxBend, "MaxBend", default_ { 0.8f })
			.field(&DeformationDescriptor::WindStrength, "WindStrength", default_ { 0.15f })
			.field(&DeformationDescriptor::Flutter, "Flutter", default_ { 0.05f })
			.field(&DeformationDescriptor::Pivot, "Pivot", default_ { MATH::Vector3f {} })
			.field(&DeformationDescriptor::FurLength, "FurLength", default_ { 0.08f })
			.field(&DeformationDescriptor::FurShells, "FurShells", default_ { 12u })
			.field(&DeformationDescriptor::FurDensity, "FurDensity", default_ { 0.7f })
			.field(&DeformationDescriptor::WeatherResponse, "WeatherResponse", default_ { 1.0f })
			.field(&DeformationDescriptor::ProceduralCoverage, "ProceduralCoverage", default_ { true });
	}
};
struct alignas(16) SurfaceGpu {
	MATH::Vector4f Parameters = {};
	MATH::Vector4f Shape = {};
	MATH::Vector4f Fur = {};
	MATH::Vector4f Pivot = {};
};
static_assert(sizeof(SurfaceGpu) == 64);
SurfaceGpu surfaceGpu(const DeformationDescriptor& descriptor, unsigned shells);
}
