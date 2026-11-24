#include "scenePicking.h"

#ifdef USE_EDITOR
#include "coreModule/ecs/components/chunkModelRenderer.h"
#include "coreModule/ecs/components/modelRenderer.h"
#include "coreModule/ecs/components/surfaceComponent.h"
#include "coreModule/ecs/object.h"
#include "renderModule/backends/interface/meshInterface.h"
#include "renderModule/backends/interface/modelInterface.h"
#include "surfaceModule/surfacePicking.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace IKIGAI::EDITOR {
namespace {
	using Vec3 = MATH::Vector3f;

	float sphere(const SceneRay& ray, const RENDER::BoundingSphere& bounds) {
		const auto offset = ray.origin - bounds.position;
		const float directionSquared = ray.direction.dot(ray.direction);
		const float projection = offset.dot(ray.direction);
		const float c = offset.dot(offset) - bounds.radius * bounds.radius;
		const float discriminant = projection * projection - directionSquared * c;
		if (discriminant < 0 || directionSquared <= 0) {
			return ray.length;
		}
		const float root = std::sqrt(discriminant);
		const float entry = (-projection - root) / directionSquared;
		const float exit = (-projection + root) / directionSquared;
		return exit >= 0 ? std::max(0.f, entry) : ray.length;
	}

	float triangle(const SceneRay& ray, Vec3 a, Vec3 b, Vec3 c) {
		const auto edge = b - a;
		const auto otherEdge = c - a;
		const auto cross = ray.direction.cross(otherEdge);
		const float determinant = edge.dot(cross);
		if (std::abs(determinant) <= 1e-8f) {
			return ray.length;
		}
		const auto offset = ray.origin - a;
		const auto side = offset.cross(edge);
		const float u = offset.dot(cross) / determinant;
		const float v = ray.direction.dot(side) / determinant;
		const float distance = otherEdge.dot(side) / determinant;
		return u >= 0 && v >= 0 && u + v <= 1 && distance >= 0 ? distance : ray.length;
	}

	float modelHit(const RENDER::ModelInterface& model, const SceneRay& ray) {
		float nearest = ray.length;
		for (const auto& mesh : model.getMeshes()) {
			const float bound = sphere(ray, mesh->getBoundingSphere());
			if (bound >= nearest) {
				continue;
			}
			const auto& vertices = mesh->rayTracingVertices();
			const auto& indices = mesh->rayTracingIndices();
			if (vertices.empty() || indices.empty()) {
				nearest = bound;
				continue;
			}
			for (size_t index = 0; index + 2 < indices.size(); index += 3) {
				const auto a = indices[index];
				const auto b = indices[index + 1];
				const auto c = indices[index + 2];
				if (a < vertices.size() && b < vertices.size() && c < vertices.size()) {
					nearest = std::min(nearest, triangle(ray, vertices[a].position, vertices[b].position, vertices[c].position));
				}
			}
		}
		return nearest;
	}

