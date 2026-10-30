#include "convexCollision.h"
#include <algorithm>
#include <limits>

namespace IKIGAI::COLLISION {
namespace {
	std::pair<Vec3, Vec3> closestSegments(Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
		auto u = b - a;
		auto v = d - c;
		auto w = a - c;
		float aa = u.dot(u);
		float bb = u.dot(v);
		float cc = v.dot(v);
		float dd = u.dot(w);
		float ee = v.dot(w);
		float determinant = aa * cc - bb * bb;
		float s = determinant > 1e-12f ? std::clamp((bb * ee - cc * dd) / determinant, 0.f, 1.f) : 0;
		float t = cc > 1e-12f ? std::clamp((bb * s + ee) / cc, 0.f, 1.f) : 0;
		s = aa > 1e-12f ? std::clamp((bb * t - dd) / aa, 0.f, 1.f) : 0;
		t = cc > 1e-12f ? std::clamp((bb * s + ee) / cc, 0.f, 1.f) : 0;
		return { a + u * s, c + v * t };
	}
	std::shared_ptr<const ConvexPolyhedron> boxHull(Vec3 h) {
		std::vector<Vec3> vertices { { -h.x, -h.y, -h.z }, { h.x, -h.y, -h.z }, { h.x, h.y, -h.z }, { -h.x, h.y, -h.z }, { -h.x, -h.y, h.z },
			{ h.x, -h.y, h.z }, { h.x, h.y, h.z }, { -h.x, h.y, h.z } };
		const std::vector<unsigned> indices { 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6,
			5 };
		return buildConvexPolyhedron(vertices, indices, std::max(h.magnitude() * 1e-6f, 1e-7f));
	}
	Vec3 direction(const ShapeView3D& shape, Vec3 local) {
		return shape.axes[0] * local.x + shape.axes[1] * local.y + shape.axes[2] * local.z;
	}
	void reduceContacts(std::vector<ManifoldPoint3D> points, Manifold3D& output) {
		if (points.empty()) {
			return;
		}
		auto deepest = std::max_element(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.penetration < b.penetration; });
		output.points[output.count++] = *deepest;
		points.erase(deepest);
		while (output.count < 4 && !points.empty()) {
			float best = -1;
			size_t selected = 0;
			for (size_t index = 0; index < points.size(); ++index) {
				float distance = std::numeric_limits<float>::max();
				for (int contact = 0; contact < output.count; ++contact) {
					distance = std::min(distance, (points[index].position - output.points[contact].position).squareMagnitude());
				}
				if (distance > best) {
					best = distance;
					selected = index;
				}
			}
			if (best < 1e-12f) {
				break;
			}
			output.points[output.count++] = points[selected];
			points.erase(points.begin() + selected);
		}
	}
}

SegmentSeparation convexSegmentSeparation(const ConvexPolyhedron& hull, Vec3 start, Vec3 end, float radius) {
	float best = std::numeric_limits<float>::max();
	Vec3 segmentPoint {};
	Vec3 surfacePoint {};
	auto consider = [&](Vec3 a, Vec3 b) {
		float distance = (a - b).squareMagnitude();
		if (distance < best) {
			best = distance;
			segmentPoint = a;
			surfacePoint = b;
		}
	};
	bool intersects = false;
	float lower = 0;
	float upper = 1;
	for (const auto& face : hull.faces) {
		float a = face.normal.dot(start) - face.offset;
		float b = face.normal.dot(end) - face.offset;
		if (a > 0 && b > 0) {
			lower = 2;
			break;
		}
		if (a > 0) {
			lower = std::max(lower, a / (a - b));
		}
		if (b > 0) {
			upper = std::min(upper, a / (a - b));
		}
	}
	intersects = lower <= upper;
	if (intersects) {
		SegmentSeparation result { -std::numeric_limits<float>::max(), {}, {} };
		for (const auto& face : hull.faces) {
			float gap = std::min(face.normal.dot(start), face.normal.dot(end)) - face.offset - radius;
			if (gap > result.gap) {
				result.gap = gap;
				result.normal = face.normal;
				auto middle = (start + end) * .5f;
				result.point = middle - face.normal * (face.normal.dot(middle) - face.offset);
			}
		}
		return result;
	}
	for (const auto& triangle : hull.triangles) {
		auto a = hull.vertices[triangle[0]];
		auto b = hull.vertices[triangle[1]];
		auto c = hull.vertices[triangle[2]];
		consider(start, closestTrianglePoint(start, a, b, c));
		consider(end, closestTrianglePoint(end, a, b, c));
		for (const auto& edge : { std::pair { a, b }, std::pair { b, c }, std::pair { c, a } }) {
			auto pair = closestSegments(start, end, edge.first, edge.second);
			consider(pair.first, pair.second);
		}
	}
	float distance = std::sqrt(best);
	return { distance - radius, distance > 1e-8f ? (segmentPoint - surfacePoint) / distance : hull.faces.front().normal, surfacePoint };
}

