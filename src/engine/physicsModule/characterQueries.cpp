#include "characterMotor.h"
#include "collisionModule/compoundCollider.h"
#include "collisionModule/convexCollision.h"
#include <limits>

namespace IKIGAI::PHYSICS::CHARACTER {
namespace {
	Separation leafSeparation(const Capsule& c, const COLLISION::ShapeView3D& g) {
		const Vec3 a = g.local(c.feet + Vec3 { 0, c.radius, 0 });
		const Vec3 b = g.local(c.feet + Vec3 { 0, c.height - c.radius, 0 });
		const Vec3 d = b - a;
		if (g.shape.kind == Shape::Kind::Convex) {
			const auto result = COLLISION::convexSegmentSeparation(*g.shape.polyhedron, a, b, c.radius);
			return { result.gap, g.axes[0] * result.normal.x + g.axes[1] * result.normal.y + g.axes[2] * result.normal.z, g.world(result.point) };
		}
		Vec3 p {}, q {};
		if (g.shape.kind == Shape::Kind::Sphere) {
			const float dd = d.dot(d);
			p = a + d * (dd > 1e-12f ? std::clamp(-a.dot(d) / dd, 0.f, 1.f) : 0.f);
			const Vec3 n = unit(p);
			return { length(p) - g.shape.radius - c.radius, g.axes[0] * n.x + g.axes[1] * n.y + g.axes[2] * n.z, g.world(n * g.shape.radius) };
		}
		// Squared segment/AABB distance is piecewise quadratic. Its breakpoints
		// are the six slab crossings; minimise on each interval without sampling.
		const Vec3 h = g.shape.halfSize;
		float cuts[8] = { 0, 1 };
		int count = 2;
		for (int axis = 0; axis < 3; ++axis) {
			if (std::abs(d[axis]) > 1e-9f) {
				for (float side : { -1.f, 1.f }) {
					const float t = (side * h[axis] - a[axis]) / d[axis];
					if (t > 0 && t < 1) {
						cuts[count++] = t;
					}
				}
			}
		}
		std::sort(cuts, cuts + count);
		float best = std::numeric_limits<float>::max();
		auto consider = [&](float t) {
			const auto x = a + d * t, y = clamp(x, h), diff = x - y;
			const float sq = diff.dot(diff);
			if (sq < best) {
				best = sq;
				p = x;
				q = y;
			}
		};
		for (int i = 0; i < count - 1; ++i) {
			const float lo = cuts[i], hi = cuts[i + 1], mid = (lo + hi) * .5f;
			float numerator = 0, denominator = 0;
			for (int axis = 0; axis < 3; ++axis) {
				const float x = a[axis] + d[axis] * mid;
				if (x < -h[axis] || x > h[axis]) {
					const float target = x < 0 ? -h[axis] : h[axis];
					numerator += d[axis] * (a[axis] - target);
					denominator += d[axis] * d[axis];
				}
			}
			consider(lo);
			consider(hi);
			if (denominator > 1e-12f) {
				consider(std::clamp(-numerator / denominator, lo, hi));
			}
		}
		if (best > 1e-12f) {
			// A valid small separation can be below Vector3's generic division epsilon.
			const auto n = (p - q) * (1.0f / std::sqrt(best));
			return { std::sqrt(best) - c.radius, g.axes[0] * n.x + g.axes[1] * n.y + g.axes[2] * n.z, g.world(q) };
		}
		float depth = std::numeric_limits<float>::max();
		Vec3 n {};
		for (int axis = 0; axis < 3; ++axis) {
			for (float side : { -1.f, 1.f }) {
				const float extent = side > 0 ? std::min(a[axis], b[axis]) : -std::max(a[axis], b[axis]);
				const float travel = h[axis] + c.radius - extent;
				if (travel < depth) {
					depth = travel;
					n = { 0, 0, 0 };
					n[axis] = side;
				}
			}
		}
		return { -depth, g.axes[0] * n.x + g.axes[1] * n.y + g.axes[2] * n.z, g.world(q) };
	}

	void sweepLeaf(
		const COLLISION::ShapeView3D& leaf, Id body, Capsule capsule, Vec3 displacement, float travel, float skin, float minNormalY, Hit& closest) {
		// Conservative advancement uses a separating normal from this convex leaf.
		float fraction = 0;
		for (int iteration = 0; iteration < 48 && fraction <= closest.fraction; ++iteration) {
			Capsule moved = capsule;
			moved.feet += displacement * fraction;
			const auto contact = leafSeparation(moved, leaf);
			const float closing = -displacement.dot(contact.normal);
			if (contact.gap <= skin + 1e-5f) {
				const bool nearer = fraction < closest.fraction || closest.body != body;
				if (nearer && contact.normal.y >= minNormalY && (contact.gap < -skin || closing > 1e-7f)) {
					closest = { body, fraction, travel * fraction, contact.point, contact.normal };
				}
				break;
			}
			if (closing <= 1e-8f) {
				break;
			}
			fraction += (contact.gap - skin) / closing;
		}
	}
}

Separation separation(const Capsule& capsule, const Body& body) {
	const auto shape = geometry(body);
	auto nearest = leafSeparation(capsule, COLLISION::compoundLeafView(shape, 0));
	const auto leafCount = COLLISION::compoundLeafCount(shape);
	for (std::size_t child = 1; child < leafCount; ++child) {
		const auto candidate = leafSeparation(capsule, COLLISION::compoundLeafView(shape, child));
		if (candidate.gap < nearest.gap) {
			nearest = candidate;
		}
	}
	return nearest;
}

Hit sweep(const World& world, Capsule capsule, Vec3 displacement, Filter filter, float skin, float minNormalY) {
	Hit closest;
	const float travel = length(displacement);
	for (const auto& [id, body] : world.bodies()) {
		if (!filter.accepts(body)) {
			continue;
		}
		const auto shape = geometry(body);
		const auto leafCount = COLLISION::compoundLeafCount(shape);
		// A leaf behind or beside the motion can be closest initially. Advance
		// against each leaf independently to find the first impact along the path.
		for (std::size_t child = 0; child < leafCount; ++child) {
			sweepLeaf(COLLISION::compoundLeafView(shape, child), id, capsule, displacement, travel, skin, minNormalY, closest);
		}
	}
	return closest;
}

bool overlaps(const World& world, Capsule c, Filter filter, float tolerance) {
	for (const auto& [id, b] : world.bodies()) {
		if (filter.accepts(b) && separation(c, b).gap < -tolerance) {
			return true;
		}
	}
	return false;
}
Hit raycast(const World& world, Vec3 origin, Vec3 delta, Filter filter) {
	return sweep(world, { origin, 0, 0 }, delta, filter, 0);
}
}
