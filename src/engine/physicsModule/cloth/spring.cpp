#include "spring.h"

Spring::Spring(float _k, float _b, float len) {
	k = _k;
	b = _b;
	restingLength = len;
}

void Spring::SetParticles(std::shared_ptr<ClothParticle> _p1, std::shared_ptr<ClothParticle> _p2) {
	p1 = _p1;
	p2 = _p2;
}

std::shared_ptr<ClothParticle> Spring::GetP1() {
	return p1;
}

std::shared_ptr<ClothParticle> Spring::GetP2() {
	return p2;
}

void Spring::SetConstants(float _k, float _b) {
	k = _k;
	b = _b;
}

void Spring::ApplyForce(float dt) {
	IKIGAI::MATH::Vector3f relPos = p2->GetPosition() - p1->GetPosition();
	IKIGAI::MATH::Vector3f relVel = p2->GetVelocity() - p1->GetVelocity();

	// Prevent underflow
	for (int i = 0; i < 3; ++i) {
		relPos[i] = (fabsf(relPos[i]) < 0.0000001f) ? 0.0f : relPos[i];
		relVel[i] = (fabsf(relVel[i]) < 0.0000001f) ? 0.0f : relVel[i];
	}

	float distance = Magnitude(relPos);
	if (distance < 1e-8f || dt <= 0)
		return;
	IKIGAI::MATH::Vector3f direction = relPos * (1.f / distance);
	float x = distance - restingLength;
	float v = Dot(relVel, direction);

	float F = (-k * x) + (-b * v);

	IKIGAI::MATH::Vector3f impulse = direction * (F * dt);
	p1->AddImpulse(impulse * -p1->InvMass());
	p2->AddImpulse(impulse * p2->InvMass());
}
