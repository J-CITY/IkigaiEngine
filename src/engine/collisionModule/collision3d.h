#pragma once
#include "convexPolyhedron.h"
#include "mathModule/geometry/aabb.h"
#include "mathModule/math.h"
#include "status.h"
#include <functional>

namespace IKIGAI::COLLISION {

using Vec3 = MATH::Vector3f;

struct Shape3D {
	enum class Kind { Sphere, Box, Convex };
	Kind kind = Kind::Sphere;
	float radius = 0.5f;
	Vec3 halfSize { 0.5f, 0.5f, 0.5f };
	std::shared_ptr<const ConvexPolyhedron> polyhedron {};
	static Shape3D sphere(float radius);
	static Shape3D box(Vec3 halfSize);
	static Shape3D convex(std::shared_ptr<const ConvexPolyhedron> polyhedron);
};

struct ShapeView3D {
	const Shape3D& shape;
	Vec3 position {};
	std::array<Vec3, 3> axes { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
	Vec3 world(Vec3 p) const {
		return position + axes[0] * p.x + axes[1] * p.y + axes[2] * p.z;
	}
	Vec3 local(Vec3 p) const {
		auto d = p - position;
		return { d.dot(axes[0]), d.dot(axes[1]), d.dot(axes[2]) };
	}
};

struct ManifoldPoint3D {
	Vec3 position;
	float penetration = 0;
};

struct Manifold3D {
	Vec3 normal;
	std::array<ManifoldPoint3D, 4> points {};
	int count = 0;
};

MATH::GEOMETRY::Aabb<3> bounds(const ShapeView3D& shape);

class INarrowPhase3D {
public:
	virtual ~INarrowPhase3D() = default;
	virtual Status collide(const ShapeView3D& a, const ShapeView3D& b, Manifold3D& out) const = 0;
};

class DefaultNarrowPhase3D : public INarrowPhase3D {
public:
	using Handler = std::function<Status(const ShapeView3D&, const ShapeView3D&, Manifold3D&)>;
	void setHandler(Shape3D::Kind a, Shape3D::Kind b, Handler handler);
	Status collide(const ShapeView3D& a, const ShapeView3D& b, Manifold3D& out) const override;

private:
	std::array<Handler, 9> mHandlers;
};
}
