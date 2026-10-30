#include "rigidJoint.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace IKIGAI::PHYSICS {
namespace {
	bool unit(MATH::QuaternionF q) {
		const float length = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
		return std::isfinite(length) && std::abs(length - 1) < 0.001f;
	}
}
void RigidJointDef::validate() const {
	if (!bodyA || !bodyB || bodyA == bodyB || !PHYSICS_COMMON::finite(anchorA) || !PHYSICS_COMMON::finite(anchorB) || !unit(frameA) || !unit(frameB)
		|| (kind != RigidJointKind::Hinge && kind != RigidJointKind::ConeTwist) || !std::isfinite(swingLimit) || swingLimit < 0
		|| swingLimit >= MATH::PI || !std::isfinite(twistMin) || !std::isfinite(twistMax) || twistMin < -MATH::PI || twistMax > MATH::PI
		|| twistMin > twistMax || !std::isfinite(damping) || damping < 0 || damping > 1) {
		throw std::invalid_argument("Invalid rigid joint frames, bodies or limits");
	}
}
MATH::Vector3f jointAngularError(const RigidJointDef& joint, const RigidBody& a, const RigidBody& b) {
	using Q = MATH::QuaternionF;
	auto relative = Q::Normalize(Q::Conjugate(a.getOrientation() * joint.frameA) * (b.getOrientation() * joint.frameB));
	if (relative.w < 0) {
		relative = relative * -1.f;
	}
	const float twistNorm = std::hypot(relative.x, relative.w);
	const Q twist = twistNorm > 1e-7f ? Q { relative.x / twistNorm, 0, 0, relative.w / twistNorm } : Q {};
	const auto swing = Q::Normalize(relative * Q::Conjugate(twist));
	const float angle = std::remainder(2 * std::atan2(twist.x, twist.w), 2 * MATH::PI);
	const float swingSin = std::hypot(swing.y, swing.z);
	const float swingAngle = 2 * std::atan2(swingSin, std::max(0.f, swing.w));
	const float limit = joint.kind == RigidJointKind::Hinge ? 0 : joint.swingLimit;
	const float excess = std::max(0.f, swingAngle - limit);
	return { angle - std::clamp(angle, joint.twistMin, joint.twistMax), swingSin > 1e-7f ? swing.y * excess / swingSin : 0,
		swingSin > 1e-7f ? swing.z * excess / swingSin : 0 };
}
}
