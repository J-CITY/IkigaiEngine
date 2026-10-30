#pragma once
#include "mathModule/math.h"
#include <cstdint>
namespace IKIGAI::PHYSICS {
struct ImpactEvent {
	std::uint64_t bodyA = 0;
	std::uint64_t bodyB = 0;
	MATH::Vector3f point {};
	MATH::Vector3f normal {};
	float normalImpulse = 0;
	float closingSpeed = 0;
};
}
