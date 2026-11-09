#include "deformableRenderer.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/components/modelRenderer.h"
#include "coreModule/ecs/components/physics3dComponent.h"
#include "physicsCommon/surface.h"
#include "renderModule/backends/interface/meshInterface.h"
#include "renderModule/backends/interface/modelInterface.h"
#include "renderModule/backends/meshFactory.h"
#include "sceneModule/scene.h"
#include <algorithm>
#include <cstddef>

namespace IKIGAI::RENDER {
namespace {
	using SurfaceVertex = Vertex;
	VertexLayout layout() {
		VertexLayout result;
		result.streams = { { 0, sizeof(SurfaceVertex), VertexInputRate::PerVertex, 1 } };
		result.attributes = { { 0, 0, VertexFormat::Float3, offsetof(SurfaceVertex, position) },
			{ 1, 0, VertexFormat::Float2, offsetof(SurfaceVertex, texCoord) }, { 2, 0, VertexFormat::Float3, offsetof(SurfaceVertex, normal) },
			{ 3, 0, VertexFormat::Float3, offsetof(SurfaceVertex, tangent) }, { 4, 0, VertexFormat::Float3, offsetof(SurfaceVertex, bitangent) },
#if defined(USING_GLES) && defined(IKIGAI_GLES_VERSION) && IKIGAI_GLES_VERSION < 300
			{ 5, 0, VertexFormat::Float4, offsetof(SurfaceVertex, m_BoneIDs) },
#else
			{ 5, 0, VertexFormat::Int4, offsetof(SurfaceVertex, m_BoneIDs) },
#endif
			{ 6, 0, VertexFormat::Float4, offsetof(SurfaceVertex, m_Weights) } };
		return result;
	}
}
void DeformableRenderer::update(SCENE_SYSTEM::Scene& scene) {
	++mFrame;
	for (auto& object : scene.getObjects()) {
		if (!object->isAlive() || !object->getIsActive()) {
			continue;
		}
		auto renderer = object->getComponent<ECS::ModelRenderer>();
		if (!renderer) {
			continue;
		}
		std::shared_ptr<ECS::Physics3DLease> lease;
		if (auto c = object->getComponent<ECS::SoftBody3DComponent>()) {
			lease = c->runtime;
		}
		if (auto c = object->getComponent<ECS::Cloth3DComponent>()) {
			lease = c->runtime;
		}
		auto world = lease ? lease->world.lock() : nullptr;
		if (!world) {
			continue;
		}
		auto* s = world->softBody(lease->id);
		if (!s || s->triangles.empty()) {
			continue;
		}
		auto snapshot = PHYSICS_COMMON::surface<PHYSICS::Particle>(s->particles, s->triangles);
		auto& entry = mEntries[lease.get()];
		bool create = entry.lease.expired() || entry.lease.lock() != lease || entry.vertices != snapshot.positions.size()
			|| entry.indices != snapshot.indices.size();
		if (create) {
			entry.uv.resize(snapshot.positions.size());
		}
		auto rotation = MATH::QuaternionF::ToMatrix3(object->tryGetTransform()->getLocalRotation());
		auto origin = object->tryGetTransform()->getLocalPosition();
		std::vector<SurfaceVertex> vertices(snapshot.positions.size());
		for (std::size_t i = 0; i < vertices.size(); ++i) {
			auto& v = vertices[i];
			v.position = rotation.transformTranspose(snapshot.positions[i] - origin);
			v.normal = rotation.transformTranspose(snapshot.normals[i]);
			auto axis = std::abs(v.normal.y) < 0.9f ? MATH::Vector3f(0, 1, 0) : MATH::Vector3f(1, 0, 0);
			v.tangent = MATH::Vector3f::Normalize(axis.cross(v.normal));
			v.bitangent = v.normal.cross(v.tangent);
			// Planar UVs remain tied to the undeformed surface on creation.
			if (create) {
				entry.uv[i] = { v.position.x, v.position.y };
			}
			v.texCoord = { entry.uv[i][0], entry.uv[i][1] };
		}
		BoundingSphere bounds { rotation.transformTranspose(snapshot.center - origin), snapshot.radius + s->radius };
		if (create) {
			MeshData data;
			data.layout = layout();
			data.vertexCount = vertices.size();
			data.indices = snapshot.indices;
			data.streams = { VertexStreamData::From(0, vertices) };
			data.bounds = bounds;
			entry.mesh = CreateMesh(data);
			entry.model = CreateEmptyModel();
			if (!entry.model) {
				throw std::runtime_error("Deformable surface requires a supported mesh backend");
			}
			entry.model->setMeshes({ entry.mesh });
			entry.model->setMaterialNames({ "Physics surface" });
			entry.vertices = vertices.size();
			entry.indices = snapshot.indices.size();
			entry.lease = lease;
		}
		else {
			entry.mesh->updateVertexStream(0, std::as_bytes(std::span(vertices)), vertices.size());
		}
		entry.mesh->setBoundingSphere(bounds);
		entry.model->computeBoundingSphere();
		entry.frame = mFrame;
		scene.removeFromBVH(renderer);
		if (renderer->getModel() != entry.model) {
			renderer->setModel(entry.model);
		}
		// setModelEvent can insert the model; rebuild once with current bounds.
		scene.removeFromBVH(renderer);
		scene.addToBVH(renderer);
	}
	std::erase_if(mEntries, [&](auto& e) { return e.second.frame != mFrame || e.second.lease.expired(); });
}
}

#endif // USE_PHYSICS_3D
