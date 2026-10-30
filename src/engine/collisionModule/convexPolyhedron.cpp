#include "convexPolyhedron.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace IKIGAI::COLLISION {
using Vec3 = MATH::Vector3f;
std::shared_ptr<const ConvexPolyhedron> buildConvexPolyhedron(std::span<const Vec3> positions, std::span<const unsigned> indices, float epsilon) {
	if (!(epsilon > 0) || !std::isfinite(epsilon) || positions.size() > 16384 || indices.size() > 24576 || indices.size() < 12
		|| indices.size() % 3) {
		throw std::invalid_argument("Convex mesh: invalid size/epsilon (maximum 8192 triangles)");
	}
	auto hull = std::make_shared<ConvexPolyhedron>();
	std::vector<unsigned> remap {};
	for (auto position : positions) {
		if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
			throw std::invalid_argument("Convex mesh: nonfinite position");
		}
		auto found = std::find_if(
			hull->vertices.begin(), hull->vertices.end(), [&](Vec3 other) { return (other - position).squareMagnitude() <= epsilon * epsilon; });
		remap.push_back(static_cast<unsigned>(found - hull->vertices.begin()));
		if (found == hull->vertices.end()) {
			hull->vertices.push_back(position);
		}
	}
	struct EdgeUse {
		unsigned count = 0;
		int orientation = 0;
		unsigned face = 0;
	};
	std::map<std::pair<unsigned, unsigned>, EdgeUse> uses {};
	std::vector<float> faceAreas {};
	std::vector<float> faceCondition {};
	double first[3] {};
	double second[3][3] {};
	// Integrate relative to an interior point to avoid cancellation far from origin.
	Vec3 reference {};
	for (auto vertex : hull->vertices) {
		reference += vertex / static_cast<float>(hull->vertices.size());
	}
	for (size_t triangle = 0; triangle < indices.size(); triangle += 3) {
		std::array<unsigned, 3> ids {};
		for (int corner = 0; corner < 3; ++corner) {
			if (indices[triangle + corner] >= remap.size()) {
				throw std::invalid_argument("Convex mesh: invalid index");
			}
			ids[corner] = remap[indices[triangle + corner]];
		}
		auto a = hull->vertices[ids[0]];
		auto b = hull->vertices[ids[1]];
		auto c = hull->vertices[ids[2]];
		auto normal = (b - a).cross(c - a);
		float length = normal.magnitude();
		if (!(length > epsilon * epsilon)) {
			throw std::invalid_argument("Convex mesh: degenerate triangle");
		}
		normal *= 1.f / length;
		float offset = normal.dot(a);
		const float condition = std::max({ (b - a).squareMagnitude(), (c - a).squareMagnitude(), (c - b).squareMagnitude() }) / length;
		unsigned faceIndex = 0;
		for (; faceIndex < hull->faces.size(); ++faceIndex) {
			const auto& face = hull->faces[faceIndex];
			const float tolerance = epsilon * 4 * std::max(1.f, faceCondition[faceIndex]);
			if (face.normal.dot(normal) > 1.f - 1e-4f && std::abs(face.normal.dot(a) - face.offset) <= tolerance
				&& std::abs(face.normal.dot(b) - face.offset) <= tolerance && std::abs(face.normal.dot(c) - face.offset) <= tolerance) {
				break;
			}
		}
		if (faceIndex == hull->faces.size()) {
			hull->faces.push_back({ normal, offset, {} });
			faceAreas.push_back(length);
			faceCondition.push_back(condition);
		}
		else if (length > faceAreas[faceIndex]) {
			// A skinny cap triangle is a poor plane estimator. Use the best-area triangle of the coplanar patch.
			hull->faces[faceIndex].normal = normal;
			hull->faces[faceIndex].offset = offset;
			faceAreas[faceIndex] = length;
			faceCondition[faceIndex] = condition;
		}
		for (int edge = 0; edge < 3; ++edge) {
			auto from = ids[edge];
			auto to = ids[(edge + 1) % 3];
			auto& use = uses[std::minmax(from, to)];
			if (use.count && use.face != faceIndex) {
				hull->edges.push_back({ from, to });
			}
			++use.count;
			use.orientation += from < to ? 1 : -1;
			use.face = faceIndex;
			hull->faces[faceIndex].vertices.push_back(from);
		}
		hull->triangles.push_back(ids);
		a -= reference;
		b -= reference;
		c -= reference;
		const double volume = double(a.x) * (double(b.y) * c.z - double(b.z) * c.y) / 6 + double(a.y) * (double(b.z) * c.x - double(b.x) * c.z) / 6
			+ double(a.z) * (double(b.x) * c.y - double(b.y) * c.x) / 6;
		hull->volume += volume;
		for (int row = 0; row < 3; ++row) {
			first[row] += volume * (double(a[row]) + b[row] + c[row]) / 4;
			for (int column = 0; column < 3; ++column) {
				second[row][column] += volume / 20
					* ((double(a[row]) + b[row] + c[row]) * (double(a[column]) + b[column] + c[column]) + double(a[row]) * a[column]
						+ double(b[row]) * b[column] + double(c[row]) * c[column]);
			}
		}
	}
	for (const auto& [edge, use] : uses) {
		if (use.count != 2 || use.orientation != 0) {
			throw std::invalid_argument("Convex mesh: surface must be closed and manifold across seams");
		}
	}
	if (!(hull->volume > double(epsilon) * epsilon * epsilon)) {
		throw std::invalid_argument("Convex mesh: zero or negative volume");
	}
	double covariance[3][3] {};
	for (int row = 0; row < 3; ++row) {
		hull->center[row] = reference[row] + static_cast<float>(first[row] / hull->volume);
		for (int column = 0; column < 3; ++column) {
			covariance[row][column] = second[row][column] / hull->volume - first[row] * first[column] / (hull->volume * hull->volume);
		}
	}
	hull->inertia = MATH::Matrix3f(float(covariance[1][1] + covariance[2][2]), float(-covariance[0][1]), float(-covariance[0][2]),
		float(-covariance[1][0]), float(covariance[0][0] + covariance[2][2]), float(-covariance[1][2]), float(-covariance[2][0]),
		float(-covariance[2][1]), float(covariance[0][0] + covariance[1][1]));
	for (auto& face : hull->faces) {
		for (auto vertex : hull->vertices) {
			if (face.normal.dot(vertex) - face.offset > epsilon * 4) {
				throw std::invalid_argument("Convex mesh: nonconvex or inward-wound surface");
			}
		}
		std::sort(face.vertices.begin(), face.vertices.end());
		face.vertices.erase(std::unique(face.vertices.begin(), face.vertices.end()), face.vertices.end());
		// Only the boundary matters to clipping; interior triangulation vertices are excluded.
		std::vector<unsigned> boundary {};
		for (const auto& edge : hull->edges) {
			for (auto vertex : edge) {
				if (std::abs(face.normal.dot(hull->vertices[vertex]) - face.offset) <= epsilon * 2) {
					boundary.push_back(vertex);
				}
			}
		}
		std::sort(boundary.begin(), boundary.end());
		boundary.erase(std::unique(boundary.begin(), boundary.end()), boundary.end());
		if (boundary.size() < 3) {
			throw std::invalid_argument("Convex mesh: invalid face boundary");
		}
		Vec3 center {};
		for (auto vertex : boundary) {
			center += hull->vertices[vertex] / float(boundary.size());
		}
		auto axis = hull->vertices[boundary[0]] - center;
		axis *= 1.f / axis.magnitude();
		auto tangent = face.normal.cross(axis);
		std::sort(boundary.begin(), boundary.end(), [&](unsigned left, unsigned right) {
			auto l = hull->vertices[left] - center;
			auto r = hull->vertices[right] - center;
			return std::atan2(l.dot(tangent), l.dot(axis)) < std::atan2(r.dot(tangent), r.dot(axis));
		});
		face.vertices = std::move(boundary);
	}
	return hull;
}

Vec3 closestTrianglePoint(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
	auto ab = b - a;
	auto ac = c - a;
	auto ap = p - a;
	float d1 = ab.dot(ap);
	float d2 = ac.dot(ap);
	if (d1 <= 0 && d2 <= 0) {
		return a;
	}
	auto bp = p - b;
	float d3 = ab.dot(bp);
	float d4 = ac.dot(bp);
	if (d3 >= 0 && d4 <= d3) {
		return b;
	}
	float vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0) {
		return a + ab * (d1 / (d1 - d3));
	}
	auto cp = p - c;
	float d5 = ab.dot(cp);
	float d6 = ac.dot(cp);
	if (d6 >= 0 && d5 <= d6) {
		return c;
	}
	float vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0) {
		return a + ac * (d2 / (d2 - d6));
	}
	float va = d3 * d6 - d5 * d4;
	if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
		return b + (c - b) * ((d4 - d3) / (d4 - d3 + d5 - d6));
	}
	float inverse = 1 / (va + vb + vc);
	return a + ab * (vb * inverse) + ac * (vc * inverse);
}
}
