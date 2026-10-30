#include "planeSlicer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace IKIGAI::DESTRUCTION {
using Vec3 = MATH::Vector3f;
namespace {
	bool finite(Vec3 value) {
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}
	Vec3 unit(Vec3 value) {
		float length = value.magnitude();
		return length > 1e-12f ? value * (1.f / length) : Vec3 {};
	}
	Vertex interpolate(const Vertex& a, const Vertex& b, float fraction) {
		Vertex vertex {};
		vertex.position = a.position + (b.position - a.position) * fraction;
		vertex.texCoord = a.texCoord + (b.texCoord - a.texCoord) * fraction;
		vertex.normal = unit(a.normal + (b.normal - a.normal) * fraction);
		auto tangent = a.tangent + (b.tangent - a.tangent) * fraction;
		vertex.tangent = unit(tangent - vertex.normal * vertex.normal.dot(tangent));
		vertex.bitangent = vertex.normal.cross(vertex.tangent);
		if (vertex.bitangent.dot(a.bitangent + (b.bitangent - a.bitangent) * fraction) < 0) {
			vertex.bitangent = -vertex.bitangent;
		}
		return vertex;
	}
	void triangle(FractureMesh& mesh, Vertex a, Vertex b, Vertex c, unsigned material, float epsilon) {
		if ((b.position - a.position).cross(c.position - a.position).squareMagnitude() <= epsilon * epsilon * epsilon * epsilon) {
			return;
		}
		unsigned base = static_cast<unsigned>(mesh.vertices.size());
		mesh.vertices.insert(mesh.vertices.end(), { a, b, c });
		mesh.indices.insert(mesh.indices.end(), { base, base + 1, base + 2 });
		mesh.materials.push_back(material);
	}
}
float meshTolerance(const FractureMesh& mesh, float relativeEpsilon) {
	Vec3 minimum(std::numeric_limits<float>::max());
	Vec3 maximum(-std::numeric_limits<float>::max());
	for (const auto& vertex : mesh.vertices) {
		for (int axis = 0; axis < 3; ++axis) {
			minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
			maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
		}
	}
	return std::max((maximum - minimum).magnitude() * relativeEpsilon, 1e-7f);
}
std::shared_ptr<const COLLISION::ConvexPolyhedron> validateMesh(const FractureMesh& mesh, const SliceSettings& settings) {
	if (!std::isfinite(settings.relativeEpsilon) || settings.relativeEpsilon <= 0 || settings.relativeEpsilon > .001f
		|| !std::isfinite(settings.minimumVolume) || settings.minimumVolume <= 0 || !std::isfinite(settings.interiorUvScale)
		|| settings.interiorUvScale <= 0 || settings.maximumTriangles < 4 || settings.maximumTriangles > 8192
		|| mesh.indices.size() / 3 > settings.maximumTriangles || mesh.materials.size() * 3 != mesh.indices.size()) {
		throw std::invalid_argument("Invalid fracture mesh/settings or triangle budget");
	}
	std::vector<Vec3> positions {};
	for (const auto& vertex : mesh.vertices) {
		if (!finite(vertex.position) || !finite(vertex.normal) || !finite(vertex.tangent) || !finite(vertex.bitangent)
			|| !std::isfinite(vertex.texCoord.x) || !std::isfinite(vertex.texCoord.y) || vertex.m_Weights.x != 0 || vertex.m_Weights.y != 0
			|| vertex.m_Weights.z != 0 || vertex.m_Weights.w != 0) {
			throw std::invalid_argument("Fracture mesh must have finite, unskinned vertices");
		}
		positions.push_back(vertex.position);
	}
	auto hull = COLLISION::buildConvexPolyhedron(positions, mesh.indices, meshTolerance(mesh, settings.relativeEpsilon));
	if (hull->vertices.size() > 512 || hull->faces.size() > 64 || hull->edges.size() > 256) {
		throw std::invalid_argument("Fracture collision complexity exceeds 512 vertices / 64 planes / 256 edges");
	}
	return hull;
}
SliceResult sliceMesh(const FractureMesh& mesh, Vec3 point, Vec3 normal, const SliceSettings& settings) {
	SliceResult result {};
	try {
		if (!finite(point) || !finite(normal) || normal.squareMagnitude() < 1e-12f) {
			throw std::invalid_argument("Slice plane must have a finite point and nonzero normal");
		}
		if (mesh.indices.size() / 3 > settings.maximumTriangles) {
			result.status = SliceStatus::BudgetExceeded;
			return result;
		}
		auto original = validateMesh(mesh, settings);
		normal = unit(normal);
		float epsilon = meshTolerance(mesh, settings.relativeEpsilon);
		float minimum = 0;
		float maximum = 0;
		for (auto position : original->vertices) {
			float distance = normal.dot(position - point);
			minimum = std::min(minimum, distance);
			maximum = std::max(maximum, distance);
		}
		if (minimum >= -epsilon || maximum <= epsilon) {
			return result;
		}
		std::vector<Vec3> section {};
		auto addSection = [&](Vec3 position) {
			if (std::none_of(section.begin(), section.end(), [&](Vec3 other) { return (position - other).squareMagnitude() < epsilon * epsilon; })) {
				section.push_back(position);
			}
		};
		for (size_t index = 0; index < mesh.indices.size(); index += 3) {
			std::array<Vertex, 3> source {};
			std::array<float, 3> distances {};
			for (int corner = 0; corner < 3; ++corner) {
				source[corner] = mesh.vertices[mesh.indices[index + corner]];
				auto& distance = distances[corner];
				distance = normal.dot(source[corner].position - point);
				if (std::abs(distance) <= epsilon) {
					source[corner].position -= normal * distance;
					distance = 0;
					addSection(source[corner].position);
				}
			}
			for (int side : { -1, 1 }) {
				std::vector<Vertex> polygon {};
				for (int edge = 0; edge < 3; ++edge) {
					int next = (edge + 1) % 3;
					float from = distances[edge] * side;
					float to = distances[next] * side;
					if (from >= 0) {
						polygon.push_back(source[edge]);
					}
					if ((from < 0 && to > 0) || (from > 0 && to < 0)) {
						auto intersection = interpolate(source[edge], source[next], from / (from - to));
						intersection.position -= normal * normal.dot(intersection.position - point);
						polygon.push_back(intersection);
						addSection(intersection.position);
					}
				}
				auto& output = side < 0 ? result.negative : result.positive;
				for (size_t corner = 1; corner + 1 < polygon.size(); ++corner) {
					triangle(output, polygon[0], polygon[corner], polygon[corner + 1], mesh.materials[index / 3], epsilon);
				}
			}
		}
		if (section.size() < 3) {
			return result;
		}
		Vec3 center {};
		for (auto position : section) {
			center += position / float(section.size());
		}
		auto axis = unit(normal.cross(std::abs(normal.x) < .8f ? Vec3 { 1, 0, 0 } : Vec3 { 0, 1, 0 }));
		auto tangent = normal.cross(axis);
		std::sort(section.begin(), section.end(), [&](Vec3 left, Vec3 right) {
			return std::atan2((left - center).dot(tangent), (left - center).dot(axis))
				< std::atan2((right - center).dot(tangent), (right - center).dot(axis));
		});
		// Keep collinear boundary vertices: they match split source edges and avoid T junctions.
		for (int side : { -1, 1 }) {
			auto vertex = [&](Vec3 position) {
				Vertex value {};
				value.position = position;
				value.normal = normal * float(-side);
				value.tangent = axis;
				value.bitangent = value.normal.cross(axis);
				value.texCoord = { position.dot(axis) * settings.interiorUvScale, position.dot(value.bitangent) * settings.interiorUvScale };
				return value;
			};
			auto& output = side < 0 ? result.negative : result.positive;
			for (size_t index = 0; index < section.size(); ++index) {
				auto a = vertex(section[index]);
				auto b = vertex(section[(index + 1) % section.size()]);
				if (side > 0) {
					std::swap(a, b);
				}
				triangle(output, vertex(center), a, b, settings.interiorMaterial, epsilon);
			}
		}
		if (result.negative.materials.size() > settings.maximumTriangles || result.positive.materials.size() > settings.maximumTriangles) {
			result.status = SliceStatus::BudgetExceeded;
			return result;
		}
		auto negative = validateMesh(result.negative, settings);
		auto positive = validateMesh(result.positive, settings);
		if (negative->volume < settings.minimumVolume || positive->volume < settings.minimumVolume) {
			result.status = SliceStatus::TooSmall;
			return result;
		}
		if (std::abs(negative->volume + positive->volume - original->volume) > original->volume * 1e-3) {
			throw std::runtime_error("Slice failed volume conservation");
		}
		result.status = SliceStatus::Split;
	} catch (const std::exception& error) {
		result.status = SliceStatus::UnsupportedMesh;
		result.error = error.what();
	}
	return result;
}
}
