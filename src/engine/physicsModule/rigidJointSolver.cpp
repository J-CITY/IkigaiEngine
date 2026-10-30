#include "rigidJointSolver.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::PHYSICS {
namespace {
	using V = MATH::Vector3f;
	using Q = MATH::QuaternionF;
	V inertia(const RigidBody& body, V value) {
		return body.getInverseMass() > 0 ? body.getInverseInertiaTensorWorld() * value : V {};
	}
	void turn(RigidBody& body, V delta) {
		const float angle = delta.magnitude();
		if (angle > 1e-8f) {
			const float limited = std::min(angle, .2f);
			const auto axis = delta * (1.f / angle);
			const float sine = std::sin(limited * .5f);
			body.setOrientation(Q::Normalize(Q { axis.x * sine, axis.y * sine, axis.z * sine, std::cos(limited * .5f) } * body.getOrientation()));
		}
	}
	void impulse(RigidBody& body, V value, V arm, bool position) {
		if (body.getInverseMass() <= 0) {
			return;
		}
		const auto angular = inertia(body, arm.cross(value));
		if (position) {
			body.setPosition(body.getPosition() + value * body.getInverseMass());
			turn(body, angular);
			body.calculateDerivedData();
		}
		else {
			body.addVelocity(value * body.getInverseMass());
			body.addRotation(angular);
		}
	}
	float effectiveMass(const RigidBody& a, const RigidBody& b, V ra, V rb, V axis) {
		return a.getInverseMass() + b.getInverseMass() + axis.dot(inertia(a, ra.cross(axis)).cross(ra) + inertia(b, rb.cross(axis)).cross(rb));
	}
	void angularImpulse(RigidBody& body, V value, bool position) {
		const auto delta = inertia(body, value);
		if (position) {
			turn(body, delta);
			body.calculateDerivedData();
		}
		else {
			body.addRotation(delta);
		}
	}
}
void warmStartJoint(RigidJoint& joint, RigidBody& a, RigidBody& b, float dt) {
	// A changed timestep or teleport must never replay a stale large impulse.
	const float ratio = joint.previousStep > 0 ? dt / joint.previousStep : 0;
	joint.anchorImpulse *= ratio >= .5f && ratio <= 2 ? .75f * ratio : 0;
	joint.previousStep = dt;
	impulse(a, joint.anchorImpulse, a.getDirectionInWorldSpace(joint.anchorA), false);
	impulse(b, -joint.anchorImpulse, b.getDirectionInWorldSpace(joint.anchorB), false);
}
void solveJointVelocity(RigidJoint& joint, RigidBody& a, RigidBody& b, float dt) {
	const auto ra = a.getDirectionInWorldSpace(joint.anchorA);
	const auto rb = b.getDirectionInWorldSpace(joint.anchorB);
	for (int index = 0; index < 3; ++index) {
		V axis {};
		axis[index] = 1;
		const float mass = effectiveMass(a, b, ra, rb, axis);
		if (mass <= 1e-9f) {
			continue;
		}
		const auto relative = b.getVelocity() + b.getRotation().cross(rb) - a.getVelocity() - a.getRotation().cross(ra);
		const V value = axis * (relative.dot(axis) / mass);
		joint.anchorImpulse += value;
		impulse(a, value, ra, false);
		impulse(b, -value, rb, false);
	}
	const auto frame = a.getOrientation() * joint.frameA;
	const auto error = jointAngularError(joint, a, b);
	for (int index = 0; index < 3; ++index) {
		V local {};
		local[index] = 1;
		const auto axis = frame * local;
		const float mass = axis.dot(inertia(a, axis) + inertia(b, axis));
		if (mass <= 1e-9f) {
			continue;
		}
		const float relative = (b.getRotation() - a.getRotation()).dot(axis);
		const bool locked = (index > 0 && joint.kind == RigidJointKind::Hinge) || (index == 0 && joint.twistMin == joint.twistMax);
		float correction = relative * joint.damping;
		if (locked || error[index] * relative > 0) {
			correction = relative;
		}
		// Bias is bounded independently of the timestep to avoid explosive recovery.
		correction += std::clamp(.1f * error[index] / dt, -2.f, 2.f);
		angularImpulse(a, axis * (correction / mass), false);
		angularImpulse(b, axis * (-correction / mass), false);
	}
}
void solveJointPosition(const RigidJoint& joint, RigidBody& a, RigidBody& b) {
	for (int index = 0; index < 3; ++index) {
		V axis {};
		axis[index] = 1;
		const auto ra = a.getDirectionInWorldSpace(joint.anchorA);
		const auto rb = b.getDirectionInWorldSpace(joint.anchorB);
		const float mass = effectiveMass(a, b, ra, rb, axis);
		if (mass <= 1e-9f) {
			continue;
		}
		const float error = (b.getPosition() + rb - a.getPosition() - ra).dot(axis);
		const auto value = axis * (std::clamp(.7f * error, -.1f, .1f) / mass);
		impulse(a, value, ra, true);
		impulse(b, -value, rb, true);
	}
	const auto error = jointAngularError(joint, a, b);
	const auto frame = a.getOrientation() * joint.frameA;
	for (int index = 0; index < 3; ++index) {
		V local {};
		local[index] = 1;
		const auto axis = frame * local;
		const float mass = axis.dot(inertia(a, axis) + inertia(b, axis));
		if (mass > 1e-9f) {
			const auto value = axis * (std::clamp(.5f * error[index], -.1f, .1f) / mass);
			angularImpulse(a, value, true);
			angularImpulse(b, -value, true);
		}
	}
}
}
