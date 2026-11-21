#include "clothParticle.h"
#include <cmath>
#include <limits>
#include <stdexcept>

struct Line;

float Dot(const IKIGAI::MATH::Vector3f& l, const IKIGAI::MATH::Vector3f& r) {
	return IKIGAI::MATH::Vector3f::Dot(l, r);
}

float Magnitude(const IKIGAI::MATH::Vector3f& v) {
	return IKIGAI::MATH::Vector3f::Length(v);
}

IKIGAI::MATH::Vector3f Normalized(const IKIGAI::MATH::Vector3f& v) {
	float length = Magnitude(v);
	return length > 1e-8f ? v * (1.f / length) : IKIGAI::MATH::Vector3f {};
}
void Normalize(IKIGAI::MATH::Vector3f& v) {
	v = Normalized(v);
}
float MagnitudeSq(const IKIGAI::MATH::Vector3f& v) {
	return IKIGAI::MATH::Vector3f::LengthSquare(v);
}

struct Line {
	IKIGAI::MATH::Vector3f start;
	IKIGAI::MATH::Vector3f end;

	inline Line() { }
	inline Line(const IKIGAI::MATH::Vector3f& s, const IKIGAI::MATH::Vector3f& e)
		: start(s)
		, end(e) { }
};
float LengthSq(const Line& line) {
	return MagnitudeSq(line.start - line.end);
}
struct Ray {
	IKIGAI::MATH::Vector3f origin;
	IKIGAI::MATH::Vector3f direction;

	inline Ray()
		: direction(0.0f, 0.0f, 1.0f) { }
	inline Ray(const IKIGAI::MATH::Vector3f& o, const IKIGAI::MATH::Vector3f& d)
		: origin(o)
		, direction(d) {
		NormalizeDirection();
	}
	inline void NormalizeDirection() {
		Normalize(direction);
	}
};

struct RaycastResult {
	IKIGAI::MATH::Vector3f point;
	IKIGAI::MATH::Vector3f normal;
	float t;
	bool hit;
};

#define CMP(x, y) (fabsf(x - y) <= std::numeric_limits<float>::epsilon() * fmaxf(1.0f, fmaxf(fabsf(x), fabsf(y))))
void ResetRaycastResult(RaycastResult* outResult) {
	if (outResult != 0) {
		outResult->t = -1;
		outResult->hit = false;
		outResult->normal = IKIGAI::MATH::Vector3f(0, 0, 1);
		outResult->point = IKIGAI::MATH::Vector3f(0, 0, 0);
	}
}

struct AABB {
	IKIGAI::MATH::Vector3f position;
	IKIGAI::MATH::Vector3f size; // HALF SIZE!

	inline AABB()
		: size(1, 1, 1) { }
	inline AABB(const IKIGAI::MATH::Vector3f& p, const IKIGAI::MATH::Vector3f& s)
		: position(p)
		, size(s) { }
};

bool Raycast(const OBB& obb, const Ray& ray, RaycastResult* outResult) {
	ResetRaycastResult(outResult);

	const float size[3] = { obb.size[0], obb.size[1], obb.size[2] };

	IKIGAI::MATH::Vector3f p = obb.position - ray.origin;

	const auto X = IKIGAI::MATH::Matrix3f::GetColumn(obb.orientation, 0);
	const auto Y = IKIGAI::MATH::Matrix3f::GetColumn(obb.orientation, 1);
	const auto Z = IKIGAI::MATH::Matrix3f::GetColumn(obb.orientation, 2);

	IKIGAI::MATH::Vector3f f(Dot(X, ray.direction), Dot(Y, ray.direction), Dot(Z, ray.direction));

	IKIGAI::MATH::Vector3f e(Dot(X, p), Dot(Y, p), Dot(Z, p));

#if 1
	float t[6] = { 0, 0, 0, 0, 0, 0 };
	for (int i = 0; i < 3; ++i) {
		if (CMP(f[i], 0)) {
			if (-e[i] - size[i] > 0 || -e[i] + size[i] < 0) {
				return false;
			}
			f[i] = 0.00001f; // Avoid div by 0!
		}

		t[i * 2 + 0] = (e[i] + size[i]) / f[i]; // tmin[x, y, z]
		t[i * 2 + 1] = (e[i] - size[i]) / f[i]; // tmax[x, y, z]
	}

	float tmin = fmaxf(fmaxf(fminf(t[0], t[1]), fminf(t[2], t[3])), fminf(t[4], t[5]));
	float tmax = fminf(fminf(fmaxf(t[0], t[1]), fmaxf(t[2], t[3])), fmaxf(t[4], t[5]));
#else
	// The above loop simplifies the below if statements
	// this is done to make sure the sample fits into the book
	if (CMP(f.x, 0)) {
		if (-e.x - obb.size.x > 0 || -e.x + obb.size.x < 0) {
			return -1;
		}
		f.x = 0.00001f; // Avoid div by 0!
	}
	else if (CMP(f.y, 0)) {
		if (-e.y - obb.size.y > 0 || -e.y + obb.size.y < 0) {
			return -1;
		}
		f.y = 0.00001f; // Avoid div by 0!
	}
	else if (CMP(f.z, 0)) {
		if (-e.z - obb.size.z > 0 || -e.z + obb.size.z < 0) {
			return -1;
		}
		f.z = 0.00001f; // Avoid div by 0!
	}
	float t1 = (e.x + obb.size.x) / f.x;
	float t2 = (e.x - obb.size.x) / f.x;
	float t3 = (e.y + obb.size.y) / f.y;
	float t4 = (e.y - obb.size.y) / f.y;
	float t5 = (e.z + obb.size.z) / f.z;
	float t6 = (e.z - obb.size.z) / f.z;
	float tmin = fmaxf(fmaxf(fminf(t1, t2), fminf(t3, t4)), fminf(t5, t6));
	float tmax = fminf(fminf(fmaxf(t1, t2), fmaxf(t3, t4)), fmaxf(t5, t6));
#endif

	// if tmax < 0, ray is intersecting AABB
	// but entire AABB is behing it's origin
	if (tmax < 0) {
		return false;
	}

	// if tmin > tmax, ray doesn't intersect AABB
	if (tmin > tmax) {
		return false;
	}

	// If tmin is < 0, tmax is closer
	float t_result = tmin;

	if (tmin < 0.0f) {
		t_result = tmax;
	}

	if (outResult != 0) {
		outResult->hit = true;
		outResult->t = t_result;
		outResult->point = ray.origin + ray.direction * t_result;

		IKIGAI::MATH::Vector3f normals[] = {
			X, // +x
			X * -1.0f, // -x
			Y, // +y
			Y * -1.0f, // -y
			Z, // +z
			Z * -1.0f // -z
		};

		for (int i = 0; i < 6; ++i) {
			if (CMP(t_result, t[i])) {
				outResult->normal = Normalized(normals[i]);
			}
		}
	}
	return true;
}
struct Sphere {
	IKIGAI::MATH::Vector3f position;
	float radius;

