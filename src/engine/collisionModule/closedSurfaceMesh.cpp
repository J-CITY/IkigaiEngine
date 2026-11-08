#include "closedSurfaceMesh.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace IKIGAI::COLLISION {
namespace {
	using Vec3 = MATH::Vector3f;
	struct EdgeUse {
		unsigned count = 0;
		int orientation = 0;
		unsigned triangle = 0;
		unsigned corner = 0;
	};
	bool finite(Vec3 value) {
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}
	void validateConnectivity(
		const ClosedSurfaceMesh& mesh, const std::vector<std::array<unsigned, 3>>& neighbors, const std::vector<std::vector<unsigned>>& incident) {
		std::vector<bool> visited(mesh.triangles.size(), false);
		std::vector<unsigned> pending { 0 };
		std::size_t count = 0;
		while (!pending.empty()) {
			const auto triangle = pending.back();
			pending.pop_back();
			if (visited[triangle]) {
				continue;
			}
			visited[triangle] = true;
			++count;
			for (auto neighbor : neighbors[triangle]) {
				pending.push_back(neighbor);
			}
		}
		if (count != mesh.triangles.size()) {
			throw std::invalid_argument("Closed surface: separate or nested shells are unsupported");
		}
		// Edge incidence alone permits pinched (bow-tie) vertices. Each used vertex needs one fan.
		std::vector<unsigned> fanMarks(mesh.triangles.size(), 0);
		for (unsigned vertex = 0; vertex < incident.size(); ++vertex) {
			if (incident[vertex].empty()) {
				continue;
			}
			pending.push_back(incident[vertex].front());
			count = 0;
			while (!pending.empty()) {
				const auto triangle = pending.back();
				pending.pop_back();
				if (fanMarks[triangle] == vertex + 1) {
					continue;
				}
				fanMarks[triangle] = vertex + 1;
				++count;
				const auto& ids = mesh.triangles[triangle];
				for (unsigned edge = 0; edge < 3; ++edge) {
					if (ids[edge] == vertex || ids[(edge + 1) % 3] == vertex) {
						pending.push_back(neighbors[triangle][edge]);
					}
				}
			}
			if (count != incident[vertex].size()) {
				throw std::invalid_argument("Closed surface: nonmanifold vertex fan");
			}
		}
	}
}

ClosedSurfaceMesh cookClosedSurfaceMesh(std::span<const Vec3> positions, std::span<const unsigned> indices, float epsilon) {
	if (!(epsilon > 0) || !std::isfinite(epsilon) || positions.size() > 16384 || indices.size() > 24576 || indices.size() < 12
		|| indices.size() % 3) {
		throw std::invalid_argument("Closed surface: invalid size/epsilon (maximum 8192 triangles)");
	}
	ClosedSurfaceMesh mesh {};
	mesh.sourceVertexToVertex.reserve(positions.size());
	for (auto position : positions) {
		if (!finite(position)) {
			throw std::invalid_argument("Closed surface: nonfinite position");
		}
		const auto found = std::find_if(
			mesh.vertices.begin(), mesh.vertices.end(), [&](Vec3 other) { return (other - position).squareMagnitude() <= epsilon * epsilon; });
		mesh.sourceVertexToVertex.push_back(static_cast<unsigned>(found - mesh.vertices.begin()));
		if (found == mesh.vertices.end()) {
			mesh.vertices.push_back(position);
		}
	}
	std::map<std::pair<unsigned, unsigned>, EdgeUse> uses {};
	std::vector<std::array<unsigned, 3>> neighbors(indices.size() / 3);
	std::vector<std::vector<unsigned>> incident(mesh.vertices.size());
	double first[3] {};
	double second[3][3] {};
	// A nearby reference reduces cancellation. Signed integration does not require it to be inside.
	Vec3 reference {};
	std::vector<bool> referenced(mesh.vertices.size(), false);
	std::size_t referencedCount = 0;
	for (auto index : indices) {
		if (index >= mesh.sourceVertexToVertex.size()) {
			throw std::invalid_argument("Closed surface: invalid index");
		}
		const auto welded = mesh.sourceVertexToVertex[index];
		if (!referenced[welded]) {
			referenced[welded] = true;
			++referencedCount;
		}
	}
	for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
		if (referenced[index]) {
			reference += mesh.vertices[index] / static_cast<float>(referencedCount);
		}
	}
	for (std::size_t triangle = 0; triangle < indices.size(); triangle += 3) {
		std::array<unsigned, 3> ids {};
		for (int corner = 0; corner < 3; ++corner) {
			if (indices[triangle + corner] >= mesh.sourceVertexToVertex.size()) {
				throw std::invalid_argument("Closed surface: invalid index");
			}
			ids[corner] = mesh.sourceVertexToVertex[indices[triangle + corner]];
		}
		auto a = mesh.vertices[ids[0]];
		auto b = mesh.vertices[ids[1]];
		auto c = mesh.vertices[ids[2]];
		const auto normal = (b - a).cross(c - a);
		if (!finite(normal) || !(normal.magnitude() > epsilon * epsilon)) {
			throw std::invalid_argument("Closed surface: degenerate triangle");
		}
		const auto triangleIndex = static_cast<unsigned>(mesh.triangles.size());
		for (unsigned edge = 0; edge < 3; ++edge) {
			const auto from = ids[edge];
			const auto to = ids[(edge + 1) % 3];
			auto& use = uses[std::minmax(from, to)];
			if (use.count == 1) {
				neighbors[triangleIndex][edge] = use.triangle;
				neighbors[use.triangle][use.corner] = triangleIndex;
			}
			++use.count;
			use.orientation += from < to ? 1 : -1;
			use.triangle = triangleIndex;
			use.corner = edge;
			incident[from].push_back(triangleIndex);
		}
		mesh.triangles.push_back(ids);
		a -= reference;
		b -= reference;
		c -= reference;
		const double volume = double(a.x) * (double(b.y) * c.z - double(b.z) * c.y) / 6 + double(a.y) * (double(b.z) * c.x - double(b.x) * c.z) / 6
			+ double(a.z) * (double(b.x) * c.y - double(b.y) * c.x) / 6;
		mesh.volume += volume;
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
			throw std::invalid_argument("Closed surface: edges must have two opposite uses across seams");
		}
	}
	validateConnectivity(mesh, neighbors, incident);
	if (!std::isfinite(mesh.volume) || !(mesh.volume > double(epsilon) * epsilon * epsilon)) {
		throw std::invalid_argument("Closed surface: zero or negative volume");
	}
	double covariance[3][3] {};
	for (int row = 0; row < 3; ++row) {
		mesh.center[row] = reference[row] + static_cast<float>(first[row] / mesh.volume);
		for (int column = 0; column < 3; ++column) {
			covariance[row][column] = second[row][column] / mesh.volume - first[row] * first[column] / (mesh.volume * mesh.volume);
		}
	}
	mesh.inertia = MATH::Matrix3f(float(covariance[1][1] + covariance[2][2]), float(-covariance[0][1]), float(-covariance[0][2]),
		float(-covariance[1][0]), float(covariance[0][0] + covariance[2][2]), float(-covariance[1][2]), float(-covariance[2][0]),
		float(-covariance[2][1]), float(covariance[0][0] + covariance[1][1]));
	if (!finite(mesh.center)) {
		throw std::invalid_argument("Closed surface: nonfinite mass properties");
	}
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			if (!std::isfinite(mesh.inertia(row, column))) {
				throw std::invalid_argument("Closed surface: nonfinite inertia");
			}
		}
	}
	return mesh;
}
}
