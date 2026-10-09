#pragma once

#include <vector>
#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>

#define EULER_INTEGRATION
//#define ACCURATE_EULER_INTEGRATION

float Dot(const glm::vec3& l, const glm::vec3& r);
float Magnitude(const glm::vec3& v);
glm::vec3 Normalized(const glm::vec3& v);
void Normalize(glm::vec3& v);
float MagnitudeSq(const glm::vec3& v);

struct OBB {
	glm::vec3 position;
	glm::vec3 size; // HALF SIZE!
	glm::mat3 orientation;

	inline OBB() : size(1, 1, 1) { }
	inline OBB(const glm::vec3& p, const glm::vec3& s) :
		position(p), size(s) { }
	inline OBB(const glm::vec3& p, const glm::vec3& s, const glm::mat3& o) :
		position(p), size(s), orientation(o) { }
};
class ClothParticle {
	glm::vec3 position;
	glm::vec3 oldPosition;
	glm::vec3 forces;

	glm::vec3 gravity;
	float friction;
	float bounce;

#ifdef EULER_INTEGRATION
	glm::vec3 velocity = glm::vec3(0);
#endif
	float mass;
public:
	ClothParticle();

	void Update(float deltaTime);
	//void Render();
	void ApplyForces();
	void SolveConstraints(const std::vector<OBB>& constraints);

	void SetPosition(const glm::vec3& pos);
	glm::vec3 GetPosition();

	void SetBounce(float b);
	float GetBounce();

	void AddImpulse(const glm::vec3& impulse);
	float InvMass();
	void SetMass(float m);
	glm::vec3 GetVelocity();
	void SetFriction(float f);
};
