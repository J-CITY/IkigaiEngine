#pragma once

#include "body.h"
#include "mathModule/math.h"

namespace IKIGAI::PHYSICS {

class ContactResolver;

class Contact {
	// ... Other data as before ...

	friend class ContactResolver;

public:
	RigidBody* body[2];

	float friction;

	float restitution;

	MATH::Vector3f contactPoint;

	MATH::Vector3f contactNormal;

	float penetration;
	float normalImpulse = 0;
	float closingSpeed = 0;

	void setBodyData(RigidBody* one, RigidBody* two, float friction, float restitution);

protected:
	MATH::Matrix3f contactToWorld = MATH::Matrix3f(0.0f);

	MATH::Vector3f contactVelocity;

	float desiredDeltaVelocity;

	MATH::Vector3f relativeContactPosition[2];

protected:
	void calculateInternals(float duration);

	void swapBodies();

	void matchAwakeState();

	void calculateDesiredDeltaVelocity(float duration);

	MATH::Vector3f calculateLocalVelocity(unsigned bodyIndex, float duration);

	void calculateContactBasis();

	void applyImpulse(const MATH::Vector3f& impulse, RigidBody* body, MATH::Vector3f* velocityChange, MATH::Vector3f* rotationChange);

	void applyVelocityChange(MATH::Vector3f velocityChange[2], MATH::Vector3f rotationChange[2]);

	void applyPositionChange(MATH::Vector3f linearChange[2], MATH::Vector3f angularChange[2], float penetration);

	MATH::Vector3f calculateFrictionlessImpulse(MATH::Matrix3f* inverseInertiaTensor);

	MATH::Vector3f calculateFrictionImpulse(MATH::Matrix3f* inverseInertiaTensor);
};

class ContactResolver {
protected:
	unsigned velocityIterations;

	unsigned positionIterations;

	float velocityEpsilon;

	float positionEpsilon;

public:
	unsigned velocityIterationsUsed;

	unsigned positionIterationsUsed;

private:
	bool validSettings;

public:
	ContactResolver(unsigned iterations, float velocityEpsilon = (float)0.01, float positionEpsilon = (float)0.01);

	ContactResolver(
		unsigned velocityIterations, unsigned positionIterations, float velocityEpsilon = (float)0.01, float positionEpsilon = (float)0.01);

	bool isValid() {
		return (velocityIterations > 0) && (positionIterations > 0) && (positionEpsilon >= 0.0f) && (positionEpsilon >= 0.0f);
	}

	void setIterations(unsigned velocityIterations, unsigned positionIterations);

	void setIterations(unsigned iterations);

	void setEpsilon(float velocityEpsilon, float positionEpsilon);

	void resolveContacts(Contact* contactArray, unsigned numContacts, float duration);

protected:
	void prepareContacts(Contact* contactArray, unsigned numContacts, float duration);

	void adjustVelocities(Contact* contactArray, unsigned numContacts, float duration);

	void adjustPositions(Contact* contacts, unsigned numContacts, float duration);
};

class ContactGenerator {
public:
	virtual unsigned addContact(Contact* contact, unsigned limit) const = 0;
};

}
