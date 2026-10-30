#include "collision3d.h"
#include "convexCollision.h"
#include <limits>

namespace IKIGAI::COLLISION {

Shape3D Shape3D::sphere(float radius) {
	if (!std::isfinite(radius) || radius <= 0) {
		throw std::invalid_argument("Invalid sphere radius");
	}
	Shape3D s;
	s.radius = radius;
	return s;
}

Shape3D Shape3D::box(Vec3 half) {
	for (int i = 0; i < 3; ++i) {
		if (!std::isfinite(half[i]) || half[i] <= 0) {
			throw std::invalid_argument("Invalid box half size");
		}
	}
	Shape3D s;
	s.kind = Kind::Box;
	s.halfSize = half;
	return s;
}

Shape3D Shape3D::convex(std::shared_ptr<const ConvexPolyhedron> polyhedron) {
	if (!polyhedron || polyhedron->faces.size() < 4 || !(polyhedron->volume > 0)) {
		throw std::invalid_argument("Invalid convex polyhedron");
	}
	Shape3D shape {};
	shape.kind = Kind::Convex;
	shape.polyhedron = std::move(polyhedron);
	return shape;
}

MATH::GEOMETRY::Aabb<3> bounds(const ShapeView3D& s) {
	if (s.shape.kind == Shape3D::Kind::Convex) {
		MATH::GEOMETRY::Aabb<3> out;
		out.min.fill(std::numeric_limits<float>::max());
		out.max.fill(-std::numeric_limits<float>::max());
		for (auto vertex : s.shape.polyhedron->vertices) {
			auto world = s.world(vertex);
			for (int axis = 0; axis < 3; ++axis) {
				out.min[axis] = std::min(out.min[axis], world[axis]);
				out.max[axis] = std::max(out.max[axis], world[axis]);
			}
		}
		return out;
	}
	Vec3 extent(s.shape.radius);
	if (s.shape.kind == Shape3D::Kind::Box) {
		for (int i = 0; i < 3; ++i) {
			extent[i] = std::abs(s.axes[0][i]) * s.shape.halfSize.x + std::abs(s.axes[1][i]) * s.shape.halfSize.y
				+ std::abs(s.axes[2][i]) * s.shape.halfSize.z;
		}
	}
	MATH::GEOMETRY::Aabb<3> out;
	for (int i = 0; i < 3; ++i) {
		out.min[i] = s.position[i] - extent[i];
		out.max[i] = s.position[i] + extent[i];
	}
	return out;
}

namespace {
	bool sphereSphere(const ShapeView3D& a, const ShapeView3D& b, Manifold3D& out) {
		auto d = b.position - a.position;
		float length = d.magnitude(), radius = a.shape.radius + b.shape.radius;
		if (length >= radius) {
			return false;
		}
		out.normal = length > 1e-8f ? d / length : Vec3(-1, 0, 0);
		out.count = 1;
		out.points[0] = { (a.position + b.position) * 0.5f, radius - length };
		return true;
	}
	bool boxSphere(const ShapeView3D& box, const ShapeView3D& sphere, Manifold3D& out) {
		auto p = box.local(sphere.position), h = box.shape.halfSize;
		Vec3 closest { std::clamp(p.x, -h.x, h.x), std::clamp(p.y, -h.y, h.y), std::clamp(p.z, -h.z, h.z) };
		auto delta = p - closest;
		float length = delta.magnitude(), depth = sphere.shape.radius - length;
		if (depth < 0) {
			return false;
		}
		Vec3 normal;
		if (length < 1e-6f) {
			Vec3 gap { h.x - std::abs(p.x), h.y - std::abs(p.y), h.z - std::abs(p.z) };
			int axis = gap.y < gap.x ? 1 : 0;
			if (gap.z < gap[axis]) {
				axis = 2;
			}
			normal[axis] = p[axis] >= 0 ? 1.0f : -1.0f;
			depth = sphere.shape.radius + gap[axis];
			closest = p;
			closest[axis] = normal[axis] * h[axis];
		}
		else {
			normal = delta / length;
		}
		out.normal = box.axes[0] * normal.x + box.axes[1] * normal.y + box.axes[2] * normal.z;
		out.count = 1;
		out.points[0] = { box.world(closest), depth };
		return true;
	}
	float projection(const ShapeView3D& s, Vec3 axis) {
		return s.shape.halfSize.x * std::abs(axis.dot(s.axes[0])) + s.shape.halfSize.y * std::abs(axis.dot(s.axes[1]))
			+ s.shape.halfSize.z * std::abs(axis.dot(s.axes[2]));
	}
	Vec3 vertex(const ShapeView3D& s, Vec3 direction) {
		auto p = s.shape.halfSize;
		for (int i = 0; i < 3; ++i) {
			if (s.axes[i].dot(direction) < 0) {
				p[i] = -p[i];
			}
		}
		return s.world(p);
	}
	Vec3 edgeContact(Vec3 p, Vec3 d, float size, Vec3 q, Vec3 e, float otherSize, bool useOne) {
		auto offset = p - q;
		float dd = d.dot(d), ee = e.dot(e), de = d.dot(e), dp = d.dot(offset), ep = e.dot(offset), den = dd * ee - de * de;
		if (std::abs(den) < 0.0001f) {
			return useOne ? p : q;
		}
		float u = (de * ep - ee * dp) / den, v = (dd * ep - de * dp) / den;
		if (std::abs(u) > size || std::abs(v) > otherSize) {
			return useOne ? p : q;
		}
		return (p + d * u + q + e * v) * 0.5f;
	}
	bool boxBox(const ShapeView3D& a, const ShapeView3D& b, Manifold3D& out) {
		auto delta = b.position - a.position;
		float depth = std::numeric_limits<float>::max();
		int best = -1, bestFace = -1;
		Vec3 normal;
		auto test = [&](Vec3 axis, int index) {
			auto length = axis.squareMagnitude();
			if (length < 0.0001f) {
				return true;
			}
			axis *= 1 / std::sqrt(length);
			float penetration = projection(a, axis) + projection(b, axis) - std::abs(delta.dot(axis));
			if (penetration < 0) {
				return false;
			}
			if (penetration < depth) {
				depth = penetration;
				best = index;
				normal = delta.dot(axis) > 0 ? axis : -axis;
			}
			return true;
		};
		for (int i = 0; i < 3; ++i) {
			if (!test(a.axes[i], i)) {
				return false;
			}
		}
		for (int i = 0; i < 3; ++i) {
			if (!test(b.axes[i], i + 3)) {
				return false;
			}
		}
		bestFace = best;
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				if (!test(a.axes[i].cross(b.axes[j]), 6 + i * 3 + j)) {
					return false;
				}
			}
		}
		if (best < 0) {
			return false;
		}
		Vec3 point;
		if (best < 3) {
			point = vertex(b, -normal);
		}
		else if (best < 6) {
			point = vertex(a, normal);
		}
		else {
			int i = (best - 6) / 3, j = (best - 6) % 3;
			auto pa = a.shape.halfSize, pb = b.shape.halfSize;
			for (int k = 0; k < 3; ++k) {
				if (k == i) {
					pa[k] = 0;
				}
				else if (a.axes[k].dot(normal) < 0) {
					pa[k] = -pa[k];
				}
				if (k == j) {
					pb[k] = 0;
				}
				else if (b.axes[k].dot(normal) > 0) {
					pb[k] = -pb[k];
				}
			}
			point = edgeContact(a.world(pa), a.axes[i], a.shape.halfSize[i], b.world(pb), b.axes[j], b.shape.halfSize[j], bestFace > 2);
		}
		out.normal = normal;
		out.count = 1;
		out.points[0] = { point, depth };
		return true;
	}
}

