#pragma once
#include <array>
// #include <coreModule/ecs/components/transform.h>

#include "objects/boundingSphere.h"

namespace IKIGAI {
namespace ECS {
	class Transform;
}
}

namespace IKIGAI {
namespace RENDER {
	namespace COLLISION {
		enum class TYPE {
			OUTSIDE = 0,
			INSIDE,
			INTERSECTION,
		};
	}
	class Frustum {
	public:
		void make(const MATH::Matrix4f& viewProjection);
		[[nodiscard]] bool pointInFrustum(float x, float y, float z) const;
		[[nodiscard]] bool sphereInFrustum(float x, float y, float z, float radius) const;
		[[nodiscard]] bool cubeInFrustum(float x, float y, float z, float size) const;
		[[nodiscard]] bool boundingSphereInFrustum(const BoundingSphere& boundingSphere, const ECS::Transform& transform) const;
		[[nodiscard]] std::array<float, 4> getNearPlane() const;
		[[nodiscard]] std::array<float, 4> getFarPlane() const;
		// Plane i of make() (A, B, C, D with A*x + B*y + C*z + D >= 0 inside), i in [0, 6).
		[[nodiscard]] std::array<float, 4> getPlane(size_t index) const;
		[[nodiscard]] COLLISION::TYPE boundingSphereInFrustumCollide(const BoundingSphere& boundingSphere, const ECS::Transform& transform) const;
		COLLISION::TYPE boundingSphereInFrustumCollide(const BoundingSphere& boundingSphere) const;

	private:
		float data[6][4] = { };
	};
}
}
