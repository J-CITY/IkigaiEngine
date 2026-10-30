#pragma once
#include "collision3d.h"
namespace IKIGAI::COLLISION {
bool collideConvex(const ShapeView3D& a, const ShapeView3D& b, Manifold3D& manifold);
struct SegmentSeparation {
	float gap = 0;
	Vec3 normal {};
	Vec3 point {};
};
SegmentSeparation convexSegmentSeparation(const ConvexPolyhedron& hull, Vec3 start, Vec3 end, float radius);
}
