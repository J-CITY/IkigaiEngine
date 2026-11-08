#include "concaveDestruction.h"
#include "../../../3rd/assimp/contrib/earcut-hpp/earcut.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace IKIGAI::DESTRUCTION {
namespace {
	using Vec3 = MATH::Vector3f;
	using Point2 = std::array<double, 2>;
	using Edge = std::pair<unsigned, unsigned>;

	struct Plane {
		Vec3 point {};
		Vec3 normal {};
	};
	struct Corner {
		Vertex vertex {};
		unsigned id = 0;
	};
	struct Triangle {
		std::array<Corner, 3> corners {};
		unsigned material = 0;
	};
	struct MeshTopology {
		std::vector<Vec3> positions {};
		std::vector<Triangle> triangles {};
	};
	struct EdgeUse {
		unsigned from = 0;
		unsigned to = 0;
		std::size_t triangle = 0;
		std::size_t neighbor = 0;
		unsigned count = 0;
	};
	using Edges = std::map<Edge, EdgeUse>;

	bool finite(Vec3 value) {
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}
	double dot(Vec3 a, Vec3 b) {
		return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
	}
	Vec3 unit(Vec3 value) {
		const double length = std::sqrt(dot(value, value));
		return length > 1e-20
			? Vec3 { static_cast<float>(value.x / length), static_cast<float>(value.y / length), static_cast<float>(value.z / length) }
			: Vec3 {};
	}
	Plane normalizedPlane(CutPlane plane) {
		if (!finite(plane.point) || !finite(plane.normal) || dot(plane.normal, plane.normal) < 1e-12) {
			throw std::invalid_argument("Cut plane requires a finite point and a nonzero normal");
		}
		return { plane.point, unit(plane.normal) };
	}
	double distance(const Plane& plane, Vec3 position) {
		return double(plane.normal.x) * (double(position.x) - plane.point.x) + double(plane.normal.y) * (double(position.y) - plane.point.y)
			+ double(plane.normal.z) * (double(position.z) - plane.point.z);
	}
	void validateSettings(const ConcaveSettings& settings) {
		const auto& slice = settings.slicing;
		if (!std::isfinite(slice.relativeEpsilon) || slice.relativeEpsilon <= 0 || slice.relativeEpsilon > .001f
			|| !std::isfinite(slice.minimumVolume) || slice.minimumVolume <= 0 || !std::isfinite(slice.interiorUvScale) || slice.interiorUvScale <= 0
			|| slice.maximumTriangles < 4 || slice.maximumTriangles > 8192 || settings.maximumPieces < 2 || settings.maximumPieces > 128
			|| settings.maximumConvexChildren == 0 || settings.maximumConvexChildren > 128
			|| settings.maximumChildTriangles > slice.maximumTriangles * 4) {
			throw std::invalid_argument("Invalid concave geometry settings");
		}
	}
	COLLISION::ClosedSurfaceMesh cookSurface(const FractureMesh& mesh, const SliceSettings& settings, float epsilon) {
		if (mesh.indices.size() / 3 > settings.maximumTriangles || mesh.vertices.size() > 16384) {
			throw std::length_error("Concave geometry triangle or vertex budget exceeded");
		}
		if (mesh.indices.size() % 3 || mesh.materials.size() != mesh.indices.size() / 3) {
			throw std::invalid_argument("Concave geometry requires one material per triangle");
		}
		std::vector<Vec3> positions {};
		positions.reserve(mesh.vertices.size());
		for (const auto& vertex : mesh.vertices) {
			if (!finite(vertex.position) || !finite(vertex.normal) || !finite(vertex.tangent) || !finite(vertex.bitangent)
				|| !std::isfinite(vertex.texCoord.x) || !std::isfinite(vertex.texCoord.y) || vertex.m_Weights.x != 0 || vertex.m_Weights.y != 0
				|| vertex.m_Weights.z != 0 || vertex.m_Weights.w != 0) {
				throw std::invalid_argument("Concave geometry requires finite, unskinned vertices");
			}
			positions.push_back(vertex.position);
		}
		return COLLISION::cookClosedSurfaceMesh(positions, mesh.indices, epsilon);
	}
	MeshTopology topology(const FractureMesh& mesh, const COLLISION::ClosedSurfaceMesh& surface) {
		MeshTopology result { surface.vertices, {} };
		result.triangles.reserve(surface.triangles.size());
		for (std::size_t triangle = 0; triangle < surface.triangles.size(); ++triangle) {
			Triangle output { {}, mesh.materials[triangle] };
			for (unsigned corner = 0; corner < 3; ++corner) {
				const auto id = surface.triangles[triangle][corner];
				auto vertex = mesh.vertices[mesh.indices[triangle * 3 + corner]];
				vertex.position = result.positions[id];
				output.corners[corner] = { vertex, id };
			}
			result.triangles.push_back(output);
		}
		return result;
	}
	Vertex interpolate(const Vertex& a, const Vertex& b, float fraction) {
		Vertex result {};
		result.position = a.position + (b.position - a.position) * fraction;
		result.texCoord = a.texCoord + (b.texCoord - a.texCoord) * fraction;
		result.normal = unit(a.normal + (b.normal - a.normal) * fraction);
		const auto tangent = a.tangent + (b.tangent - a.tangent) * fraction;
		result.tangent = unit(tangent - result.normal * result.normal.dot(tangent));
		result.bitangent = result.normal.cross(result.tangent);
		if (result.bitangent.dot(a.bitangent + (b.bitangent - a.bitangent) * fraction) < 0) {
			result.bitangent = -result.bitangent;
		}
		return result;
	}
	Vertex capVertex(Vec3 position, Vec3 normal, Vec3 tangent, float uvScale) {
		Vertex result {};
		result.position = position;
		result.normal = normal;
		result.tangent = tangent;
		result.bitangent = normal.cross(tangent);
		result.texCoord = { static_cast<float>(dot(position, tangent) * uvScale), static_cast<float>(dot(position, result.bitangent) * uvScale) };
		return result;
	}
	void appendTriangle(std::vector<Triangle>& triangles, Corner a, Corner b, Corner c, unsigned material, float epsilon, unsigned limit) {
		const auto normal = (b.vertex.position - a.vertex.position).cross(c.vertex.position - a.vertex.position);
		if (a.id == b.id || b.id == c.id || c.id == a.id || !(normal.magnitude() > epsilon * epsilon)) {
			throw std::invalid_argument("Cut creates a degenerate triangle");
		}
		if (triangles.size() >= limit) {
			throw std::length_error("Concave geometry triangle budget exceeded");
		}
		triangles.push_back({ { a, b, c }, material });
	}
	Edges edgesOf(const std::vector<Triangle>& triangles) {
		Edges edges {};
		for (std::size_t index = 0; index < triangles.size(); ++index) {
			const auto& corners = triangles[index].corners;
			for (unsigned i = 0; i < 3; ++i) {
				const unsigned from = corners[i].id;
				const unsigned to = corners[(i + 1) % 3].id;
				auto& use = edges[std::minmax(from, to)];
				if (use.count == 0) {
					use = { from, to, index, index, 1 };
				}
				else if (use.count == 1 && use.from == to && use.to == from) {
					use.neighbor = index;
					++use.count;
				}
				else {
					throw std::invalid_argument("Cut output has inconsistent edge incidence");
				}
			}
		}
		return edges;
	}
	double orient(const Point2& a, const Point2& b, const Point2& c) {
		return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
	}
	bool onSegment(const Point2& a, const Point2& b, const Point2& point, double epsilon) {
		const double length = std::hypot(b[0] - a[0], b[1] - a[1]);
		return std::abs(orient(a, b, point)) <= epsilon * length && point[0] >= std::min(a[0], b[0]) - epsilon
			&& point[0] <= std::max(a[0], b[0]) + epsilon && point[1] >= std::min(a[1], b[1]) - epsilon && point[1] <= std::max(a[1], b[1]) + epsilon;
	}
	bool properCrossing(const Point2& a, const Point2& b, const Point2& c, const Point2& d) {
		return orient(a, b, c) * orient(a, b, d) < 0 && orient(c, d, a) * orient(c, d, b) < 0;
	}
	struct SectionLoop {
		std::vector<unsigned> ids {};
		std::vector<Point2> points {};
		double area = 0;
		std::size_t parent = 0;
		unsigned depth = 0;
	};
	bool insideLoop(const Point2& point, const SectionLoop& loop) {
		bool inside = false;
		for (std::size_t i = 0, previous = loop.points.size() - 1; i < loop.points.size(); previous = i++) {
			const auto& a = loop.points[previous];
			const auto& b = loop.points[i];
			if ((a[1] > point[1]) != (b[1] > point[1]) && point[0] < a[0] + (point[1] - a[1]) * (b[0] - a[0]) / (b[1] - a[1])) {
				inside = !inside;
			}
		}
		return inside;
	}
	std::vector<SectionLoop> sectionLoops(
		const std::vector<Triangle>& triangles, const std::vector<Vec3>& positions, const Plane& plane, Vec3 tangent, Vec3 bitangent, float epsilon) {
		const unsigned none = static_cast<unsigned>(positions.size());
		std::vector<unsigned> next(positions.size(), none);
		std::vector<bool> hasPredecessor(positions.size(), false);
		for (const auto& [key, use] : edgesOf(triangles)) {
			if (use.count == 2) {
				continue;
			}
			if (std::abs(distance(plane, positions[use.from])) > 2 * epsilon || std::abs(distance(plane, positions[use.to])) > 2 * epsilon
				|| next[use.to] != none || hasPredecessor[use.from]) {
				throw std::invalid_argument("Cut section has a branched or nonplanar boundary");
			}
			// The cap needs the opposite orientation along every open source edge.
			next[use.to] = use.from;
			hasPredecessor[use.from] = true;
		}
		std::vector<SectionLoop> loops {};
		for (unsigned start = 0; start < next.size(); ++start) {
			if (next[start] == none) {
				continue;
			}
			auto& loop = loops.emplace_back();
			unsigned current = start;
			do {
				if (current == none || next[current] == none) {
					throw std::invalid_argument("Cut section has an open boundary");
				}
				loop.ids.push_back(current);
				const auto local = positions[current] - plane.point;
				loop.points.push_back({ dot(local, tangent), dot(local, bitangent) });
				const auto successor = next[current];
				next[current] = none;
				current = successor;
			} while (current != start);
			for (std::size_t i = 0; i < loop.points.size(); ++i) {
				loop.area += orient(loop.points[0], loop.points[i], loop.points[(i + 1) % loop.points.size()]);
			}
			if (loop.points.size() < 3 || std::abs(loop.area) <= epsilon * epsilon) {
				throw std::invalid_argument("Cut section encloses a degenerate loop");
			}
		}
		return loops;
	}
	void nestLoops(std::vector<SectionLoop>& loops, float epsilon) {
		for (std::size_t i = 0; i < loops.size(); ++i) {
			auto& loop = loops[i];
			for (std::size_t j = 0; j <= i; ++j) {
				const auto& other = loops[j];
				for (std::size_t edge = 0; edge < loop.points.size(); ++edge) {
					const auto& a = loop.points[edge];
					const auto& b = loop.points[(edge + 1) % loop.points.size()];
					const auto first = i == j ? edge + 1 : 0;
					for (std::size_t otherEdge = first; otherEdge < other.points.size(); ++otherEdge) {
						const auto& c = other.points[otherEdge];
						const auto& d = other.points[(otherEdge + 1) % other.points.size()];
						if (i == j && (edge + 1 == otherEdge || (otherEdge + 1) % loop.points.size() == edge)) {
							continue;
						}
						if (properCrossing(a, b, c, d) || onSegment(a, b, c, epsilon) || onSegment(a, b, d, epsilon) || onSegment(c, d, a, epsilon)
							|| onSegment(c, d, b, epsilon)) {
							throw std::invalid_argument("Cut section loops cross or touch");
						}
					}
				}
			}
			loop.parent = loops.size();
			for (std::size_t j = 0; j < loops.size(); ++j) {
				const auto& candidate = loops[j];
				if (std::abs(candidate.area) > std::abs(loop.area) && insideLoop(loop.points.front(), candidate)) {
					++loop.depth;
					if (loop.parent == loops.size() || std::abs(candidate.area) < std::abs(loops[loop.parent].area)) {
						loop.parent = j;
					}
				}
			}
			if ((loop.area > 0) != (loop.depth % 2 == 0)) {
				throw std::invalid_argument("Cut section winding disagrees with loop containment");
			}
		}
	}
	void addRing(const SectionLoop& loop, std::vector<std::vector<Point2>>& rings, std::vector<Point2>& points, std::vector<unsigned>& ids,
		std::vector<unsigned>& earcutRemap, float epsilon) {
		std::vector<unsigned> kept {};
		for (unsigned i = 0; i < loop.points.size(); ++i) {
			kept.push_back(i);
		}
		for (std::size_t i = 0; i < kept.size() && kept.size() > 3;) {
			const auto previous = kept[(i + kept.size() - 1) % kept.size()];
			const auto next = kept[(i + 1) % kept.size()];
			if (onSegment(loop.points[previous], loop.points[next], loop.points[kept[i]], epsilon * .1)) {
				kept.erase(kept.begin() + i);
			}
			else {
				++i;
			}
		}
		auto& ring = rings.emplace_back();
		for (unsigned index : kept) {
			ring.push_back(loop.points[index]);
			earcutRemap.push_back(static_cast<unsigned>(points.size()) + index);
		}
		points.insert(points.end(), loop.points.begin(), loop.points.end());
		ids.insert(ids.end(), loop.ids.begin(), loop.ids.end());
	}
	std::vector<Triangle> capSection(
		const std::vector<Triangle>& triangles, std::vector<Vec3>& positions, const Plane& plane, float epsilon, const SliceSettings& settings) {
		const Vec3 tangent = unit(plane.normal.cross(std::abs(plane.normal.x) < .8f ? Vec3 { 1, 0, 0 } : Vec3 { 0, 1, 0 }));
		const Vec3 bitangent = plane.normal.cross(tangent);
		auto loops = sectionLoops(triangles, positions, plane, tangent, bitangent, epsilon);
		nestLoops(loops, epsilon);
		std::vector<Triangle> result {};
		auto corner = [&](unsigned id) { return Corner { capVertex(positions[id], plane.normal, tangent, settings.interiorUvScale), id }; };
		auto emit = [&](unsigned a, unsigned b, unsigned c) {
			appendTriangle(result, corner(a), corner(b), corner(c), settings.interiorMaterial, epsilon, settings.maximumTriangles);
		};
		for (std::size_t i = 0; i < loops.size(); ++i) {
			if (loops[i].depth % 2) {
				continue;
			}
			std::vector<std::vector<Point2>> rings {};
			std::vector<Point2> points {};
			std::vector<unsigned> ids {};
			std::vector<unsigned> earcutRemap {};
			addRing(loops[i], rings, points, ids, earcutRemap, epsilon);
			for (const auto& hole : loops) {
				if (hole.parent == i) {
					addRing(hole, rings, points, ids, earcutRemap, epsilon);
				}
			}
			auto indices = mapbox::earcut<unsigned>(rings);
			std::vector<bool> used(points.size(), false);
			for (auto& index : indices) {
				index = earcutRemap[index];
				used[index] = true;
			}
			for (std::size_t j = 0; j < indices.size(); j += 3) {
				std::array<unsigned, 3> face { indices[j], indices[j + 1], indices[j + 2] };
				if (orient(points[face[0]], points[face[1]], points[face[2]]) < 0) {
					std::swap(face[1], face[2]);
				}
				std::vector<unsigned> boundary {};
				// Earcut can discard collinear section vertices. Keep every source boundary edge.
				for (unsigned edge = 0; edge < 3; ++edge) {
					const auto a = face[edge];
					const auto b = face[(edge + 1) % 3];
					boundary.push_back(ids[a]);
					std::vector<std::pair<double, unsigned>> omitted {};
					for (unsigned k = 0; k < points.size(); ++k) {
						if (!used[k] && onSegment(points[a], points[b], points[k], epsilon * .5)) {
							const double x = points[k][0] - points[a][0];
							const double y = points[k][1] - points[a][1];
							omitted.emplace_back(x * x + y * y, ids[k]);
						}
					}
					std::sort(omitted.begin(), omitted.end());
					for (const auto& [distanceSquared, id] : omitted) {
						boundary.push_back(id);
					}
				}
				if (boundary.size() == 3) {
					emit(boundary[0], boundary[1], boundary[2]);
				}
				else {
					const auto center = (positions[ids[face[0]]] + positions[ids[face[1]]] + positions[ids[face[2]]]) / 3.f;
					const auto centerId = static_cast<unsigned>(positions.size());
					positions.push_back(center);
					for (std::size_t edge = 0; edge < boundary.size(); ++edge) {
						emit(centerId, boundary[edge], boundary[(edge + 1) % boundary.size()]);
					}
				}
			}
		}
		return result;
	}
	struct HalfSurfaces {
		std::vector<Vec3> positions {};
		std::array<std::vector<Triangle>, 2> exterior {};
		std::array<std::vector<Triangle>, 2> caps {};
		bool split = false;
	};
	HalfSurfaces splitSurface(const MeshTopology& source, const Plane& plane, float epsilon, const SliceSettings& settings, bool bothCaps = true) {
		HalfSurfaces result { source.positions };
		std::vector<double> distances(source.positions.size(), 0);
		bool positive = false;
		bool negative = false;
		for (const auto& triangle : source.triangles) {
			for (const auto& corner : triangle.corners) {
				const double value = distance(plane, source.positions[corner.id]);
				if (std::abs(value) <= epsilon) {
					result.positions[corner.id] = source.positions[corner.id] - plane.normal * static_cast<float>(value);
				}
				else {
					distances[corner.id] = value;
					positive |= value > 0;
					negative |= value < 0;
				}
			}
		}
		if (!(positive && negative)) {
			return result;
		}
		result.split = true;
		std::map<Edge, unsigned> intersections {};
		auto crossing = [&](const Corner& from, const Corner& to) {
			const auto [low, high] = std::minmax(from.id, to.id);
			const auto [found, inserted] = intersections.emplace(Edge { low, high }, static_cast<unsigned>(result.positions.size()));
			if (inserted) {
				const auto fraction = static_cast<float>(distances[low] / (distances[low] - distances[high]));
				auto position = source.positions[low] + (source.positions[high] - source.positions[low]) * fraction;
				position -= plane.normal * static_cast<float>(distance(plane, position));
				result.positions.push_back(position);
			}
			const auto fraction = static_cast<float>(distances[from.id] / (distances[from.id] - distances[to.id]));
			auto vertex = interpolate(from.vertex, to.vertex, fraction);
			vertex.position = result.positions[found->second];
			return Corner { vertex, found->second };
		};
		for (const auto& triangle : source.triangles) {
			auto corners = triangle.corners;
			for (auto& corner : corners) {
				corner.vertex.position = result.positions[corner.id];
			}
			const bool coplanar = std::all_of(corners.begin(), corners.end(), [&](const auto& corner) { return distances[corner.id] == 0; });
			const auto faceNormal
				= (corners[1].vertex.position - corners[0].vertex.position).cross(corners[2].vertex.position - corners[0].vertex.position);
			for (unsigned side = 0; side < 2; ++side) {
				if (coplanar && ((dot(faceNormal, plane.normal) < 0) != (side == 1))) {
					continue;
				}
				const double sign = side == 0 ? -1 : 1;
				std::array<Corner, 4> polygon {};
				unsigned size = 0;
				for (unsigned edge = 0; edge < 3; ++edge) {
					const auto& from = corners[edge];
					const auto& to = corners[(edge + 1) % 3];
					const double start = distances[from.id] * sign;
					const double end = distances[to.id] * sign;
					if (start >= 0) {
						polygon[size++] = from;
					}
					if (start * end < 0) {
						polygon[size++] = crossing(from, to);
					}
				}
				for (unsigned corner = 1; corner + 1 < size; ++corner) {
					appendTriangle(result.exterior[side], polygon[0], polygon[corner], polygon[corner + 1], triangle.material, epsilon,
						settings.maximumTriangles);
				}
			}
		}
		result.caps[0] = capSection(result.exterior[0], result.positions, plane, epsilon, settings);
		if (bothCaps) {
			result.caps[1] = capSection(result.exterior[1], result.positions, { plane.point, -plane.normal }, epsilon, settings);
		}
		return result;
	}
	bool sameAttributes(const Vertex& a, const Vertex& b) {
		return a.texCoord.x == b.texCoord.x && a.texCoord.y == b.texCoord.y && dot(a.normal - b.normal, a.normal - b.normal) == 0
			&& dot(a.tangent - b.tangent, a.tangent - b.tangent) == 0 && dot(a.bitangent - b.bitangent, a.bitangent - b.bitangent) == 0
			&& a.m_BoneIDs == b.m_BoneIDs;
	}
	std::vector<FractureMesh> connectedMeshes(const std::vector<Triangle>& triangles, std::size_t positionCount, unsigned maximumPieces) {
		std::vector<std::vector<std::size_t>> neighbors(triangles.size());
		for (const auto& [key, use] : edgesOf(triangles)) {
			if (use.count != 2) {
				throw std::invalid_argument("Cut output is open");
			}
			neighbors[use.triangle].push_back(use.neighbor);
			neighbors[use.neighbor].push_back(use.triangle);
		}
		std::vector<FractureMesh> result {};
		std::vector<bool> visited(triangles.size(), false);
		std::vector<std::vector<unsigned>> renderVertices(positionCount);
		std::vector<std::size_t> component(positionCount, 0);
		for (std::size_t first = 0; first < triangles.size(); ++first) {
			if (visited[first]) {
				continue;
			}
			if (result.size() >= maximumPieces) {
				throw std::length_error("Concave geometry piece budget exceeded");
			}
			auto& mesh = result.emplace_back();
			std::vector<std::size_t> pending { first };
			visited[first] = true;
			while (!pending.empty()) {
				const auto current = pending.back();
				pending.pop_back();
				for (const auto& corner : triangles[current].corners) {
					auto& candidates = renderVertices[corner.id];
					auto& owner = component[corner.id];
					if (owner && owner != result.size()) {
						throw std::invalid_argument("Cut components touch at a vertex");
					}
					owner = result.size();
					auto existing = std::find_if(
						candidates.begin(), candidates.end(), [&](unsigned index) { return sameAttributes(mesh.vertices[index], corner.vertex); });
					if (existing == candidates.end()) {
						mesh.indices.push_back(static_cast<unsigned>(mesh.vertices.size()));
						candidates.push_back(mesh.indices.back());
						mesh.vertices.push_back(corner.vertex);
					}
					else {
						mesh.indices.push_back(*existing);
					}
				}
				mesh.materials.push_back(triangles[current].material);
				for (auto neighbor : neighbors[current]) {
					if (!visited[neighbor]) {
						visited[neighbor] = true;
						pending.push_back(neighbor);
					}
				}
			}
		}
		return result;
	}
	struct TriangleBounds {
		Vec3 minimum {};
		Vec3 maximum {};
		Plane plane {};
		std::array<unsigned, 3> ids {};
	};
	TriangleBounds bounds(const Triangle& triangle) {
		const auto a = triangle.corners[0].vertex.position;
		const auto b = triangle.corners[1].vertex.position;
		const auto c = triangle.corners[2].vertex.position;
		TriangleBounds result { a, a, { a, unit((b - a).cross(c - a)) }, {} };
		for (unsigned corner = 0; corner < 3; ++corner) {
			result.ids[corner] = triangle.corners[corner].id;
			for (unsigned axis = 0; axis < 3; ++axis) {
				result.minimum[axis] = std::min(result.minimum[axis], triangle.corners[corner].vertex.position[axis]);
				result.maximum[axis] = std::max(result.maximum[axis], triangle.corners[corner].vertex.position[axis]);
			}
		}
		return result;
	}
	unsigned dominantAxis(Vec3 normal) {
		unsigned result = 0;
		for (unsigned axis = 1; axis < 3; ++axis) {
			if (std::abs(normal[axis]) > std::abs(normal[result])) {
				result = axis;
			}
		}
		return result;
	}
	Point2 project(Vec3 point, unsigned axis) {
		return { point[(axis + 1) % 3], point[(axis + 2) % 3] };
	}
	bool inTriangle(const std::array<Point2, 3>& triangle, Point2 point, double epsilon) {
		const double sign = orient(triangle[0], triangle[1], triangle[2]) > 0 ? 1 : -1;
		for (unsigned i = 0; i < 3; ++i) {
			const auto& a = triangle[i];
			const auto& b = triangle[(i + 1) % 3];
			if (orient(a, b, point) * sign < -epsilon * std::hypot(b[0] - a[0], b[1] - a[1])) {
				return false;
			}
		}
		return true;
	}
	bool illegalContacts(const TriangleBounds& a, const TriangleBounds& b, const std::vector<Vec3>& positions, double epsilon) {
		const auto axis = dominantAxis(b.plane.normal);
		std::array<Point2, 3> projected {};
		for (unsigned i = 0; i < 3; ++i) {
			projected[i] = project(positions[b.ids[i]], axis);
		}
		auto shared = [&](unsigned id) { return std::find(b.ids.begin(), b.ids.end(), id) != b.ids.end(); };
		for (unsigned i = 0; i < 3; ++i) {
			const unsigned from = a.ids[i];
			const unsigned to = a.ids[(i + 1) % 3];
			const auto p = positions[from];
			const auto q = positions[to];
			const double start = shared(from) ? 0 : distance(b.plane, p);
			const double end = shared(to) ? 0 : distance(b.plane, q);
			if (std::abs(start) <= epsilon && !shared(from) && inTriangle(projected, project(p, axis), epsilon)) {
				return true;
			}
			if (std::abs(start) <= epsilon && std::abs(end) <= epsilon) {
				for (unsigned edge = 0; edge < 3; ++edge) {
					if (properCrossing(project(p, axis), project(q, axis), projected[edge], projected[(edge + 1) % 3])) {
						return true;
					}
				}
			}
			else if (start * end < 0) {
				const auto hit = p + (q - p) * static_cast<float>(start / (start - end));
				if (inTriangle(projected, project(hit, axis), epsilon)) {
					return true;
				}
			}
		}
		return false;
	}
	void validateNoIntersections(const std::vector<Vec3>& positions, const std::vector<Triangle>& triangles, float epsilon) {
		std::vector<TriangleBounds> boxes {};
		boxes.reserve(triangles.size());
		for (const auto& triangle : triangles) {
			boxes.push_back(bounds(triangle));
		}
		std::stable_sort(boxes.begin(), boxes.end(), [](const auto& a, const auto& b) { return a.minimum.x < b.minimum.x; });
		for (std::size_t i = 0; i < boxes.size(); ++i) {
			const auto& a = boxes[i];
			for (std::size_t j = i + 1; j < boxes.size() && boxes[j].minimum.x <= a.maximum.x + epsilon; ++j) {
				const auto& b = boxes[j];
				if (b.minimum.y > a.maximum.y + epsilon || a.minimum.y > b.maximum.y + epsilon || b.minimum.z > a.maximum.z + epsilon
					|| a.minimum.z > b.maximum.z + epsilon) {
					continue;
				}
				if (illegalContacts(a, b, positions, epsilon) || illegalContacts(b, a, positions, epsilon)) {
					throw std::invalid_argument("Concave geometry contains intersecting or touching triangles");
				}
			}
		}
	}
	struct InsufficientVolume : std::runtime_error {
		using std::runtime_error::runtime_error;
	};
	double pieceVolume(const FractureMesh& mesh, const SliceSettings& settings, float epsilon) {
		const auto surface = cookSurface(mesh, settings, epsilon);
		if (surface.volume < settings.minimumVolume) {
			throw InsufficientVolume("Concave piece volume is below the configured minimum");
		}
		return surface.volume;
	}
	void validateVolume(double expected, double actual, float epsilon) {
		const double characteristicLength = std::cbrt(expected);
		if (std::abs(expected - actual) > std::max(expected * 5e-5, 4 * epsilon * characteristicLength * characteristicLength)) {
			throw std::invalid_argument("Concave operation did not conserve volume");
		}
	}
	std::array<std::vector<FractureMesh>, 2> splitMeshes(
		const MeshTopology& source, const Plane& plane, float epsilon, const ConcaveSettings& settings) {
		auto halves = splitSurface(source, plane, epsilon, settings.slicing);
		std::array<std::vector<FractureMesh>, 2> result {};
		if (!halves.split) {
			return result;
		}
		std::size_t triangleCount = 0;
		for (unsigned side = 0; side < 2; ++side) {
			auto& triangles = halves.exterior[side];
			triangles.insert(triangles.end(), halves.caps[side].begin(), halves.caps[side].end());
			triangleCount += triangles.size();
			if (triangleCount > settings.slicing.maximumTriangles) {
				throw std::length_error("Concave slice output triangle budget exceeded");
			}
			validateNoIntersections(halves.positions, triangles, epsilon * .25f);
			result[side] = connectedMeshes(triangles, halves.positions.size(), settings.maximumPieces - static_cast<unsigned>(result[0].size()));
		}
		return result;
	}
	struct PartitionCandidate {
		Plane plane {};
		double balance = 0;
	};
	std::vector<PartitionCandidate> partitionCandidates(const MeshTopology& mesh, float epsilon) {
		std::vector<PartitionCandidate> candidates {};
		std::vector<bool> used(mesh.positions.size(), false);
		for (const auto& triangle : mesh.triangles) {
			for (const auto& corner : triangle.corners) {
				used[corner.id] = true;
			}
		}
		for (const auto& triangle : mesh.triangles) {
			const auto plane = bounds(triangle).plane;
			double minimum = 0;
			double maximum = 0;
			for (unsigned id = 0; id < used.size(); ++id) {
				if (used[id]) {
					const double value = distance(plane, mesh.positions[id]);
					minimum = std::min(minimum, value);
					maximum = std::max(maximum, value);
				}
			}
			if (minimum >= -4 * epsilon || maximum <= 4 * epsilon) {
				continue;
			}
			const bool duplicate = std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
				return dot(candidate.plane.normal, plane.normal) > 1 - 1e-6 && std::abs(distance(candidate.plane, plane.point)) <= epsilon;
			});
			if (!duplicate) {
				candidates.push_back({ plane, std::min(-minimum, maximum) });
			}
		}
		std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.balance > b.balance; });
		return candidates;
	}
	std::shared_ptr<const COLLISION::ConvexPolyhedron> cookCell(const COLLISION::ClosedSurfaceMesh& surface, float epsilon) {
		std::vector<Vec3> positions {};
		std::vector<unsigned> indices {};
		std::vector<unsigned> remap(surface.vertices.size(), std::numeric_limits<unsigned>::max());
		for (const auto& triangle : surface.triangles) {
			for (unsigned id : triangle) {
				if (remap[id] == std::numeric_limits<unsigned>::max()) {
					remap[id] = static_cast<unsigned>(positions.size());
					positions.push_back(surface.vertices[id]);
				}
				indices.push_back(remap[id]);
			}
		}
		auto cell = COLLISION::buildConvexPolyhedron(positions, indices, epsilon);
		if (cell->vertices.size() > 512 || cell->faces.size() > 64 || cell->edges.size() > 256) {
			throw std::length_error("Concave collision child exceeds convex vertex/plane/edge limits");
		}
		return cell;
	}
	void decompose(const FractureMesh& source, const COLLISION::ClosedSurfaceMesh& surface, float epsilon, const ConcaveSettings& settings,
		std::vector<std::shared_ptr<const COLLISION::ConvexPolyhedron>>& children, unsigned& workRemaining, unsigned& childTriangles,
		unsigned scratchTriangles) {
		if (children.size() >= settings.maximumConvexChildren || workRemaining == 0) {
			throw std::length_error("Concave collision decomposition budget exceeded");
		}
		--workRemaining;
		try {
			auto child = cookCell(surface, epsilon);
			const unsigned limit = settings.maximumChildTriangles ? settings.maximumChildTriangles : settings.slicing.maximumTriangles * 4;
			if (surface.triangles.size() > limit - childTriangles) {
				throw std::length_error("Concave collision aggregate triangle budget exceeded");
			}
			children.push_back(std::move(child));
			childTriangles += static_cast<unsigned>(surface.triangles.size());
			return;
		} catch (const std::invalid_argument&) {
			// The convex cooker estimates supporting planes from coplanar patches. Prefer it
			// to individual face normals, particularly for thin triangles left by seam repair.
		}
		const auto mesh = topology(source, surface);
		const auto candidates = partitionCandidates(mesh, epsilon);
		const auto attempts = std::min<std::size_t>(candidates.size(), 16);
		for (std::size_t candidate = 0; candidate < attempts; ++candidate) {
			if (workRemaining == 0) {
				throw std::length_error("Concave collision decomposition work budget exceeded");
			}
			--workRemaining;
			const auto initialChildren = children.size();
			const auto initialTriangles = childTriangles;
			try {
				const auto pieces = splitMeshes(mesh, candidates[candidate].plane, epsilon, settings);
				if (pieces[0].empty() || pieces[1].empty()) {
					continue;
				}
				auto nextScratch = scratchTriangles;
				for (const auto& side : pieces) {
					for (const auto& piece : side) {
						nextScratch += static_cast<unsigned>(piece.indices.size() / 3);
					}
				}
				if (nextScratch > settings.slicing.maximumTriangles * 4) {
					throw std::length_error("Concave collision partition scratch budget exceeded");
				}
				for (const auto& side : pieces) {
					for (const auto& piece : side) {
						const auto part = cookSurface(piece, settings.slicing, epsilon);
						if (part.volume < settings.slicing.minimumVolume) {
							throw InsufficientVolume("Concave collision child volume is below the configured minimum");
						}
						decompose(piece, part, epsilon, settings, children, workRemaining, childTriangles, nextScratch);
					}
				}
				double volume = 0;
				for (std::size_t i = initialChildren; i < children.size(); ++i) {
					volume += children[i]->volume;
				}
				validateVolume(surface.volume, volume, epsilon);
				return;
			} catch (const std::invalid_argument&) {
				children.resize(initialChildren);
				childTriangles = initialTriangles;
			} catch (const InsufficientVolume&) {
				children.resize(initialChildren);
				childTriangles = initialTriangles;
			}
		}
		throw std::invalid_argument("No supported convex partition was found within the candidate limit");
	}
	std::vector<Plane> validateCutter(std::span<const CutPlane> cutter, float epsilon) {
		if (cutter.size() < 4 || cutter.size() > 12) {
			throw std::invalid_argument("Convex cutter requires 4 to 12 planes");
		}
		std::vector<Plane> planes {};
		for (const auto& plane : cutter) {
			planes.push_back(normalizedPlane(plane));
		}
		std::vector<Vec3> vertices {};
		const Vec3 reference = planes.front().point;
		for (std::size_t i = 0; i < planes.size(); ++i) {
			const auto& a = planes[i];
			for (std::size_t j = i + 1; j < planes.size(); ++j) {
				const auto& b = planes[j];
				const auto axis = a.normal.cross(b.normal);
				if (dot(axis, axis) <= 1e-12) {
					if (dot(a.normal, b.normal) > 0 && std::abs(distance(a, b.point)) <= epsilon) {
						throw std::invalid_argument("Convex cutter has duplicate planes");
					}
					continue;
				}
				// Any nonzero recession cone in three dimensions has a ray parallel to a pair of independent planes,
				// unless all plane normals are parallel (which cannot produce any corner vertices below).
				for (const auto direction : { unit(axis), -unit(axis) }) {
					if (std::all_of(planes.begin(), planes.end(), [&](const auto& plane) { return dot(plane.normal, direction) <= 1e-6; })) {
						throw std::invalid_argument("Convex cutter must be bounded");
					}
				}
				for (std::size_t k = j + 1; k < planes.size(); ++k) {
					const auto& c = planes[k];
					const double determinant = dot(axis, c.normal);
					if (std::abs(determinant) <= 1e-6) {
						continue;
					}
					const auto point = reference
						+ (b.normal.cross(c.normal) * static_cast<float>(dot(a.normal, a.point - reference) / determinant)
							+ c.normal.cross(a.normal) * static_cast<float>(dot(b.normal, b.point - reference) / determinant)
							+ axis * static_cast<float>(dot(c.normal, c.point - reference) / determinant));
					if (finite(point)
						&& std::all_of(planes.begin(), planes.end(), [&](const auto& plane) { return distance(plane, point) <= epsilon; })) {
						vertices.push_back(point);
					}
				}
			}
		}
		if (vertices.size() < 4) {
			throw std::invalid_argument("Convex cutter must enclose a nonempty solid");
		}
		Vec3 center = reference;
		for (auto vertex : vertices) {
			center += (vertex - reference) / static_cast<float>(vertices.size());
		}
		for (const auto& plane : planes) {
			if (distance(plane, center) >= -epsilon) {
				throw std::invalid_argument("Convex cutter is too thin or numerically degenerate");
			}
		}
		return planes;
	}
	using Polygon = std::vector<Vertex>;
	struct PolygonSplit {
		Polygon inside {};
		Polygon outside {};
		bool coplanar = false;
	};
	PolygonSplit splitPolygon(const Polygon& polygon, const Plane& plane, float epsilon) {
		PolygonSplit result {};
		if (polygon.empty()) {
			return result;
		}
		std::vector<double> distances {};
		distances.reserve(polygon.size());
		for (const auto& vertex : polygon) {
			const double value = distance(plane, vertex.position);
			distances.push_back(std::abs(value) <= epsilon ? 0 : value);
		}
		result.coplanar = std::all_of(distances.begin(), distances.end(), [](double value) { return value == 0; });
		for (std::size_t i = 0; i < polygon.size(); ++i) {
			const auto next = (i + 1) % polygon.size();
			auto vertex = polygon[i];
			const double value = distances[i];
			const double end = distances[next];
			if (value == 0) {
				vertex.position -= plane.normal * static_cast<float>(distance(plane, vertex.position));
			}
			if (value <= 0) {
				result.inside.push_back(vertex);
			}
			if (!result.coplanar && value >= 0) {
				result.outside.push_back(vertex);
			}
			if (value * end < 0) {
				auto crossing = interpolate(polygon[i], polygon[next], static_cast<float>(value / (value - end)));
				crossing.position -= plane.normal * static_cast<float>(distance(plane, crossing.position));
				result.inside.push_back(crossing);
				result.outside.push_back(crossing);
			}
		}
		return result;
	}
	bool hasArea(const Polygon& polygon, float epsilon) {
		for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
			if ((polygon[i].position - polygon[0].position).cross(polygon[i + 1].position - polygon[0].position).magnitude() > epsilon * epsilon) {
				return true;
			}
		}
		return false;
	}
	Polygon polygonOf(const Triangle& triangle) {
		return { triangle.corners[0].vertex, triangle.corners[1].vertex, triangle.corners[2].vertex };
	}
	struct BooleanBoundary {
		MeshTopology mesh {};
		float epsilon = 0;
		unsigned limit = 0;

		Corner weld(Vertex vertex) {
			auto found = std::find_if(mesh.positions.begin(), mesh.positions.end(),
				[&](Vec3 position) { return (position - vertex.position).squareMagnitude() <= epsilon * epsilon; });
			const auto id = static_cast<unsigned>(found - mesh.positions.begin());
			if (found != mesh.positions.end()) {
				vertex.position = *found;
			}
			else {
				mesh.positions.push_back(vertex.position);
			}
			return { vertex, id };
		}
		void append(const Polygon& polygon, unsigned material) {
			if (!hasArea(polygon, epsilon)) {
				return;
			}
			const auto first = weld(polygon.front());
			auto previous = weld(polygon[1]);
			for (std::size_t i = 2; i < polygon.size(); ++i) {
				const auto current = weld(polygon[i]);
				const auto a = previous.vertex.position - first.vertex.position;
				const auto b = current.vertex.position - first.vertex.position;
				if (a.cross(b).magnitude() > epsilon * epsilon) {
					appendTriangle(mesh.triangles, first, previous, current, material, epsilon, limit);
				}
				previous = current;
			}
		}
	};
	bool clipExterior(const MeshTopology& source, const std::vector<Plane>& cutter, BooleanBoundary& remaining, BooleanBoundary& removed) {
		bool intersects = false;
		for (const auto& triangle : source.triangles) {
			const auto original = polygonOf(triangle);
			auto inside = original;
			std::vector<Polygon> outside {};
			bool coplanar = false;
			for (const auto& plane : cutter) {
				auto split = splitPolygon(inside, plane, remaining.epsilon);
				outside.push_back(std::move(split.outside));
				inside = std::move(split.inside);
				coplanar |= split.coplanar;
				if (!hasArea(inside, remaining.epsilon)) {
					break;
				}
			}
			if (!hasArea(inside, remaining.epsilon)) {
				remaining.append(original, triangle.material);
				continue;
			}
			if (coplanar) {
				throw std::invalid_argument("Cutter and source have overlapping coplanar faces");
			}
			intersects = true;
			removed.append(inside, triangle.material);
			for (const auto& polygon : outside) {
				remaining.append(polygon, triangle.material);
			}
		}
		return intersects;
	}
	void addCutterFaces(const MeshTopology& source, const std::vector<Plane>& cutter, BooleanBoundary& remaining, BooleanBoundary& removed,
		const SliceSettings& settings) {
		for (std::size_t i = 0; i < cutter.size(); ++i) {
			const auto& plane = cutter[i];
			const auto half = splitSurface(source, plane, remaining.epsilon, settings, false);
			for (const auto& triangle : half.caps[0]) {
				auto polygon = polygonOf(triangle);
				for (std::size_t j = 0; j < cutter.size(); ++j) {
					if (i != j) {
						polygon = splitPolygon(polygon, cutter[j], remaining.epsilon).inside;
					}
				}
				removed.append(polygon, settings.interiorMaterial);
				std::reverse(polygon.begin(), polygon.end());
				for (auto& vertex : polygon) {
					vertex.normal = -vertex.normal;
					vertex.bitangent = -vertex.bitangent;
					vertex.texCoord.y = -vertex.texCoord.y;
				}
				remaining.append(polygon, settings.interiorMaterial);
			}
		}
	}
	// Independently clipped faces may place a vertex along a neighbor's edge. Split that edge on both
	// sides using its exact welded positions, retaining each face's own attributes at the seam.
	void conformBoundary(BooleanBoundary& boundary) {
		auto original = std::exchange(boundary.mesh.triangles, {});
		auto& positions = boundary.mesh.positions;
		const auto positionCount = positions.size();
		for (const auto& triangle : original) {
			std::vector<Corner> ring {};
			for (unsigned edge = 0; edge < 3; ++edge) {
				const auto& from = triangle.corners[edge];
				const auto& to = triangle.corners[(edge + 1) % 3];
				ring.push_back(from);
				const auto direction = to.vertex.position - from.vertex.position;
				const double lengthSquared = dot(direction, direction);
				std::vector<std::pair<double, unsigned>> splits {};
				for (unsigned id = 0; id < positionCount; ++id) {
					if (id == from.id || id == to.id) {
						continue;
					}
					const auto delta = positions[id] - from.vertex.position;
					const double fraction = dot(delta, direction) / lengthSquared;
					if (fraction > 0 && fraction < 1
						&& (delta - direction * static_cast<float>(fraction)).squareMagnitude() <= boundary.epsilon * boundary.epsilon) {
						splits.emplace_back(fraction, id);
					}
				}
				std::sort(splits.begin(), splits.end());
				for (const auto& [fraction, id] : splits) {
					auto vertex = interpolate(from.vertex, to.vertex, static_cast<float>(fraction));
					vertex.position = positions[id];
					ring.push_back({ vertex, id });
				}
			}
			if (ring.size() == 3) {
				appendTriangle(boundary.mesh.triangles, ring[0], ring[1], ring[2], triangle.material, boundary.epsilon, boundary.limit);
				continue;
			}
			const auto& [a, b, c] = triangle.corners;
			auto vertex = interpolate(interpolate(a.vertex, b.vertex, .5f), c.vertex, 1.f / 3);
			const Corner center { vertex, static_cast<unsigned>(positions.size()) };
			positions.push_back(vertex.position);
			for (std::size_t edge = 0; edge < ring.size(); ++edge) {
				appendTriangle(
					boundary.mesh.triangles, center, ring[edge], ring[(edge + 1) % ring.size()], triangle.material, boundary.epsilon, boundary.limit);
			}
		}
	}
}

