#pragma once
#include "rigidJoint.h"

namespace IKIGAI::PHYSICS {
void warmStartJoint(RigidJoint&, RigidBody&, RigidBody&, float dt);
void solveJointVelocity(RigidJoint&, RigidBody&, RigidBody&, float dt);
void solveJointPosition(const RigidJoint&, RigidBody&, RigidBody&);
}
