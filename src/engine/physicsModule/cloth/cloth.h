#pragma once

#include <memory>

#include "clothParticle.h"
#include "spring.h"
#include <vector>

class ClothObj {
public:
//protected:
	std::vector<std::shared_ptr<ClothParticle>> verts;
	std::vector<std::shared_ptr<Spring>> structural;
	std::vector<std::shared_ptr<Spring>> shear;
	std::vector<std::shared_ptr<Spring>> bend;
	float clothSize;
public:
	// Public API
	void Initialize(int gridSize, float distance, const glm::vec3& position);

	void SetStructuralSprings(float k, float b);
	void SetShearSprings(float k, float b);
	void SetBendSprings(float k, float b);
	void SetParticleMass(float mass);

	// For Physics System
	void ApplyForces();
	void Update(float dt);
	void SolveConstraints(const std::vector<OBB>& constraints);
	void ApplySpringForces(float dt);
	//void Render(bool debug);
};