	inline Sphere()
		: radius(1.0f) { }
	inline Sphere(const IKIGAI::MATH::Vector3f& p, float r)
		: position(p)
		, radius(r) { }
};
IKIGAI::MATH::Vector3f ClosestPoint(const Line& line, const IKIGAI::MATH::Vector3f& point) {
	IKIGAI::MATH::Vector3f lVec = line.end - line.start; // Line Vector
	// Project "point" onto the "Line Vector", computing:
	// closest(t) = start + t * (end - start)
	// T is how far along the line the projected point is
	float t = Dot(point - line.start, lVec) / Dot(lVec, lVec);
	// Clamp t to the 0 to 1 range
	t = fmaxf(t, 0.0f);
	t = fminf(t, 1.0f);
	// Return projected position of t
	return line.start + lVec * t;
}
bool PointOnLine(const IKIGAI::MATH::Vector3f& point, const Line& line) {
	IKIGAI::MATH::Vector3f closest = ClosestPoint(line, point);
	float distanceSq = MagnitudeSq(closest - point);
	return CMP(distanceSq, 0.0f);
}
bool PointOnRay(const IKIGAI::MATH::Vector3f& point, const Ray& ray) {
	if (point.x == ray.origin.x && point.y == ray.origin.y && point.z == ray.origin.z) {
		return true;
	}

	IKIGAI::MATH::Vector3f norm = point - ray.origin;
	Normalize(norm);
	float diff = Dot(norm, ray.direction); // Direction is normalized
	// If BOTH vectors point in the same direction, diff should be 1
	return CMP(diff, 1.0f);
}
bool PointInSphere(const IKIGAI::MATH::Vector3f& point, const Sphere& sphere) {
	return MagnitudeSq(point - sphere.position) < sphere.radius * sphere.radius;
}

IKIGAI::MATH::Vector3f GetMin(const AABB& aabb) {
	IKIGAI::MATH::Vector3f p1 = aabb.position + aabb.size;
	IKIGAI::MATH::Vector3f p2 = aabb.position - aabb.size;

	return IKIGAI::MATH::Vector3f(fminf(p1.x, p2.x), fminf(p1.y, p2.y), fminf(p1.z, p2.z));
}

