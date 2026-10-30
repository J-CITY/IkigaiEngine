#include "surfaceEdits.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace IKIGAI::SURFACE {
namespace {
	constexpr std::array<const char*, 4> operationNames = { "AddHeight", "FlattenHeight", "PaintLayer", "FoliageDensity" };
}
void from_json(const nlohmann::json& json, BrushOperation& operation) {
	const auto name = json.get<std::string>();
	const auto found = std::find(operationNames.begin(), operationNames.end(), name);
	if (found == operationNames.end()) {
		throw std::invalid_argument("Unknown surface brush operation: " + name);
	}
	operation = BrushOperation(found - operationNames.begin());
}
void to_json(nlohmann::json& json, const BrushOperation& operation) {
	if (size_t(operation) >= operationNames.size()) {
		throw std::invalid_argument("Invalid surface brush operation");
	}
	json = operationNames[size_t(operation)];
}
void BrushStroke::validate() const {
	if (size_t(Operation) >= operationNames.size() || !std::isfinite(Center.x) || !std::isfinite(Center.y) || !std::isfinite(Radius) || Radius <= 0
		|| Radius > 100000 || !std::isfinite(Hardness) || Hardness < 0 || Hardness > 1 || !std::isfinite(Strength) || Strength < 0 || Strength > 1
		|| !std::isfinite(Value) || std::abs(Value) > 100000 || (Operation == BrushOperation::FoliageDensity && (Value < 0 || Value > 1))) {
		throw std::invalid_argument("Invalid surface brush parameters");
	}
}
float brushInfluence(const BrushStroke& stroke, float x, float z) {
	const float dx = x - stroke.Center.x;
	const float dz = z - stroke.Center.y;
	const float distance = std::sqrt(dx * dx + dz * dz) / stroke.Radius;
	if (distance >= 1) {
		return 0;
	}
	const float fade = std::clamp((distance - stroke.Hardness) / std::max(1 - stroke.Hardness, .00001f), 0.0f, 1.0f);
	return stroke.Strength * (1 - fade * fade * (3 - 2 * fade));
}
}