bool collideConvex(const ShapeView3D& a, const ShapeView3D& b, Manifold3D& output) {
	if (a.shape.kind == Shape3D::Kind::Sphere || b.shape.kind == Shape3D::Kind::Sphere) {
		const bool sphereFirst = a.shape.kind == Shape3D::Kind::Sphere;
		const auto& sphere = sphereFirst ? a : b;
		const auto& poly = sphereFirst ? b : a;
		auto separation
			= convexSegmentSeparation(*poly.shape.polyhedron, poly.local(sphere.position), poly.local(sphere.position), sphere.shape.radius);
		if (separation.gap > 0) {
			return false;
		}
		output.normal = direction(poly, separation.normal) * (sphereFirst ? -1.f : 1.f);
		output.count = 1;
		output.points[0] = { poly.world(separation.point), -separation.gap };
		return true;
	}
	auto aStorage = a.shape.kind == Shape3D::Kind::Box ? boxHull(a.shape.halfSize) : a.shape.polyhedron;
	auto bStorage = b.shape.kind == Shape3D::Kind::Box ? boxHull(b.shape.halfSize) : b.shape.polyhedron;
	const auto& ah = *aStorage;
	const auto& bh = *bStorage;
	float depth = std::numeric_limits<float>::max();
	int reference = 0;
	Vec3 normal {};
	std::array<unsigned, 2> edgeA {};
	std::array<unsigned, 2> edgeB {};
	std::vector<Vec3> testedAxes {};
	auto test = [&](Vec3 axis, int owner, std::array<unsigned, 2> ae = {}, std::array<unsigned, 2> be = {}) {
		float length = axis.magnitude();
		if (length < 1e-6f) {
			return true;
		}
		axis *= 1.f / length;
		if (std::any_of(testedAxes.begin(), testedAxes.end(), [&](Vec3 other) { return std::abs(other.dot(axis)) > 1.f - 1e-6f; })) {
			return true;
		}
		testedAxes.push_back(axis);
		float amin = std::numeric_limits<float>::max();
		float amax = -amin;
		float bmin = amin;
		float bmax = -amin;
		for (auto vertex : ah.vertices) {
			float p = a.world(vertex).dot(axis);
			amin = std::min(amin, p);
			amax = std::max(amax, p);
		}
		for (auto vertex : bh.vertices) {
			float p = b.world(vertex).dot(axis);
			bmin = std::min(bmin, p);
			bmax = std::max(bmax, p);
		}
		float positive = amax - bmin;
		float negative = bmax - amin;
		if (positive < -1e-6f || negative < -1e-6f) {
			return false;
		}
		float overlap = std::max(0.f, std::min(positive, negative));
		// Prefer face manifolds for almost equal axes to avoid unstable edge contacts.
		if (overlap < depth - (owner == 2 ? 1e-5f : 0.f)) {
			depth = overlap;
			normal = positive <= negative ? axis : -axis;
			reference = owner;
			edgeA = ae;
			edgeB = be;
		}
		return true;
	};
	for (const auto& face : ah.faces) {
		if (!test(direction(a, face.normal), 0)) {
			return false;
		}
	}
	for (const auto& face : bh.faces) {
		if (!test(direction(b, face.normal), 1)) {
			return false;
		}
	}
	for (auto ae : ah.edges) {
		for (auto be : bh.edges) {
			if (!test(
					direction(a, ah.vertices[ae[1]] - ah.vertices[ae[0]]).cross(direction(b, bh.vertices[be[1]] - bh.vertices[be[0]])), 2, ae, be)) {
				return false;
			}
		}
	}
	output.normal = normal;
	if (reference == 2) {
		// Select support edges parallel to the winning direction, rather than an arbitrary parallel edge.
		Vec3 da = ah.vertices[edgeA[1]] - ah.vertices[edgeA[0]];
		Vec3 db = bh.vertices[edgeB[1]] - bh.vertices[edgeB[0]];
		float bestA = -std::numeric_limits<float>::max();
		float bestB = std::numeric_limits<float>::max();
		for (auto edge : ah.edges) {
			if ((ah.vertices[edge[1]] - ah.vertices[edge[0]]).cross(da).squareMagnitude() < 1e-8f) {
				float support = normal.dot(a.world((ah.vertices[edge[0]] + ah.vertices[edge[1]]) * .5f));
				if (support > bestA) {
					bestA = support;
					edgeA = edge;
				}
			}
		}
		for (auto edge : bh.edges) {
			if ((bh.vertices[edge[1]] - bh.vertices[edge[0]]).cross(db).squareMagnitude() < 1e-8f) {
				float support = normal.dot(b.world((bh.vertices[edge[0]] + bh.vertices[edge[1]]) * .5f));
				if (support < bestB) {
					bestB = support;
					edgeB = edge;
				}
			}
		}
		auto points = closestSegments(
			a.world(ah.vertices[edgeA[0]]), a.world(ah.vertices[edgeA[1]]), b.world(bh.vertices[edgeB[0]]), b.world(bh.vertices[edgeB[1]]));
		output.points[0] = { (points.first + points.second) * .5f, depth };
		output.count = 1;
		return true;
	}
	const auto& ref = reference == 0 ? a : b;
	const auto& incident = reference == 0 ? b : a;
	const auto& rh = reference == 0 ? ah : bh;
	const auto& ih = reference == 0 ? bh : ah;
	Vec3 outward = reference == 0 ? normal : -normal;
	auto face = std::max_element(rh.faces.begin(), rh.faces.end(),
		[&](const auto& x, const auto& y) { return direction(ref, x.normal).dot(outward) < direction(ref, y.normal).dot(outward); });
	auto other = std::min_element(ih.faces.begin(), ih.faces.end(),
		[&](const auto& x, const auto& y) { return direction(incident, x.normal).dot(outward) < direction(incident, y.normal).dot(outward); });
	std::vector<Vec3> polygon {};
	for (auto vertex : other->vertices) {
		polygon.push_back(incident.world(ih.vertices[vertex]));
	}
	for (size_t edge = 0; edge < face->vertices.size(); ++edge) {
		auto p = ref.world(rh.vertices[face->vertices[edge]]);
		auto q = ref.world(rh.vertices[face->vertices[(edge + 1) % face->vertices.size()]]);
		auto inward = outward.cross(q - p);
		if (inward.squareMagnitude() < 1e-16f) {
			continue;
		}
		inward *= 1.f / inward.magnitude();
		std::vector<Vec3> clipped {};
		for (size_t vertex = 0; vertex < polygon.size(); ++vertex) {
			auto from = polygon[vertex];
			auto to = polygon[(vertex + 1) % polygon.size()];
			float fd = inward.dot(from - p);
			float td = inward.dot(to - p);
			if (fd >= -1e-6f) {
				clipped.push_back(from);
			}
			if ((fd < 0 && td > 0) || (fd > 0 && td < 0)) {
				clipped.push_back(from + (to - from) * (fd / (fd - td)));
			}
		}
		polygon = std::move(clipped);
	}
	float plane = outward.dot(ref.world(rh.vertices[face->vertices.front()]));
	std::vector<ManifoldPoint3D> contacts {};
	for (auto point : polygon) {
		float penetration = plane - outward.dot(point);
		if (penetration >= -1e-5f) {
			contacts.push_back({ point + outward * (std::max(0.f, penetration) * .5f), std::max(0.f, penetration) });
		}
	}
	reduceContacts(std::move(contacts), output);
	if (!output.count) {
		// Degenerate face clipping must not silently turn a proven overlap into separation.
		auto av = *std::max_element(
			ah.vertices.begin(), ah.vertices.end(), [&](Vec3 x, Vec3 y) { return normal.dot(a.world(x)) < normal.dot(a.world(y)); });
		auto bv = *std::min_element(
			bh.vertices.begin(), bh.vertices.end(), [&](Vec3 x, Vec3 y) { return normal.dot(b.world(x)) < normal.dot(b.world(y)); });
		output.count = 1;
		output.points[0] = { (a.world(av) + b.world(bv)) * .5f, depth };
	}
	return true;
}
}