void DefaultNarrowPhase3D::setHandler(Shape3D::Kind a, Shape3D::Kind b, Handler handler) {
	auto ai = static_cast<unsigned>(a), bi = static_cast<unsigned>(b);
	if (ai > 2 || bi > 2) {
		throw std::invalid_argument("Unsupported shape kind");
	}
	mHandlers[ai * 3 + bi] = std::move(handler);
}

Status DefaultNarrowPhase3D::collide(const ShapeView3D& a, const ShapeView3D& b, Manifold3D& out) const {
	out = {};
	auto ai = static_cast<unsigned>(a.shape.kind), bi = static_cast<unsigned>(b.shape.kind);
	if (ai > 2 || bi > 2) {
		return Status::Unsupported;
	}
	if (const auto& handler = mHandlers[ai * 3 + bi]) {
		return handler(a, b, out);
	}
	bool hit;
	if (ai == 2 || bi == 2) {
		hit = collideConvex(a, b, out);
	}
	else if (ai == 0 && bi == 0) {
		hit = sphereSphere(a, b, out);
	}
	else if (ai == 1 && bi == 1) {
		hit = boxBox(a, b, out);
	}
	else if (ai == 1) {
		hit = boxSphere(a, b, out);
	}
	else {
		hit = boxSphere(b, a, out);
		out.normal = -out.normal;
	}
	return hit ? Status::Contact : Status::Separated;
}

}
