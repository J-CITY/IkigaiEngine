#include "clothParticle.h"

#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>


struct Line;

float Dot(const glm::vec3& l, const glm::vec3& r) {
	return l.x * r.x + l.y * r.y + l.z * r.z;
}

float Magnitude(const glm::vec3& v) {
	return sqrtf(Dot(v, v));
}

glm::vec3 Normalized(const glm::vec3& v) {
	return v * (1.0f / Magnitude(v));
}
void Normalize(glm::vec3& v) {
	v = v * (1.0f / Magnitude(v));
}
float MagnitudeSq(const glm::vec3& v) {
	return Dot(v, v);
}


struct Line {
	glm::vec3 start;
	glm::vec3 end;

	inline Line() {}
	inline Line(const glm::vec3& s, const glm::vec3& e) :
		start(s), end(e) { }
};
float LengthSq(const Line& line) {
	return MagnitudeSq(line.start - line.end);
}
struct Ray {
	glm::vec3 origin;
	glm::vec3 direction;

	inline Ray() : direction(0.0f, 0.0f, 1.0f) {}
	inline Ray(const glm::vec3& o, const glm::vec3& d) :
		origin(o), direction(d) {
		NormalizeDirection();
	}
	inline void NormalizeDirection() {
		Normalize(direction);
	}
};

struct RaycastResult {
	glm::vec3 point;
	glm::vec3 normal;
	float t;
	bool hit;
};

#define CMP(x, y) \
	(fabsf(x - y) <= std::numeric_limits<float>::epsilon() * fmaxf(1.0f, fmaxf(fabsf(x), fabsf(y))))
void ResetRaycastResult(RaycastResult* outResult) {
	if (outResult != 0) {
		outResult->t = -1;
		outResult->hit = false;
		outResult->normal = glm::vec3(0, 0, 1);
		outResult->point = glm::vec3(0, 0, 0);
	}
}

struct AABB {
	glm::vec3 position;
	glm::vec3 size; // HALF SIZE!

	inline AABB() : size(1, 1, 1) { }
	inline AABB(const glm::vec3& p, const glm::vec3& s) :
		position(p), size(s) { }
};

