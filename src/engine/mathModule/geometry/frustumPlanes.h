#pragma once

#include <array>

namespace IKIGAI::MATH::GEOMETRY {
// Planes with unit normals pointing into the frustum.
using FrustumPlanes = std::array<std::array<float, 4>, 6>;

[[nodiscard]] bool SphereIntersectsFrustum(const FrustumPlanes& planes, float x, float y, float z, float radius);
}