ConcaveGeometry cookConcaveMesh(const FractureMesh& mesh, const ConcaveSettings& settings) {
	validateSettings(settings);
	const auto epsilon = meshTolerance(mesh, settings.slicing.relativeEpsilon);
	ConcaveGeometry result { cookSurface(mesh, settings.slicing, epsilon) };
	if (result.surface.volume < settings.slicing.minimumVolume) {
		throw std::invalid_argument("Concave geometry volume is below the configured minimum");
	}
	{
		const auto source = topology(mesh, result.surface);
		validateNoIntersections(source.positions, source.triangles, epsilon * .25f);
	}
	unsigned workRemaining = settings.maximumConvexChildren * 8;
	unsigned childTriangles = 0;
	decompose(mesh, result.surface, epsilon, settings, result.children, workRemaining, childTriangles,
		static_cast<unsigned>(result.surface.triangles.size()));
	return result;
}

ConcaveResult sliceConcaveMesh(const FractureMesh& mesh, Vec3 point, Vec3 normal, const ConcaveSettings& settings) {
	try {
		validateSettings(settings);
		const auto plane = normalizedPlane({ point, normal });
		const auto epsilon = meshTolerance(mesh, settings.slicing.relativeEpsilon);
		const auto surface = cookSurface(mesh, settings.slicing, epsilon);
		const auto source = topology(mesh, surface);
		validateNoIntersections(source.positions, source.triangles, epsilon * .25f);
		auto sides = splitMeshes(source, plane, epsilon, settings);
		if (sides[0].empty() || sides[1].empty()) {
			return {};
		}
		double volume = 0;
		ConcaveResult result { SliceStatus::Split };
		for (unsigned side = 0; side < 2; ++side) {
			for (auto& piece : sides[side]) {
				volume += pieceVolume(piece, settings.slicing, epsilon);
				result.positiveSide.push_back(side == 1);
				result.remaining.push_back(std::move(piece));
			}
		}
		validateVolume(surface.volume, volume, epsilon);
		return result;
	} catch (const std::length_error& error) {
		return { SliceStatus::BudgetExceeded, {}, {}, {}, error.what() };
	} catch (const InsufficientVolume& error) {
		return { SliceStatus::TooSmall, {}, {}, {}, error.what() };
	} catch (const std::invalid_argument& error) {
		return { SliceStatus::UnsupportedMesh, {}, {}, {}, error.what() };
	}
}

