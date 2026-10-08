#pragma once


#include <memory>

#include "clothParticle.h"

// F = -kx-bv
// k - spring "tightness" constant [negative to 0] rigid at 0
// x - displacement of spring from equalibrium
// b - constant (coefficient) dampening
// v - realtive velocity of points of spring

class Spring {
public:
	std::shared_ptr<ClothParticle> p1;
	std::shared_ptr<ClothParticle> p2;

	float k; // [-x to 0] higher = stiff sprint, lower = loose spring
	float restingLength;
	float b;
public:
	Spring(float _k, float _b, float len);
	void SetParticles(std::shared_ptr<ClothParticle> _p1, std::shared_ptr<ClothParticle> _p2);
	std::shared_ptr<ClothParticle> GetP1();
	std::shared_ptr<ClothParticle> GetP2();
	void SetConstants(float _k, float _b);
	void ApplyForce(float dt);
};
