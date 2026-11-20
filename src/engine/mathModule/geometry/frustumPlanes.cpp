#include "frustumPlanes.h"

namespace IKIGAI::MATH::GEOMETRY {
bool SphereIntersectsFrustum(const FrustumPlanes& planes, float x, float y, float z, float radius) {
	for (const auto& [a, b, c, d] : planes) {
		if (a * x + b * y + c * z + d <= -radius) {
			return false;
		}
	}
	return true;
}
}
