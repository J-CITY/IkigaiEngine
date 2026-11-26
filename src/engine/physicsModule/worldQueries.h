#pragma once
#include "physicWorld.h"

namespace IKIGAI::PHYSICS::QUERY {
struct Hit {
	Id body = 0;
	float fraction = 1;
	float distance = 0;
	Vec3 point {};
	Vec3 normal { 0, 1, 0 };
	explicit operator bool() const {
		return body != 0;
	}
};
struct Filter {
	std::uint32_t layer = 1;
	std::uint32_t mask = ~0u;
	Id ignore = 0;
	bool staticsOnly = true;
	bool accepts(const Body& b) const {
		return b.id != ignore && (!staticsOnly || b.type == BodyType::Static) && (b.layer & mask) && (layer & b.mask);
	}
};
// Return the first solid along the finite displacement.
Hit raycast(const PhysicWorld& world, Vec3 origin, Vec3 delta, Filter filter = {});
}
