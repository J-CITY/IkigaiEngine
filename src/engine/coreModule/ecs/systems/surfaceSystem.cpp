#include "surfaceSystem.h"
#include "coreModule/ecs/components/chunkModelRenderer.h"
#include "coreModule/ecs/components/foliageComponent.h"
#include "coreModule/ecs/components/materialRenderer.h"
#include "coreModule/ecs/components/surfaceComponent.h"
#include "coreModule/ecs/object.h"
#include "renderModule/backends/interface/meshInterface.h"
#include "renderModule/backends/interface/modelInterface.h"
#include "renderModule/drawable.h"
#include "renderModule/frustum.h"
#include "renderModule/instanceBatch.h"
#include "sceneModule/scene.h"
#include "surfaceModule/surfaceRuntime.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::ECS {
namespace {
	struct DrawBatch {
		RENDER::Drawable Prototype = {};
		std::vector<RENDER::InstanceTransform> Transforms = {};
		float Distance = 0;
	};
	using DrawBatches = std::map<std::tuple<RENDER::MeshInterface*, RENDER::MaterialInterface*, unsigned>, DrawBatch>;
	MATH::Vector3f point(const MATH::Matrix4f& matrix, const MATH::Vector3f& position) {
		const auto value = matrix * MATH::Vector4f { position.x, position.y, position.z, 1 };
		return { value.x, value.y, value.z };
	}
	float scaleBound(const MATH::Matrix4f& matrix) {
		float squared = 0;
		for (const unsigned index : { 0u, 1u, 2u, 4u, 5u, 6u, 8u, 9u, 10u }) {
			squared += matrix.getData()[index] * matrix.getData()[index];
		}
		return std::sqrt(squared);
	}
	bool visible(const MATH::Matrix4f& world, const MATH::Vector3f& center, float radius, const MATH::Vector3f& camera, float viewDistance,
		const RENDER::Frustum* frustum, float& distance) {
		const auto position = point(world, center);
		const float worldRadius = radius * scaleBound(world);
		distance = MATH::Vector3f::Distance(position, camera);
		return distance <= viewDistance + worldRadius && (!frustum || frustum->sphereInFrustum(position.x, position.y, position.z, worldRadius));
	}
	void submit(RENDER::RenderChunk& output, RENDER::Drawable drawable, float distance) {
		if (drawable.material->isBlendable()) {
			if (drawable.material->isDeferred()) {
				output.transparentDrawablesDeferred.emplace(distance, std::move(drawable));
			}
			else {
				output.transparentDrawablesForward.emplace(distance, std::move(drawable));
			}
		}
		else if (drawable.material->isDeferred()) {
			output.opaqueDrawablesDeferred.emplace(distance, std::move(drawable));
		}
		else {
			output.opaqueDrawablesForward.emplace(distance, std::move(drawable));
		}
	}
	void emit(Object& owner, RENDER::RenderChunk& output, const std::shared_ptr<RENDER::ModelInterface>& model,
		const std::vector<std::shared_ptr<RENDER::MaterialInterface>>& materials, const std::shared_ptr<RENDER::MaterialInterface>& overrideMaterial,
		const std::shared_ptr<RENDER::MaterialInterface>& fallback, const MATH::Matrix4f& world, const MATH::Matrix4f& previous,
		const MATH::Vector3f& camera, const RENDER::Frustum* frustum, float viewDistance, DrawBatches* batches = nullptr, unsigned tileId = 0,
		MATH::Vector4f tint = { 1, 1, 1, 1 }) {
		if (!model) {
			return;
		}
		for (const auto& mesh : model->getMeshes()) {
			auto material = overrideMaterial				  ? overrideMaterial
				: mesh->getMaterialIndex() < materials.size() ? materials[mesh->getMaterialIndex()]
															  : nullptr;
			if (!material || (!material->getShader() && !material->isDeferred())) {
				material = fallback;
			}
			if (!material) {
				continue;
			}
			const auto& deformation = material->deformation();
			const float padding = deformation.Mode == RENDER::DeformationMode::None ? 0 : deformation.MaxBend * 2 + deformation.FurLength;
			float distance = 0;
			const auto& bounds = mesh->getBoundingSphere();
			if (!visible(world, bounds.position, bounds.radius + padding, camera, viewDistance, frustum, distance)) {
				continue;
			}
			RENDER::Drawable drawable;
			drawable.prevWorld = previous;
			drawable.world = world;
			drawable.mesh = mesh;
			drawable.material = material;
			drawable.OwnerId = static_cast<int>(owner.getID().getUniqueId());
			drawable.tint = tint;
			if (batches && !material->isBlendable() && material->getGPUInstances() == 1 && deformation.Mode != RENDER::DeformationMode::Fur) {
				auto& batch = (*batches)[{ mesh.get(), material.get(), tileId }];
				if (batch.Transforms.empty()) {
					batch.Prototype = drawable;
					batch.Prototype.world = MATH::Matrix4f::Identity;
					batch.Prototype.prevWorld = MATH::Matrix4f::Identity;
					batch.Distance = distance;
				}
				batch.Distance = std::min(batch.Distance, distance);
				batch.Transforms.push_back({ world, previous, tint });
				continue;
			}
			submit(output, std::move(drawable), distance);
		}
	}
	struct TileCandidate {
		unsigned X = 0;
		unsigned Y = 0;
		unsigned Z = 0;
		float Distance = 0;
	};
	MATH::Vector3f tileCenter(const SURFACE::Settings& settings, unsigned x, unsigned z) {
		const auto [minHeight, maxHeight] = SURFACE::heightBounds(settings);
		return { (float(x) + .5f - settings.TilesX * .5f) * settings.TileSize, (minHeight + maxHeight) * .5f,
			(float(z) + .5f - settings.TilesZ * .5f) * settings.TileSize };
	}
}
void SurfaceSystem::beginFrame(SCENE_SYSTEM::Scene& scene) {
	auto manager = scene.getWorld().getComponentManager();
	for (auto& component : manager->getComponentsArray<SurfaceComponent>()) {
		component.frame.beginFrame(component.settings().BuildBudget);
		component.terrain().beginFrame();
		component.pollUpdate();
	}
	for (auto& foliage : manager->getComponentsArray<FoliageComponent>()) {
		foliage.bind(scene);
	}
	for (auto& surface : manager->getComponentsArray<SurfaceComponent>()) {
		if (!surface.updatePending()) {
			continue;
		}
		bool ready = true;
		for (auto& foliage : manager->getComponentsArray<FoliageComponent>()) {
			if (foliage.surface().get() == &surface && !foliage.preparedFor(surface.requestedRevision())) {
				ready = false;
				if (!foliage.updateError().empty()) {
					surface.failUpdate("Foliage: " + foliage.updateError());
					break;
				}
			}
		}
		surface.publishUpdate(ready);
	}
	for (auto& foliage : manager->getComponentsArray<FoliageComponent>()) {
		foliage.bind(scene);
	}
	for (auto& component : manager->getComponentsArray<ChunkModelRenderer>()) {
		component.frame.beginFrame(component.settings().BuildBudget);
	}
}
void SurfaceSystem::append(SurfaceComponent& component, RENDER::RenderChunk& output, MATH::Vector3f camera, const RENDER::Frustum* frustum,
	std::shared_ptr<RENDER::MaterialInterface> fallback) {
	auto& owner = component.obj.get();
	if (!owner.isAlive() || !owner.getIsActive() || !owner.getIsVisible()) {
		return;
	}
	const auto& settings = component.terrain().settings();
	const auto& transform = owner.getTransform()->getTransform();
	const auto& world = transform.getWorldMatrix();
	std::vector<TileCandidate> candidates;
	const auto [minHeight, maxHeight] = SURFACE::heightBounds(settings);
	const float halfHeight = (maxHeight - minHeight) * .5f;
	const float radius = std::sqrt(settings.TileSize * settings.TileSize * .5f + halfHeight * halfHeight);
	for (unsigned z = 0; z < settings.TilesZ; ++z) {
		for (unsigned x = 0; x < settings.TilesX; ++x) {
			const auto center = tileCenter(settings, x, z);
			float distance = 0;
			if (visible(world, center, radius, camera, settings.ViewDistance, frustum, distance)) {
				candidates.push_back({ x, 0, z, distance });
			}
		}
	}
	std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.Distance < b.Distance; });
	auto& budget = component.frame.Budget;
	const auto& previous = component.frame.previous(world);
	for (const auto& tile : candidates) {
		const unsigned lod = SURFACE::selectLod(tile.Distance, settings.LodDistances);
		auto model = component.terrain().model(tile.X, tile.Z, lod, budget);
		emit(owner, output, model, component.materials(), {}, fallback, world, previous, camera, frustum, settings.ViewDistance);
		auto water = component.terrain().water(tile.X, tile.Z, budget);
		emit(owner, output, water, component.materials(), {}, fallback, world, previous, camera, frustum, settings.ViewDistance);
	}
}
void SurfaceSystem::append(ChunkModelRenderer& component, RENDER::RenderChunk& output, MATH::Vector3f camera, const RENDER::Frustum* frustum,
	std::shared_ptr<RENDER::MaterialInterface> fallback) {
	auto& owner = component.obj.get();
	if (!owner.isAlive() || !owner.getIsActive() || !owner.getIsVisible() || !component.mModel) {
		return;
	}
	const auto& settings = component.settings();
	const auto& transform = owner.getTransform()->getTransform();
	const auto& world = transform.getWorldMatrix();
	const auto origin = SURFACE::voxelOrigin(settings);
	const float size = settings.ChunkSize * settings.BlockSize;
	std::vector<TileCandidate> candidates;
	for (unsigned z = 0; z < settings.ChunksZ; ++z) {
		for (unsigned y = 0; y < settings.ChunksY; ++y) {
			for (unsigned x = 0; x < settings.ChunksX; ++x) {
				const auto center = origin + MATH::Vector3f { x + .5f, y + .5f, z + .5f } * size;
				float distance = 0;
				if (visible(world, center, size * .866026f, camera, settings.ViewDistance, frustum, distance)) {
					candidates.push_back({ x, y, z, distance });
				}
			}
		}
	}
	std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.Distance < b.Distance; });
	std::vector<std::shared_ptr<RENDER::MaterialInterface>> materials;
	if (auto renderer = owner.getComponent<MaterialRenderer>()) {
		materials.assign(renderer->getMaterials().begin(), renderer->getMaterials().end());
	}
	auto& budget = component.frame.Budget;
	const auto& previous = component.frame.previous(world);
	for (const auto& tile : candidates) {
		auto model = component.mModel->model(tile.X, tile.Y, tile.Z, budget);
		emit(owner, output, model, materials, {}, fallback, world, previous, camera, frustum, settings.ViewDistance);
	}
}
void SurfaceSystem::append(SCENE_SYSTEM::Scene& scene, FoliageComponent& component, RENDER::RenderChunk& output, MATH::Vector3f camera,
	const RENDER::Frustum* frustum, std::shared_ptr<RENDER::MaterialInterface> fallback) {
	auto& owner = component.obj.get();
	if (!owner.isAlive() || !owner.getIsActive() || !owner.getIsVisible()) {
		return;
	}
	component.bind(scene);
	auto surface = component.surface();
	if (!surface || !surface->obj->getIsActive() || !surface->obj->getIsVisible()) {
		return;
	}
	// Placements are in the referenced surface's space, including its parent transforms.
	const auto& transform = surface->obj->getTransform()->getTransform();
	const auto& world = transform.getWorldMatrix();
	const auto& previous = surface->frame.previous(world);
	const auto localCamera = point(MATH::Matrix4f::Inverse(world), camera);
	const auto& settings = surface->terrain().settings();
	std::vector<std::optional<unsigned>> terrainLods;
	for (unsigned z = 0; z < settings.TilesZ; ++z) {
		for (unsigned x = 0; x < settings.TilesX; ++x) {
			const float distance = MATH::Vector3f::Distance(point(world, tileCenter(settings, x, z)), camera);
			terrainLods.push_back(surface->terrain().residentLod(x, z, SURFACE::selectLod(distance, settings.LodDistances)));
		}
	}
	for (size_t speciesId = 0; speciesId < component.instances().size(); ++speciesId) {
		DrawBatches batches;
		const auto& species = component.species()[speciesId];
		const auto& runtime = component.instances()[speciesId];
		for (const auto& instance : runtime.Placements) {
			if (!terrainLods[instance.TileId]) {
				continue;
			}
			auto localPosition = instance.Position;
			localPosition.y = instance.LodHeights[*terrainLods[instance.TileId]];
			const auto position = point(world, localPosition);
			const float distance = MATH::Vector3f::Distance(position, camera);
			if (distance > species.ViewDistance) {
				continue;
			}
			size_t lod = 0;
			while (lod + 1 < species.Lods.size() && distance >= species.Lods[lod].Distance) {
				++lod;
			}
			const bool billboard = species.Lods[lod].Billboard;
			const float yaw = billboard ? std::atan2(localCamera.x - localPosition.x, localCamera.z - localPosition.z) : instance.Yaw;
			auto rotation = MATH::Matrix4f::Rotation(yaw, { 0, 1, 0 });
			if (species.AlignToNormal && !billboard) {
				const auto axis = MATH::Vector3f::Cross({ 0, 1, 0 }, instance.Normal);
				if (axis.dot(axis) > .000001f) {
					rotation = MATH::Matrix4f::Rotation(std::acos(std::clamp(instance.Normal.y, -1.0f, 1.0f)), axis) * rotation;
				}
			}
			const auto local
				= MATH::Matrix4f::Translation(localPosition) * rotation * MATH::Matrix4f::Scaling({ instance.Scale, instance.Scale, instance.Scale });
			const float fade = species.FadeDistance > 0 ? std::clamp((species.ViewDistance - distance) / species.FadeDistance, 0.0f, 1.0f) : 1.0f;
			emit(owner, output, runtime.Models[lod], runtime.ModelMaterials[lod], runtime.LodMaterials[lod], fallback, world * local,
				previous * local, camera, frustum, species.ViewDistance, species.Instancing ? &batches : nullptr, instance.TileId,
				{ instance.Tint.x, instance.Tint.y, instance.Tint.z, fade });
		}
		for (auto& [key, batch] : batches) {
			batch.Prototype.instances = std::make_shared<RENDER::InstanceBatch>(std::move(batch.Transforms));
			submit(output, std::move(batch.Prototype), batch.Distance);
		}
	}
}
}
