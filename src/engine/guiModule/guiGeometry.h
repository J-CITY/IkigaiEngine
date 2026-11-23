#pragma once

#include "mathModule/math.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace IKIGAI::ECS {
class Object;
}

namespace IKIGAI::GUI {

struct CanvasGeometry {
	MATH::Matrix4f correction = MATH::Matrix4f::Identity;
	int layer = 0;
	int order = 0;
	bool worldSpace = false;
	bool depthTest = true;
};

struct ClipBounds {
	MATH::Vector2f minimum {};
	MATH::Vector2f maximum {};
	bool clipped = false;

	bool empty() const;
	bool contains(MATH::Vector2f point) const;
};

CanvasGeometry canvasGeometry(const ECS::Object& object);
std::pair<int, std::int64_t> drawOrder(const ECS::Object& object, const CanvasGeometry& canvas);
// A camera projects world-space clips. Screen-space clips use the same pixel coordinates as pointer events.
ClipBounds clipBounds(const ECS::Object& object, MATH::Vector2f viewport, const MATH::Matrix4f* camera = nullptr);
std::optional<MATH::Vector2f> localPoint(const MATH::Matrix4f& matrix, MATH::Vector2f point);
}
