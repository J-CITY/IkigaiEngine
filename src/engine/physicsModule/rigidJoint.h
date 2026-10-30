#pragma once
#include "body.h"
#include "physicsCommon/forces.h"

namespace IKIGAI::PHYSICS {
enum class RigidJointKind { Hinge, ConeTwist };
// Joint X is the hinge/twist axis. Frames are relative to each body's centre.
struct RigidJointDef {
	PHYSICS_COMMON::Id bodyA = 0;
	PHYSICS_COMMON::Id bodyB = 0;
	MATH::Vector3f anchorA {};
	MATH::Vector3f anchorB {};
	MATH::QuaternionF frameA {};
	MATH::QuaternionF frameB {};
	RigidJointKind kind = RigidJointKind::ConeTwist;
	float swingLimit = 0.78539816f;
	float twistMin = -0.78539816f;
	float twistMax = 0.78539816f;
	float damping = 0.05f;
	bool collideConnected = false;
	void validate() const;
};
struct RigidJoint : RigidJointDef {
	PHYSICS_COMMON::Id id = 0;
	MATH::Vector3f anchorImpulse {};
	float previousStep = 0;
};
// Exposed for diagnostics and validation. Components are in frame A's space.
MATH::Vector3f jointAngularError(const RigidJointDef&, const RigidBody&, const RigidBody&);
}
