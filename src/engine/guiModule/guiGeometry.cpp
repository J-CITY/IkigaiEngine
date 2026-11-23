#include "guiGeometry.h"

#include "coreModule/ecs/object.h"
#include "coreModule/gui/components/spriteComponent.h"
#include "coreModule/gui/components/vectorComponents.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace IKIGAI::GUI {

CanvasGeometry canvasGeometry(const ECS::Object& object) {
	CanvasGeometry result;
	for (auto ancestor = &object; ancestor; ancestor = ancestor->getParent().get()) {
		auto canvas = ancestor->getComponent<ECS::VectorCanvasComponent>();
		if (!canvas) {
			continue;
		}
		result.worldSpace = canvas->WorldSpace;
		result.depthTest = canvas->DepthTest;
		result.layer = canvas->SortingLayer;
		result.order = canvas->Order;
		if (canvas->DpiScale != 1.f) {
			const auto& root = ancestor->getTransform()->getWorldMatrix();
			const float dpi = std::clamp(canvas->DpiScale, .01f, 100.f);
			if (result.worldSpace) {
				if (MATH::Matrix4f::Determinant(root) != 0.f) {
					auto scale = MATH::Matrix4f::Identity;
					scale(0, 0) = scale(1, 1) = dpi;
					result.correction = root * scale * MATH::Matrix4f::Inverse(root);
				}
			}
			else {
				// Scale in the screen plane around the canvas origin, including for a zero Z scale.
				result.correction(0, 0) = result.correction(1, 1) = dpi;
				result.correction(0, 3) = root.getData()[3] * (1.f - dpi);
				result.correction(1, 3) = root.getData()[7] * (1.f - dpi);
			}
		}
		break;
	}
	return result;
}

std::pair<int, std::int64_t> drawOrder(const ECS::Object& object, const CanvasGeometry& canvas) {
	std::int64_t order = canvas.order;
	if (auto shape = object.getComponent<ECS::VectorShapeComponent>()) {
		order += shape->Order;
	}
	else if (auto path = object.getComponent<ECS::VectorPathComponent>()) {
		order += path->Order;
	}
	else if (auto text = object.getComponent<ECS::SdfTextComponent>()) {
		order += text->Order;
	}
	return { canvas.layer, order };
}

bool ClipBounds::empty() const {
	return !(maximum.x > minimum.x && maximum.y > minimum.y);
}

bool ClipBounds::contains(MATH::Vector2f point) const {
	return point.x >= minimum.x && point.y >= minimum.y && point.x < maximum.x && point.y < maximum.y;
}

ClipBounds clipBounds(const ECS::Object& object, MATH::Vector2f viewport, const MATH::Matrix4f* camera) {
	ClipBounds result { {}, viewport, false };
	for (auto ancestor = &object; ancestor; ancestor = ancestor->getParent().get()) {
		auto clip = ancestor->getComponent<ECS::ClipComponent>();
		if (!clip) {
			continue;
		}
		result.clipped = true;
		const auto matrix = canvasGeometry(*ancestor).correction * ancestor->getTransform()->getWorldMatrix();
		const auto infinity = std::numeric_limits<float>::infinity();
		MATH::Vector2f minimum { infinity, infinity };
		MATH::Vector2f maximum { -infinity, -infinity };
		for (float x : { 0.f, clip->mWidth }) {
			for (float y : { 0.f, clip->mHeight }) {
				auto point = matrix * MATH::Vector4f(x, camera ? -y : y, 0, 1);
				if (camera) {
					point = *camera * point;
					if (point.w <= 1e-4f) {
						continue;
					}
					point.x = (point.x / point.w * .5f + .5f) * viewport.x;
					point.y = (.5f - point.y / point.w * .5f) * viewport.y;
				}
				minimum.x = std::min(minimum.x, point.x);
				minimum.y = std::min(minimum.y, point.y);
				maximum.x = std::max(maximum.x, point.x);
				maximum.y = std::max(maximum.y, point.y);
			}
		}
		result.minimum.x = std::max(result.minimum.x, minimum.x);
		result.minimum.y = std::max(result.minimum.y, minimum.y);
		result.maximum.x = std::min(result.maximum.x, maximum.x);
		result.maximum.y = std::min(result.maximum.y, maximum.y);
	}
	return result;
}

std::optional<MATH::Vector2f> localPoint(const MATH::Matrix4f& matrix, MATH::Vector2f point) {
	const auto* row = matrix.getData();
	const float determinant = row[0] * row[5] - row[1] * row[4];
	if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-12f) {
		return {};
	}
	const float x = point.x - row[3];
	const float y = point.y - row[7];
	return MATH::Vector2f { (row[5] * x - row[1] * y) / determinant, (row[0] * y - row[4] * x) / determinant };
}

}