ConcaveResult subtractConvexCutter(const FractureMesh& mesh, std::span<const CutPlane> cutter, const ConcaveSettings& settings) {
	try {
		validateSettings(settings);
		const auto epsilon = meshTolerance(mesh, settings.slicing.relativeEpsilon);
		const auto planes = validateCutter(cutter, epsilon);
		const auto surface = cookSurface(mesh, settings.slicing, epsilon);
		const auto source = topology(mesh, surface);
		validateNoIntersections(source.positions, source.triangles, epsilon * .25f);
		BooleanBoundary remaining { {}, epsilon, settings.slicing.maximumTriangles };
		BooleanBoundary removed { {}, epsilon, settings.slicing.maximumTriangles };
		if (!clipExterior(source, planes, remaining, removed)) {
			// A strictly internal cutter would create a nested shell. This representation rejects
			// it explicitly. A cutter wholly outside the solid produces no clipped cutter faces.
			addCutterFaces(source, planes, remaining, removed, settings.slicing);
			if (!removed.mesh.triangles.empty()) {
				throw std::invalid_argument("An enclosed cutter cavity requires nested shell support");
			}
			return {};
		}
		addCutterFaces(source, planes, remaining, removed, settings.slicing);
		conformBoundary(remaining);
		conformBoundary(removed);
		if (removed.mesh.triangles.empty()) {
			throw InsufficientVolume("Convex cutter intersection collapsed within tolerance");
		}
		if (remaining.mesh.triangles.size() + removed.mesh.triangles.size() > settings.slicing.maximumTriangles) {
			throw std::length_error("Convex cutter output triangle budget exceeded");
		}
		ConcaveResult result { SliceStatus::Split };
		double volume = 0;
		for (auto [boundary, output] : { std::pair { &remaining, &result.remaining }, std::pair { &removed, &result.removed } }) {
			validateNoIntersections(boundary->mesh.positions, boundary->mesh.triangles, epsilon * .25f);
			*output = connectedMeshes(
				boundary->mesh.triangles, boundary->mesh.positions.size(), settings.maximumPieces - static_cast<unsigned>(result.remaining.size()));
			for (const auto& piece : *output) {
				volume += pieceVolume(piece, settings.slicing, epsilon);
			}
		}
		validateVolume(surface.volume, volume, epsilon);
		return result;
	} catch (const std::length_error& error) {
		return { SliceStatus::BudgetExceeded, {}, {}, {}, error.what() };
	} catch (const InsufficientVolume& error) {
		return { SliceStatus::TooSmall, {}, {}, {}, error.what() };
	} catch (const std::invalid_argument& error) {
		return { SliceStatus::UnsupportedMesh, {}, {}, {}, error.what() };
	}
}
}