bool Raycast(const OBB& obb, const Ray& ray, RaycastResult* outResult) {
	ResetRaycastResult(outResult);

	const float o[9] = {
		obb.orientation[0][0], obb.orientation[0][1], obb.orientation[0][2],
		obb.orientation[1][0], obb.orientation[1][1], obb.orientation[1][2],
		obb.orientation[2][0], obb.orientation[2][1], obb.orientation[2][2]
	};
	const float size[3] = { obb.size[0], obb.size[1], obb.size[2] };

	glm::vec3 p = obb.position - ray.origin;

	glm::vec3 X(o[0], o[1], o[2]);
	glm::vec3 Y(o[3], o[4], o[5]);
	glm::vec3 Z(o[6], o[7], o[8]);

	glm::vec3 f(
		Dot(X, ray.direction),
		Dot(Y, ray.direction),
		Dot(Z, ray.direction)
	);

	glm::vec3 e(
		Dot(X, p),
		Dot(Y, p),
		Dot(Z, p)
	);

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

		glm::vec3 normals[] = {
			X,			// +x
			X * -1.0f,	// -x
			Y,			// +y
			Y * -1.0f,	// -y
			Z,			// +z
			Z * -1.0f	// -z
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
	glm::vec3 position;
	float radius;

	inline Sphere() : radius(1.0f) { }
	inline Sphere(const glm::vec3& p, float r) :
		position(p), radius(r) { }
};
glm::vec3 ClosestPoint(const Line& line, const glm::vec3& point) {
	glm::vec3 lVec = line.end - line.start; // Line Vector
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
bool PointOnLine(const glm::vec3& point, const Line& line) {
	glm::vec3 closest = ClosestPoint(line, point);
	float distanceSq = MagnitudeSq(closest - point);
	return CMP(distanceSq, 0.0f);
}
bool PointOnRay(const glm::vec3& point, const Ray& ray) {
	if (point == ray.origin) {
		return true;
	}

	glm::vec3 norm = point - ray.origin;
	Normalize(norm);
	float diff = Dot(norm, ray.direction); // Direction is normalized
	// If BOTH vectors point in the same direction, diff should be 1
	return CMP(diff, 1.0f);
}
bool PointInSphere(const glm::vec3& point, const Sphere& sphere) {
	return MagnitudeSq(point - sphere.position) < sphere.radius * sphere.radius;
}

glm::vec3 GetMin(const AABB& aabb) {
	glm::vec3 p1 = aabb.position + aabb.size;
	glm::vec3 p2 = aabb.position - aabb.size;

	return glm::vec3(fminf(p1.x, p2.x), fminf(p1.y, p2.y), fminf(p1.z, p2.z));
}

glm::vec3 GetMax(const AABB& aabb) {
	glm::vec3 p1 = aabb.position + aabb.size;
	glm::vec3 p2 = aabb.position - aabb.size;

	return glm::vec3(fmaxf(p1.x, p2.x), fmaxf(p1.y, p2.y), fmaxf(p1.z, p2.z));
}
bool PointInAABB(const glm::vec3& point, const AABB& aabb) {
	glm::vec3 min = GetMin(aabb);
	glm::vec3 max = GetMax(aabb);

	if (point.x < min.x || point.y < min.y || point.z < min.z) {
		return false;
	}
	if (point.x > max.x || point.y > max.y || point.z > max.z) {
		return false;
	}

	return true;
}
bool PointInOBB(const glm::vec3& point, const OBB& obb) {
	glm::vec3 dir = point - obb.position;

	for (int i = 0; i < 3; ++i) {
		const float orientation[3] = { obb.orientation[i][0], obb.orientation[i][1], obb.orientation[i][2] };
		glm::vec3 axis(orientation[0], orientation[1], orientation[2]);

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
	//type = RIGIDBODY_TYPE_PARTICLE;
	friction = 0.95f;
	bounce = 0.7f;
	gravity = glm::vec3(0.0f, -9.82f, 0.0f);

#ifdef EULER_INTEGRATION
	mass = 1.0f;
#endif
}

void ClothParticle::Update(float deltaTime) {
#ifdef EULER_INTEGRATION
	oldPosition = position;
	glm::vec3 acceleration = forces *InvMass();
	#ifdef ACCURATE_EULER_INTEGRATION
		glm::vec3 oldVelocity = velocity;
		velocity = velocity * friction + acceleration * deltaTime;
		position = position + (oldVelocity + velocity) * 0.5f * deltaTime;
	#else
		velocity = velocity * friction + acceleration * deltaTime;
		position = position + velocity * deltaTime;
	#endif
#else
	glm::vec3 velocity = position - oldPosition;
	oldPosition = position;
	float deltaSquare = deltaTime * deltaTime;
	position = position + (velocity * friction + forces * deltaSquare);
#endif
}

//void Particle::Render() {
//	Sphere visual(position, 0.1f);
//	::Render(visual);
//}

void ClothParticle::ApplyForces() {
#ifdef EULER_INTEGRATION
	forces = gravity *mass;
#else
	forces = gravity *mass;
#endif
}

void ClothParticle::SolveConstraints(const std::vector<OBB>& constraints) {
	int size = constraints.size();
	for (int i = 0; i < size; ++i) {
		Line traveled(oldPosition, position);
		if (Linetest(constraints[i], traveled)) {
			//if (PointInOBB(position, constraints[i])) {
#ifndef EULER_INTEGRATION
			glm::vec3 velocity = position - oldPosition;
#endif
			glm::vec3 direction = Normalized(velocity);
			Ray ray(oldPosition, direction);
			RaycastResult result;

			if (Raycast(constraints[i], ray, &result)) {
				// Place object just a little above collision result
				position = result.point + result.normal * 0.3f;

				glm::vec3 vn = result.normal * Dot(result.normal, velocity);
				glm::vec3 vt = velocity - vn;

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

void ClothParticle::SetPosition(const glm::vec3& pos) {
	position = pos;
	oldPosition = pos;
}

glm::vec3 ClothParticle::GetPosition() {
	return position;
}

void ClothParticle::SetBounce(float b) {
	bounce = b;
}

float ClothParticle::GetBounce() {
	return bounce;
}

void ClothParticle::AddImpulse(const glm::vec3& impulse) {
#ifdef EULER_INTEGRATION
	velocity = velocity + impulse;
#else
	glm::vec3 velocity = position - oldPosition;
	velocity = velocity + impulse;
	if (impulse.x > 0 || impulse.y > 0 || impulse.z > 0)
	{
		int a=0;
	}
	oldPosition = position - velocity;
#endif
}

float ClothParticle::InvMass() {
	if (mass == 0.0f) { return 0.0f; }
	return 1.0f / mass;
}

void ClothParticle::SetMass(float m) {
	mass = m;
}

glm::vec3 ClothParticle::GetVelocity() {
#ifdef EULER_INTEGRATION
	return velocity;
#else
	return position - oldPosition;
#endif
}

void ClothParticle::SetFriction(float f) {
	friction = f;
}