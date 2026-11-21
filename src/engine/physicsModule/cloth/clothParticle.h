#pragma once

#include "mathModule/math.h"
#include <vector>


#define EULER_INTEGRATION
// #define ACCURATE_EULER_INTEGRATION

float Dot(const IKIGAI::MATH::Vector3f& l, const IKIGAI::MATH::Vector3f& r);
float Magnitude(const IKIGAI::MATH::Vector3f& v);
IKIGAI::MATH::Vector3f Normalized(const IKIGAI::MATH::Vector3f& v);
void Normalize(IKIGAI::MATH::Vector3f& v);
float MagnitudeSq(const IKIGAI::MATH::Vector3f& v);

struct OBB {
	IKIGAI::MATH::Vector3f position {};
	IKIGAI::MATH::Vector3f size {}; // HALF SIZE!
	IKIGAI::MATH::Matrix3f orientation = IKIGAI::MATH::Matrix3f::Identity;

	inline OBB()
		: size(1, 1, 1) { }
	inline OBB(const IKIGAI::MATH::Vector3f& p, const IKIGAI::MATH::Vector3f& s)
		: position(p)
		, size(s) { }
	inline OBB(const IKIGAI::MATH::Vector3f& p, const IKIGAI::MATH::Vector3f& s, const IKIGAI::MATH::Matrix3f& o)
		: position(p)
		, size(s)
		, orientation(o) { }
};
class ClothParticle {
	IKIGAI::MATH::Vector3f position {};
	IKIGAI::MATH::Vector3f oldPosition {};
	IKIGAI::MATH::Vector3f forces {};

	IKIGAI::MATH::Vector3f gravity {};
	float friction;
	float bounce;

#ifdef EULER_INTEGRATION
	IKIGAI::MATH::Vector3f velocity {};
#endif
	float mass;

public:
	ClothParticle();

	void Update(float deltaTime);
	// void Render();
	void ApplyForces();
	void SolveConstraints(const std::vector<OBB>& constraints);

	void SetPosition(const IKIGAI::MATH::Vector3f& pos);
	IKIGAI::MATH::Vector3f GetPosition();

	void SetBounce(float b);
	float GetBounce();

	void AddImpulse(const IKIGAI::MATH::Vector3f& impulse);
	float InvMass();
	void SetMass(float m);
	IKIGAI::MATH::Vector3f GetVelocity();
	void SetFriction(float f);
};
