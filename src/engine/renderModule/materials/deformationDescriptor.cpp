#include "deformationDescriptor.h"
#include <cmath>
#include <stdexcept>

namespace IKIGAI::RENDER {
void from_json(const nlohmann::json& json, DeformationMode& value) {
	const auto name = json.get<std::string>();
	if (name == "None") {
		value = DeformationMode::None;
	}
	else if (name == "Grass") {
		value = DeformationMode::Grass;
	}
	else if (name == "Foliage") {
		value = DeformationMode::Foliage;
	}
	else if (name == "Fur") {
		value = DeformationMode::Fur;
	}
	else {
		throw std::invalid_argument("Unknown Deformation.Mode: " + name);
	}
}
void to_json(nlohmann::json& json, const DeformationMode& value) {
	json = value == DeformationMode::None	? "None"
		: value == DeformationMode::Grass	? "Grass"
		: value == DeformationMode::Foliage ? "Foliage"
											: "Fur";
}
void DeformationDescriptor::validate() const {
	auto valid = [](float value, float low, float high) { return std::isfinite(value) && value >= low && value <= high; };
	if (!valid(Height, .001f, 1000) || !valid(RootHeight, -1e5f, 1e5f) || !valid(Stiffness, 0, 1) || !valid(MaxBend, 0, 10)
		|| !valid(WindStrength, 0, 10) || !valid(Flutter, 0, 10) || !valid(FurLength, 0, 10) || !valid(FurDensity, 0, 1)
		|| !valid(WeatherResponse, 0, 1) || FurShells < 2 || FurShells > 32 || !valid(Pivot.x, -1e5f, 1e5f) || !valid(Pivot.y, -1e5f, 1e5f)
		|| !valid(Pivot.z, -1e5f, 1e5f)) {
		throw std::invalid_argument("Invalid Deformation settings");
	}
}
SurfaceGpu surfaceGpu(const DeformationDescriptor& descriptor, unsigned shells) {
	return { { float(descriptor.Mode), descriptor.Stiffness, descriptor.MaxBend, descriptor.WindStrength },
		{ descriptor.Height, descriptor.RootHeight, descriptor.Flutter, descriptor.WeatherResponse },
		{ descriptor.FurLength, float(shells), descriptor.FurDensity, descriptor.ProceduralCoverage ? 1.0f : 0.0f },
		{ descriptor.Pivot.x, descriptor.Pivot.y, descriptor.Pivot.z, 0 } };
}
}