IKIGAI::MATH::Vector3f GetMax(const AABB& aabb) {
	IKIGAI::MATH::Vector3f p1 = aabb.position + aabb.size;
	IKIGAI::MATH::Vector3f p2 = aabb.position - aabb.size;

	return IKIGAI::MATH::Vector3f(fmaxf(p1.x, p2.x), fmaxf(p1.y, p2.y), fmaxf(p1.z, p2.z));
}
bool PointInAABB(const IKIGAI::MATH::Vector3f& point, const AABB& aabb) {
	IKIGAI::MATH::Vector3f min = GetMin(aabb);
	IKIGAI::MATH::Vector3f max = GetMax(aabb);

	if (point.x < min.x || point.y < min.y || point.z < min.z) {
		return false;
	}
	if (point.x > max.x || point.y > max.y || point.z > max.z) {
		return false;
	}

	return true;
}
bool PointInOBB(const IKIGAI::MATH::Vector3f& point, const OBB& obb) {
	IKIGAI::MATH::Vector3f dir = point - obb.position;

	for (int i = 0; i < 3; ++i) {
		const auto axis = IKIGAI::MATH::Matrix3f::GetColumn(obb.orientation, i);

		float distance = Dot(dir, axis);

		if (distance > obb.size[i]) {
			return false;
		}
		if (distance < -obb.size[i]) {
			return false;
		}
	}

	return true;
}
bool Linetest(const OBB& obb, const Line& line) {
	if (MagnitudeSq(line.end - line.start) < 0.0000001f) {
		return PointInOBB(line.start, obb);
	}
	Ray ray;
	ray.origin = line.start;
	ray.direction = Normalized(line.end - line.start);
	RaycastResult result;
	if (!Raycast(obb, ray, &result)) {
		return false;
	}
	float t = result.t;

	return t >= 0 && t * t <= LengthSq(line);
}

ClothParticle::ClothParticle() {
	// type = RIGIDBODY_TYPE_PARTICLE;
	friction = 0.95f;
	bounce = 0.7f;
	gravity = IKIGAI::MATH::Vector3f(0.0f, -9.82f, 0.0f);

#ifdef EULER_INTEGRATION
	mass = 1.0f;
#endif
}

void ClothParticle::Update(float deltaTime) {
	if (mass == 0)
		return;
#ifdef EULER_INTEGRATION
	oldPosition = position;
	IKIGAI::MATH::Vector3f acceleration = forces * InvMass();
#ifdef ACCURATE_EULER_INTEGRATION
	IKIGAI::MATH::Vector3f oldVelocity = velocity;
	velocity = velocity * std::pow(friction, deltaTime * 60) + acceleration * deltaTime;
	position = position + (oldVelocity + velocity) * 0.5f * deltaTime;
#else
	velocity = velocity * std::pow(friction, deltaTime * 60) + acceleration * deltaTime;
	position = position + velocity * deltaTime;
#endif
#else
	IKIGAI::MATH::Vector3f velocity = position - oldPosition;
	oldPosition = position;
	float deltaSquare = deltaTime * deltaTime;
	position = position + (velocity * friction + forces * deltaSquare);
#endif
}

// void Particle::Render() {
//	Sphere visual(position, 0.1f);
//	::Render(visual);
// }

void ClothParticle::ApplyForces() {
#ifdef EULER_INTEGRATION
	forces = gravity * mass;
#else
	forces = gravity * mass;
#endif
}

void ClothParticle::SolveConstraints(const std::vector<OBB>& constraints) {
	int size = constraints.size();
	for (int i = 0; i < size; ++i) {
		Line traveled(oldPosition, position);
		if (Linetest(constraints[i], traveled)) {
			// if (PointInOBB(position, constraints[i])) {
#ifndef EULER_INTEGRATION
			IKIGAI::MATH::Vector3f velocity = position - oldPosition;
#endif
			IKIGAI::MATH::Vector3f direction = Normalized(velocity);
			Ray ray(oldPosition, direction);
			RaycastResult result;

			if (Raycast(constraints[i], ray, &result)) {
				// Place object just a little above collision result
				position = result.point + result.normal * 0.3f;

				IKIGAI::MATH::Vector3f vn = result.normal * Dot(result.normal, velocity);
				IKIGAI::MATH::Vector3f vt = velocity - vn;

#ifdef EULER_INTEGRATION
				oldPosition = position;
				velocity = vt - vn * bounce;
#else
				oldPosition = position - (vt - vn * bounce);
#endif
				break;
			}
		}
	}
}

void ClothParticle::SetPosition(const IKIGAI::MATH::Vector3f& pos) {
	position = pos;
	oldPosition = pos;
}

IKIGAI::MATH::Vector3f ClothParticle::GetPosition() {
	return position;
}

void ClothParticle::SetBounce(float b) {
	bounce = b;
}

float ClothParticle::GetBounce() {
	return bounce;
}

void ClothParticle::AddImpulse(const IKIGAI::MATH::Vector3f& impulse) {
#ifdef EULER_INTEGRATION
	velocity = velocity + impulse;
#else
	IKIGAI::MATH::Vector3f velocity = position - oldPosition;
	velocity = velocity + impulse;
	if (impulse.x > 0 || impulse.y > 0 || impulse.z > 0) {
		int a = 0;
	}
	oldPosition = position - velocity;
#endif
}

float ClothParticle::InvMass() {
	if (mass == 0.0f) {
		return 0.0f;
	}
	return 1.0f / mass;
}

void ClothParticle::SetMass(float m) {
	if (!std::isfinite(m) || m < 0)
		throw std::invalid_argument("Invalid cloth particle mass");
	mass = m;
}

IKIGAI::MATH::Vector3f ClothParticle::GetVelocity() {
#ifdef EULER_INTEGRATION
	return velocity;
#else
	return position - oldPosition;
#endif
}

void ClothParticle::SetFriction(float f) {
	friction = f;
}