	float voxelHit(const ECS::ChunkModelRenderer& voxels, const SceneRay& ray) {
		const auto& settings = voxels.settings();
		const auto origin = SURFACE::voxelOrigin(settings);
		const auto start = (ray.origin - origin) * (1.f / settings.BlockSize);
		const auto direction = ray.direction * (1.f / settings.BlockSize);
		const std::array count { int(settings.ChunksX * settings.ChunkSize), int(settings.ChunksY * settings.ChunkSize),
			int(settings.ChunksZ * settings.ChunkSize) };
		float entry = 0;
		float exit = ray.length;
		for (unsigned axis = 0; axis < 3; ++axis) {
			if (std::abs(direction[axis]) < 1e-8f) {
				if (start[axis] < 0 || start[axis] >= count[axis]) {
					return ray.length;
				}
				continue;
			}
			const float nearDistance = -start[axis] / direction[axis];
			const float farDistance = (count[axis] - start[axis]) / direction[axis];
			entry = std::max(entry, std::min(nearDistance, farDistance));
			exit = std::min(exit, std::max(nearDistance, farDistance));
		}
		if (entry >= exit) {
			return ray.length;
		}
		const auto point = start + direction * entry;
		std::array<int, 3> cell {};
		std::array<int, 3> step {};
		std::array<float, 3> crossing {};
		for (unsigned axis = 0; axis < 3; ++axis) {
			const bool forward = direction[axis] >= 0;
			const float first = forward ? std::floor(point[axis]) : std::ceil(point[axis]) - 1;
			cell[axis] = std::clamp(int(first), 0, count[axis] - 1);
			step[axis] = forward ? 1 : -1;
			crossing[axis] = std::abs(direction[axis]) < 1e-8f ? std::numeric_limits<float>::infinity()
															   : (cell[axis] + int(forward) - start[axis]) / direction[axis];
		}
		while (entry < exit) {
			if (voxels.getBlock(cell[0], cell[1], cell[2]) != SURFACE::Block::Air) {
				return entry;
			}
			const unsigned axis = unsigned(std::min_element(crossing.begin(), crossing.end()) - crossing.begin());
			entry = crossing[axis];
			cell[axis] += step[axis];
			if (cell[axis] < 0 || cell[axis] >= count[axis]) {
				break;
			}
			crossing[axis] = (cell[axis] + int(step[axis] > 0) - start[axis]) / direction[axis];
		}
		return ray.length;
	}
}

SceneRay unprojectSceneRay(const MATH::Matrix4f& inverseViewProjection, MATH::Vector2f ndc) {
	const auto start = inverseViewProjection * MATH::Vector4f { ndc.x, ndc.y, -1, 1 };
	const auto end = inverseViewProjection * MATH::Vector4f { ndc.x, ndc.y, 1, 1 };
	const auto origin = Vec3 { start.x, start.y, start.z } * (1.f / start.w);
	const auto delta = Vec3 { end.x, end.y, end.z } * (1.f / end.w) - origin;
	const float length = delta.magnitude();
	return length > 0 && std::isfinite(length) ? SceneRay { origin, delta * (1.f / length), length } : SceneRay {};
}

SceneObjectIntersection intersectSceneObject(ECS::Object& object, const SceneRay& ray) {
	const auto surface = object.getComponent<ECS::SurfaceComponent>();
	const auto voxels = object.getComponent<ECS::ChunkModelRenderer>();
	const auto renderer = object.getComponent<ECS::ModelRenderer>();
	const auto lod = object.getComponent<ECS::ModelLODRenderer>();
	if (!(surface || voxels || renderer || lod) || ray.length <= 0) {
		return { bool(surface || voxels || renderer || lod), ray.length };
	}
	const auto& world = object.tryGetTransform()->getWorldMatrix();
	if (std::abs(MATH::Matrix4f::Determinant(world)) <= 1e-12f) {
		return { false, ray.length };
	}
	const auto inverse = MATH::Matrix4f::Inverse(world);
	const auto localOrigin = inverse * MATH::Vector4f { ray.origin.x, ray.origin.y, ray.origin.z, 1 };
	const auto localDirection = inverse * MATH::Vector4f { ray.direction.x, ray.direction.y, ray.direction.z, 0 };
	const SceneRay local { { localOrigin.x, localOrigin.y, localOrigin.z }, { localDirection.x, localDirection.y, localDirection.z }, ray.length };
	if (surface) {
		const auto hit = SURFACE::raycastSurface(surface->settings(), local.origin, local.direction, local.length);
		return { true, hit ? hit->Parameter : ray.length };
	}
	if (voxels) {
		return { true, voxelHit(*voxels, local) };
	}
	std::shared_ptr<RENDER::ModelInterface> model;
	if (renderer) {
		model = renderer->getModel();
	}
	if (!model && lod) {
		model = lod->getModelByDistance((object.tryGetTransform()->getWorldPosition() - ray.origin).magnitude());
	}
	if (model) {
		return { true, modelHit(*model, local) };
	}
	return { false, ray.length };
}
}
#endif
