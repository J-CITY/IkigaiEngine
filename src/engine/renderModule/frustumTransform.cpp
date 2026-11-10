#include "frustum.h"

#include <algorithm>

#include "coreModule/ecs/components/transform.h"

namespace IKIGAI::RENDER {
bool Frustum::boundingSphereInFrustum(const BoundingSphere& boundingSphere, const ECS::Transform& transform) const {
	return boundingSphereInFrustum(boundingSphere, transform.getWorldPosition(), transform.getWorldRotation(), transform.getWorldScale());
}

COLLISION::TYPE Frustum::boundingSphereInFrustumCollide(const BoundingSphere& boundingSphere, const ECS::Transform& transform) const {
	const auto& position = transform.getWorldPosition();
	const auto& rotation = transform.getWorldRotation();
	const auto& scale = transform.getWorldScale();
	const float maxScale = std::max(std::max(std::max(scale.x, scale.y), scale.z), 0.0f);
	const auto sphereOffset = MATH::QuaternionF::RotatePoint(boundingSphere.position, rotation) * maxScale;
	const BoundingSphere worldBounds { position + sphereOffset, boundingSphere.radius * maxScale };
	return boundingSphereInFrustumCollide(worldBounds);
}
}
