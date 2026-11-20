#include "sceneRenderData.h"

#include "coreModule/ecs/componentTypes.h"
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/components/renderTargetComponent.h"
#include "coreModule/ecs/components/skeletalAnimationComponent.h"
#include "coreModule/ecs/components/skeletalComponent.h"
#include "coreModule/ecs/systems/surfaceSystem.h"
#include "profilerModule/profiler.h"
#include "profilerModule/profilerSpy.h"
#include "renderModule/backends/interface/meshInterface.h"
#include "renderModule/backends/interface/renderEnums.h"
#include "renderModule/gpuScene.h"
#include "renderModule/spatial/frustumBvh.h"
#include "renderModule/spatial/frustumVisibility.h"
#include "sceneModule/scene.h"
#include "taskModule/taskSystem.h"
#include "traceModule/trace.h"
#include "utilsModule/memory/frameArena.h"
#include "utilsModule/serviceManager.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/components/ragdollComponent.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>

using namespace IKIGAI;
using namespace IKIGAI::RENDER;
using namespace IKIGAI::SCENE_SYSTEM;

namespace {
struct BVHData {
	std::weak_ptr<ECS::Object> object;
	std::weak_ptr<RENDER::MeshInterface> mesh;
};

// TODO: move to utils
RENDER::BoundingSphere getGlobalBoundingSphere(const RENDER::BoundingSphere& boundingSphere, const ECS::Transform& transform) {
	const auto& position = transform.getWorldPosition();
	const auto& rotation = transform.getWorldRotation();
	const auto& scale = transform.getWorldScale();

	float maxScale = std::max(std::max(std::max(scale.x, scale.y), scale.z), 0.0f);
	float scaledRadius = boundingSphere.radius * maxScale;
	auto sphereOffset = MATH::QuaternionF::RotatePoint(boundingSphere.position, rotation) * maxScale;

	MATH::Vector3f worldCenter = position + sphereOffset;

	return RENDER::BoundingSphere { worldCenter, scaledRadius };
}

static float DeformationPadding(const ECS::MaterialRenderer& renderer) {
	float padding = 0;
	for (const auto& material : renderer.getMaterials()) {
		if (material && material->deformation().Mode != RENDER::DeformationMode::None) {
			padding = std::max(padding, material->deformation().MaxBend * 2 + material->deformation().FurLength);
		}
	}
	return padding;
}

}

struct SceneRenderData::SpatialIndex {
	FrustumBvh<std::shared_ptr<BVHData>> meshesTree;
	std::unordered_map<ECS::Object::Id, std::vector<std::shared_ptr<BVHData>>> objectToMeshes;
	std::unordered_map<ECS::Object::Id, std::weak_ptr<ECS::Object>> pendingUpdates;
	std::weak_ptr<Scene::ComponentChanges> changes;
	EVENT::EventListener listener {};

	~SpatialIndex() {
		if (auto source = changes.lock()) {
			source->removeListener(listener);
		}
	}

	void remove(ECS::Object& object) {
		if (auto it = objectToMeshes.find(object.getID()); it != objectToMeshes.end()) {
			for (const auto& entry : it->second) {
				meshesTree.remove(entry);
			}
			objectToMeshes.erase(it);
		}
	}

	void refresh(ECS::Object& object) {
		remove(object);
		const auto renderer = object.getComponent<ECS::ModelRenderer>();
		const auto transform = object.tryGetTransform();
		const auto model = renderer ? renderer->getModel() : nullptr;
		if (!object.isAlive() || !model || !transform) {
			return;
		}

		const auto& modelBounds = renderer->getFrustumBehaviour() == ECS::EFrustumBehaviour::CULL_CUSTOM ? renderer->getCustomBoundingSphere()
																										 : model->getBoundingSphere();
		auto bounds = getGlobalBoundingSphere(modelBounds, transform->getTransform());
		if (auto materials = object.getComponent<ECS::MaterialRenderer>()) {
			const auto scale = transform->getWorldScale();
			bounds.radius += DeformationPadding(*materials) * std::max({ 1.0f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
		}
#if USE_PHYSICS_3D
		if (auto ragdoll = object.getComponent<ECS::RagdollComponent>(); ragdoll && ragdoll->state() == ECS::RagdollState::Simulated) {
			auto skeletal = object.getComponent<ECS::SkeletalComponent>();
			auto* pose = skeletal ? dynamic_cast<SKELETON::RagdollInstance*>(skeletal->finalPlayable(nullptr)) : nullptr;
			if (pose) {
				bounds.radius += pose->boundsRadius() + (bounds.position - transform->getWorldPosition()).magnitude();
				bounds.position = transform->getWorldPosition();
			}
		}
#endif
		for (const auto& mesh : model->getMeshes()) {
			auto entry = std::make_shared<BVHData>(BVHData { object.weak_from_this(), mesh });
			meshesTree.upsert(entry, bounds);
			objectToMeshes[object.getID()].push_back(std::move(entry));
		}
	}

	void onChange(Scene::ComponentChange change, UTILS::WeakPtr<ECS::ComponentBase> component) {
		if (!component) {
			return;
		}

		const auto& type = component->getName();
		auto& object = component->obj.get();
		if (type == "ModelRenderer" || type == "TransformComponent") {
			if (change == Scene::ComponentChange::Removed) {
				pendingUpdates.erase(object.getID());
				remove(object);
			}
			else {
				pendingUpdates.insert_or_assign(object.getID(), object.weak_from_this());
			}
		}
	}

	void refreshPending() {
		for (const auto& [id, pending] : pendingUpdates) {
			if (auto object = pending.lock()) {
				refresh(*object);
			}
		}
		pendingUpdates.clear();
	}
};

SceneRenderData::SceneRenderData() = default;
SceneRenderData::~SceneRenderData() = default;

SceneRenderData::SpatialIndex& SceneRenderData::spatial(Scene& scene) {
	auto changes = scene.componentChanges().lock();
	if (!mSpatial || mSpatial->changes.lock() != changes) {
		auto spatial = std::make_unique<SpatialIndex>();
		spatial->changes = changes;
		spatial->listener = changes->add([index = spatial.get()](Scene::ComponentChange change, UTILS::WeakPtr<ECS::ComponentBase> component) {
			index->onChange(change, component);
		});
		// Include transient models registered directly in the scene's world.
		for (auto& renderer : scene.getWorld().getComponentManager()->getComponentsArray<ECS::ModelRenderer>()) {
			spatial->refresh(renderer.obj.get());
		}
		mSpatial = std::move(spatial);
	}
	return *mSpatial;
}

std::vector<IKIGAI::RENDER::LightOGL> SceneRenderData::findLightData(const Scene& scene, std::vector<int>* objectIds) {
	std::vector<RENDER::LightOGL> result;
	if (objectIds) {
		objectIds->clear();
	}
	auto cm = scene.getWorld().getComponentManager();
	for (auto& light : cm->getComponentsArray<ECS::SpotLight>()) {
		if (light.obj->getIsActive()) {
			auto ldata = light.getData().generateOGLStruct();
			result.push_back(ldata);
			if (objectIds) {
				objectIds->push_back(light.obj->getIDInt());
			}
		}
	}
	for (auto& light : cm->getComponentsArray<ECS::DirectionalLight>()) {
		if (light.obj->getIsActive()) {
			auto ldata = light.getData().generateOGLStruct();
			result.push_back(ldata);
			if (objectIds) {
				objectIds->push_back(light.obj->getIDInt());
			}
		}
	}
	for (auto& light : cm->getComponentsArray<ECS::PointLight>()) {
		if (light.obj->getIsActive()) {
			auto ldata = light.getData().generateOGLStruct();
			result.push_back(ldata);
			if (objectIds) {
				objectIds->push_back(light.obj->getIDInt());
			}
		}
	}
	for (auto& light : cm->getComponentsArray<ECS::AmbientLight>()) {
		if (light.obj->getIsActive()) {
			auto ldata = light.getData().generateOGLStruct();
			result.push_back(ldata);
			if (objectIds) {
				objectIds->push_back(light.obj->getIDInt());
			}
		}
	}
	for (auto& light : cm->getComponentsArray<ECS::AmbientSphereLight>()) {
		if (light.obj->getIsActive()) {
			auto ldata = light.getData().generateOGLStruct();
			result.push_back(ldata);
			if (objectIds) {
				objectIds->push_back(light.obj->getIDInt());
			}
		}
	}
	return result;
}

std::vector<IKIGAI::RENDER::LightOGL> SceneRenderData::findLightDataInFrustum(
	const Scene& scene, const RENDER::Frustum& p_frustum, std::vector<int>* objectIds) {
	std::vector<RENDER::LightOGL> result;
	if (objectIds) {
		objectIds->clear();
	}

	auto cm = scene.getWorld().getComponentManager();
	for (auto& light : cm->getComponentsArray<ECS::SpotLight>()) {
		if (light.obj->getIsActive()) {
			const auto& lightData = light.getData();
			const auto& position = lightData.getTransform().getWorldPosition();
			auto effectRange = lightData.getEffectRange();

			if (std::isinf(effectRange) || p_frustum.sphereInFrustum(position.x, position.y, position.z, lightData.getEffectRange())) {
				auto ldata = lightData.generateOGLStruct();
				result.push_back(ldata);
				if (objectIds) {
					objectIds->push_back(light.obj->getIDInt());
				}
			}
		}
	}
	for (auto& light : cm->getComponentsArray<ECS::DirectionalLight>()) {
		if (light.obj->getIsActive()) {
			const auto& lightData = light.getData();
			const auto& position = lightData.getTransform().getWorldPosition();
			auto effectRange = lightData.getEffectRange();

			if (std::isinf(effectRange) || p_frustum.sphereInFrustum(position.x, position.y, position.z, lightData.getEffectRange())) {
				auto ldata = lightData.generateOGLStruct();
				result.push_back(ldata);
				if (objectIds) {
					objectIds->push_back(light.obj->getIDInt());
				}
			}
		}
	}
	for (auto& light : cm->getComponentsArray<ECS::PointLight>()) {
		if (light.obj->getIsActive()) {
			const auto& lightData = light.getData();
			const auto& position = lightData.getTransform().getWorldPosition();
			auto effectRange = lightData.getEffectRange();

			if (std::isinf(effectRange) || p_frustum.sphereInFrustum(position.x, position.y, position.z, lightData.getEffectRange())) {
				auto ldata = lightData.generateOGLStruct();
				result.push_back(ldata);
				if (objectIds) {
					objectIds->push_back(light.obj->getIDInt());
				}
			}
		}
	}
	for (auto& light : cm->getComponentsArray<ECS::AmbientLight>()) {
		if (light.obj->getIsActive()) {
			const auto& lightData = light.getData();
			const auto& position = lightData.getTransform().getWorldPosition();
			auto effectRange = lightData.getEffectRange();

			if (std::isinf(effectRange) || p_frustum.sphereInFrustum(position.x, position.y, position.z, lightData.getEffectRange())) {
				auto ldata = lightData.generateOGLStruct();
				result.push_back(ldata);
				if (objectIds) {
					objectIds->push_back(light.obj->getIDInt());
				}
			}
		}
	}
	for (auto& light : cm->getComponentsArray<ECS::AmbientSphereLight>()) {
		if (light.obj->getIsActive()) {
			const auto& lightData = light.getData();
			const auto& position = lightData.getTransform().getWorldPosition();
			auto effectRange = lightData.getEffectRange();

			if (std::isinf(effectRange) || p_frustum.sphereInFrustum(position.x, position.y, position.z, lightData.getEffectRange())) {
				auto ldata = lightData.generateOGLStruct();
				result.push_back(ldata);
				if (objectIds) {
					objectIds->push_back(light.obj->getIDInt());
				}
			}
		}
	}
	return result;
}

std::vector<std::shared_ptr<RENDER::MeshInterface>> SceneRenderData::getMeshesInFrustum(const RENDER::ModelInterface& model,
	const RENDER::BoundingSphere& modelBoundingSphere, const ECS::Transform& modelTransform, const RENDER::Frustum& frustum,
	RENDER::CullingOptions cullingOptions) {
	const bool frustumPerModel = isFlagSet(RENDER::CullingOptions::FRUSTUM_PER_MODEL, cullingOptions);

	if (!frustumPerModel || frustum.boundingSphereInFrustum(modelBoundingSphere, modelTransform)) {
		std::vector<std::shared_ptr<RENDER::MeshInterface>> result;

		const bool frustumPerMesh = isFlagSet(RENDER::CullingOptions::FRUSTUM_PER_MESH, cullingOptions);

		auto& meshes = model.getMeshes();

		for (auto mesh : meshes) {
			if (meshes.size() == 1 || !frustumPerMesh || frustum.boundingSphereInFrustum(mesh->getBoundingSphere(), modelTransform)) {
				result.emplace_back(mesh);
			}
		}
		return result;
	}
	return {};
}

namespace {
template <class Key, class Value>
using ScratchMap = std::unordered_map<Key, Value, std::hash<Key>, std::equal_to<Key>, MEMORY::StlAllocator<std::pair<const Key, Value>>>;
template <class Value> using ScratchVector = std::vector<Value, MEMORY::StlAllocator<Value>>;

void MapChildrenToChunk(
	IKIGAI::ECS::Object* obj, size_t chunkIdx, ScratchMap<IKIGAI::ECS::Entity, size_t>& map, const ScratchMap<IKIGAI::ECS::Entity, size_t>& rtMap) {
	for (auto child : obj->getChildren()) {
		if (rtMap.contains(child->getID())) {
			continue; // child has its own RenderTargetComponent, skip it and its children
		}
		map[child->getID()] = chunkIdx;
		MapChildrenToChunk(child.get(), chunkIdx, map, rtMap);
	}
}

struct ChunkContext {
	std::shared_ptr<MEMORY::IAllocator> domain;
	std::vector<IKIGAI::RENDER::RenderChunk> chunks;
	ScratchMap<IKIGAI::ECS::Entity, size_t> objToChunk;
	ScratchMap<const ECS::Transform*, MATH::Matrix4f> previousWorlds;

	explicit ChunkContext(std::shared_ptr<MEMORY::IAllocator> allocationDomain)
		: domain(allocationDomain ? std::move(allocationDomain) : MEMORY::FrameArena::heapDomain())
		, objToChunk(MEMORY::StlAllocator<std::pair<const ECS::Entity, size_t>>(domain))
		, previousWorlds(MEMORY::StlAllocator<std::pair<const ECS::Transform* const, MATH::Matrix4f>>(domain)) { }

	const MATH::Matrix4f& previousWorld(ECS::Transform& transform) {
		// Each material batch must see the same history even when another batch already advanced the transform.
		return previousWorlds.try_emplace(&transform, transform.getPrevWorldMatrix()).first->second;
	}

	void init(ECS::World& world) {
		chunks.emplace_back(nullptr, nullptr, domain); // Main chunk (index 0)

		auto cm = world.getComponentManager();
		ScratchMap<IKIGAI::ECS::Entity, size_t> rtMap(objToChunk.get_allocator());

		for (auto& rt : cm->getComponentsArray<IKIGAI::ECS::RenderTargetComponent>()) {
			if (!rt.obj->getIsActive()) {
				continue;
			}

			chunks.emplace_back(rt.getFrameBuffer(), rt.getCamera(), domain);
			size_t chunkIdx = chunks.size() - 1;

			rtMap[rt.obj->getID()] = chunkIdx;
			objToChunk[rt.obj->getID()] = chunkIdx;
		}

		for (auto& rt : cm->getComponentsArray<IKIGAI::ECS::RenderTargetComponent>()) {
			if (!rt.obj->getIsActive()) {
				continue;
			}
			if (rt.getType() == IKIGAI::ECS::RenderFlowType::NODE_AND_CHILD) {
				MapChildrenToChunk(&rt.obj.get(), rtMap[rt.obj->getID()], objToChunk, rtMap);
			}
		}
	}

	size_t getChunkIdxForObj(IKIGAI::ECS::Entity id) const {
		auto it = objToChunk.find(id);
		if (it != objToChunk.end()) {
			return it->second;
		}
		return 0;
	}

	IKIGAI::RENDER::RenderChunk& getChunk(size_t idx) {
		return chunks[idx];
	}
};
}

namespace {
const RENDER::Frustum* SurfaceView(const RENDER::RenderChunk& output, const RENDER::Frustum* frustum) {
	if (!frustum || !output.camera) {
		return frustum;
	}
	const auto& camera = output.camera->getCamera();
	return camera.isFrustumGeometryCulling() || camera.isFrustumGeometryBVHCulling() ? &camera.getFrustum() : nullptr;
}
bool IsOrdinaryGpuMesh(const RENDER::MeshInterface& mesh, const RENDER::MaterialInterface& material) {
	const auto shader = material.getShader();
	return mesh.getIndexCount() && mesh.getOffset() == 0 && !mesh.vertexLayout() && mesh.morphTargets().empty() && !material.isBlendable()
		&& (shader || material.isDeferred()) && material.getGPUInstances() == 1 && material.pbrLayers().Layers.empty()
		&& (!shader || !shader->supportsPbrLayers()) && material.deformation().Mode == RENDER::DeformationMode::None;
}

void AppendGpuLod(ECS::ModelLODRenderer& renderer, ChunkContext& ctx, RENDER::GpuSceneSnapshot& snapshot) {
	const auto& models = renderer.getModels();
	auto materials = renderer.obj->getComponent<ECS::MaterialRenderer>();
	if (models.size() < 2 || models.size() > RENDER::GpuSceneLod::MaxLevels || !materials || DeformationPadding(*materials) != 0
		|| renderer.obj->getComponent<ECS::SkeletalComponent>() || renderer.obj->getComponent<ECS::FacialAnimationComponent>()
		|| renderer.obj->getComponent<ECS::ModelRenderer>()) {
		return;
	}
	auto& transform = renderer.obj->getTransform()->getTransform();
	RENDER::GpuSceneLod lod;
	lod.Owner = static_cast<int>(renderer.obj->getID().getUniqueId());
	lod.Position = transform.getWorldPosition();
	lod.Levels = static_cast<uint32_t>(models.size());
	const auto behavior = renderer.getFrustumBehaviour();
	for (uint32_t level = 0; level < lod.Levels; ++level) {
		const auto& model = models[level].m_model;
		lod.Distances[level] = models[level].m_distance;
		if (model->getMeshes().empty()) {
			return;
		}
		for (const auto& mesh : model->getMeshes()) {
			const auto materialIndex = mesh->getMaterialIndex();
			const auto& slots = materials->getMaterials();
			if (materialIndex >= slots.size() || !slots[materialIndex] || !IsOrdinaryGpuMesh(*mesh, *slots[materialIndex])) {
				return;
			}
			RENDER::BoundingSphere bounds { {}, -1.f };
			if (behavior != ECS::EFrustumBehaviour::DISABLED) {
				const auto& localBounds = behavior == ECS::EFrustumBehaviour::CULL_CUSTOM ? renderer.getCustomBoundingSphere()
					: behavior == ECS::EFrustumBehaviour::CULL_MODEL					  ? model->getBoundingSphere()
																						  : mesh->getBoundingSphere();
				bounds = getGlobalBoundingSphere(localBounds, transform);
			}
			lod.Instances.push_back(
				{ lod.Owner, mesh, slots[materialIndex], { transform.getWorldMatrix(), ctx.previousWorld(transform) }, bounds, { {}, -1.f }, level });
		}
	}
	snapshot.Lods.push_back(std::move(lod));
}

void AppendSurfaceDrawables(Scene& scene, ChunkContext& ctx, const MATH::Vector3f& cameraPosition, const RENDER::Frustum* frustum,
	const std::shared_ptr<RENDER::MaterialInterface>& defaultMaterial, RENDER::GpuSceneSnapshot* gpuScene = nullptr) {
	auto cm = scene.getWorld().getComponentManager();
	for (auto& modelRenderer : cm->getComponentsArray<ECS::ModelLODRenderer>()) {
		if (modelRenderer.obj->isAlive() && modelRenderer.obj->getIsActive() && modelRenderer.obj->getIsVisible()) {
			size_t chunkIdx = ctx.getChunkIdxForObj(modelRenderer.obj->getID());
			auto& chunk = ctx.getChunk(chunkIdx);
			if (gpuScene && !chunk.camera) {
				AppendGpuLod(modelRenderer, ctx, *gpuScene);
			}
			const auto* localFrustum = SurfaceView(chunk, frustum);

			MATH::Vector3f localCameraPos = cameraPosition;
			if (chunk.camera) {
				localCameraPos = chunk.camera->obj->tryGetTransform()->getWorldPosition();
			}

			float distanceToActor = MATH::Vector3f::Distance(modelRenderer.obj->getTransform()->getWorldPosition(), localCameraPos);
			if (auto model = modelRenderer.getModelByDistance(distanceToActor)) {
				if (auto materialRenderer = modelRenderer.obj->getComponent<ECS::MaterialRenderer>()) {
					auto& transform = modelRenderer.obj->getTransform()->getTransform();
					auto skeletonComp = modelRenderer.obj->getComponent<ECS::SkeletalComponent>();
					auto animComp = modelRenderer.obj->getComponent<ECS::SkeletalAnimationComponent>();
					auto skeleton = skeletonComp ? skeletonComp->getSkeleton() : nullptr;
					auto animationPlayable = skeletonComp ? skeletonComp->finalPlayable(animComp ? animComp->getPlayable() : nullptr) : nullptr;
					auto animOffset = skeletonComp ? skeletonComp->mAnimOffset.get() : nullptr;
					auto animLocalTransform = skeletonComp ? skeletonComp->mAnimLocalTransform.get() : nullptr;
					auto animGlobalTransform = skeletonComp ? skeletonComp->mAnimGlobalTransform.get() : nullptr;

					const ECS::MaterialRenderer::MaterialList& materials = materialRenderer->getMaterials();

					for (const auto mesh : model->getMeshes()) {
						const auto behavior = modelRenderer.getFrustumBehaviour();
						const auto& bounds = behavior == ECS::EFrustumBehaviour::CULL_CUSTOM ? modelRenderer.getCustomBoundingSphere()
							: behavior == ECS::EFrustumBehaviour::CULL_MODEL				 ? model->getBoundingSphere()
																							 : mesh->getBoundingSphere();
						if (localFrustum && !skeletonComp && DeformationPadding(*materialRenderer) == 0
							&& behavior != ECS::EFrustumBehaviour::DISABLED && !localFrustum->boundingSphereInFrustum(bounds, transform)) {
							continue;
						}
						std::shared_ptr<RENDER::MaterialInterface> material;
						if (mesh->getMaterialIndex() < MAX_MATERIAL_COUNT) {
							material = materials.at(mesh->getMaterialIndex());
							if (!material || (!material->getShader() && !material->isDeferred())) {
								material = defaultMaterial;
							}
						}

						if (material) {
							RENDER::Drawable element
								= { ctx.previousWorld(transform), transform.getWorldMatrix(), mesh, material, skeleton, animationPlayable, animOffset,
									  animLocalTransform, animGlobalTransform, static_cast<int>(modelRenderer.obj->getID().getUniqueId()) };
							if (auto facial = modelRenderer.obj->getComponent<ECS::FacialAnimationComponent>()) {
								facial->bind(*model);
								element.facialAnimation = facial.get();
							}

							if (material->isBlendable()) {
								if (!material->isDeferred()) {
									chunk.transparentDrawablesForward.emplace(distanceToActor, element);
								}
								else {
									chunk.transparentDrawablesDeferred.emplace(distanceToActor, element);
								}
							}
							else {
								if (!material->isDeferred()) {
									chunk.opaqueDrawablesForward.emplace(distanceToActor, element);
								}
								else {
									chunk.opaqueDrawablesDeferred.emplace(distanceToActor, element);
								}
							}
						}
					}
				}
			}
		}
	}

	auto append = [&](auto& component) {
		if (!component.obj->isAlive() || !component.obj->getIsActive() || !component.obj->getIsVisible()) {
			return;
		}
		using T = std::remove_cvref_t<decltype(component)>;
		auto target = component.obj->getID();
		if constexpr (std::is_same_v<T, ECS::FoliageComponent>) {
			component.bind(scene);
			if (auto surface = component.surface()) {
				target = surface->obj->getID();
			}
		}
		auto& output = ctx.getChunk(ctx.getChunkIdxForObj(target));
		auto camera = output.camera ? output.camera->obj->getTransform()->getWorldPosition() : cameraPosition;
		const auto* view = SurfaceView(output, frustum);
		if constexpr (std::is_same_v<T, ECS::FoliageComponent>) {
			ECS::SurfaceSystem::append(scene, component, output, camera, view, defaultMaterial);
		}
		else {
			ECS::SurfaceSystem::append(component, output, camera, view, defaultMaterial);
		}
	};
	for (auto& component : cm->getComponentsArray<ECS::SurfaceComponent>()) {
		append(component);
	}
	for (auto& component : cm->getComponentsArray<ECS::ChunkModelRenderer>()) {
		append(component);
	}
	for (auto& component : cm->getComponentsArray<ECS::FoliageComponent>()) {
		append(component);
	}
}
}

std::vector<IKIGAI::RENDER::RenderChunk> SceneRenderData::findAndSortFrustumCulledBVHDrawables(Scene& scene, const MATH::Vector3f& cameraPosition,
	const RENDER::Frustum& frustum, std::shared_ptr<RENDER::MaterialInterface> defaultMaterial,
	std::shared_ptr<MEMORY::IAllocator> allocationDomain) {
	ChunkContext ctx(std::move(allocationDomain));
	ctx.init(scene.getWorld());

	auto& index = spatial(scene);
	index.refreshPending();
	std::vector<std::shared_ptr<BVHData>> toDraw;
	index.meshesTree.query(RENDER::frustumPredicate(frustum), [&](const auto& payload) { toDraw.push_back(payload); });

	for (const auto& meshData : toDraw) {
		auto owner = meshData->object.lock();
		const auto mesh = meshData->mesh.lock();

		if (mesh && owner && owner->isAlive() && owner->getIsVisible() && owner->getIsActive()) {
			if (auto materialRenderer = owner->getComponent<ECS::MaterialRenderer>()) {
				auto& transform = owner->getTransform()->getTransform();
				auto skeletonComp = owner->getComponent<ECS::SkeletalComponent>();
				auto animComp = owner->getComponent<ECS::SkeletalAnimationComponent>();
				std::shared_ptr<SKELETON::Skeleton> skeleton = skeletonComp ? skeletonComp->getSkeleton() : nullptr;
				SKELETON::IAnimationPlayable* animationPlayable
					= skeletonComp ? skeletonComp->finalPlayable(animComp ? animComp->getPlayable() : nullptr) : nullptr;
				SKELETON::AnimOffset* animOffset = skeletonComp ? skeletonComp->mAnimOffset.get() : nullptr;
				SKELETON::AnimLocalTransform* animLocalTransform = skeletonComp ? skeletonComp->mAnimLocalTransform.get() : nullptr;
				SKELETON::AnimGlobalTransform* animGlobalTransform = skeletonComp ? skeletonComp->mAnimGlobalTransform.get() : nullptr;

				size_t chunkIdx = ctx.getChunkIdxForObj(owner->getID());
				auto& chunk = ctx.getChunk(chunkIdx);

				MATH::Vector3f localCameraPos = cameraPosition;
				if (chunk.camera) {
					localCameraPos = chunk.camera->obj->tryGetTransform()->getWorldPosition();
				}

				float distanceToActor = MATH::Vector3f::Distance(transform.getWorldPosition(), localCameraPos);
				const ECS::MaterialRenderer::MaterialList& materials = materialRenderer->getMaterials();

				std::shared_ptr<RENDER::MaterialInterface> material;
				if (mesh->getMaterialIndex() < MAX_MATERIAL_COUNT) {
					material = materials.at(mesh->getMaterialIndex());
					if (!material || (!material->getShader() && !material->isDeferred())) {
						material = defaultMaterial;
					}
				}

				if (material) {
					RENDER::Drawable element = { ctx.previousWorld(transform), transform.getWorldMatrix(), mesh, material, skeleton,
						animationPlayable, animOffset, animLocalTransform, animGlobalTransform, static_cast<int>(owner->getID().getUniqueId()) };
					if (auto facial = owner->getComponent<ECS::FacialAnimationComponent>()) {
						if (auto modelRenderer = owner->getComponent<ECS::ModelRenderer>(); modelRenderer && modelRenderer->getModel()) {
							facial->bind(*modelRenderer->getModel());
						}
						element.facialAnimation = facial.get();
					}

					if (material->isBlendable()) {
						if (!material->isDeferred()) {
							chunk.transparentDrawablesForward.emplace(distanceToActor, element);
						}
						else {
							chunk.transparentDrawablesDeferred.emplace(distanceToActor, element);
						}
					}
					else {
						if (!material->isDeferred()) {
							chunk.opaqueDrawablesForward.emplace(distanceToActor, element);
						}
						else {
							chunk.opaqueDrawablesDeferred.emplace(distanceToActor, element);
						}
					}
				}
			}
		}
	}

	AppendSurfaceDrawables(scene, ctx, cameraPosition, &frustum, defaultMaterial);
	return std::move(ctx.chunks);
}

std::vector<IKIGAI::RENDER::RenderChunk> SceneRenderData::findAndSortFrustumCulledDrawables(Scene& scene, const MATH::Vector3f& cameraPosition,
	const RENDER::Frustum& frustum, std::shared_ptr<RENDER::MaterialInterface> defaultMaterial, FrustumCullMode mode,
	std::shared_ptr<MEMORY::IAllocator> allocationDomain) {
	IKIGAI_PROFILE_SCOPE("Prepare Linear Frustum Drawables");
	ChunkContext ctx(std::move(allocationDomain));
	ctx.init(scene.getWorld());

	// These handles stay on the scene owner. The job receives only frustum and bounds values.
	struct ModelDrawInput {
		std::shared_ptr<RENDER::ModelInterface> model;
		ECS::Object& owner;
		ECS::Transform& transform;
		const ECS::MaterialRenderer& materialRenderer;
		ECS::SkeletalComponent* skeletonComp = nullptr;
		std::optional<std::size_t> queryIndex = std::nullopt;
	};

	const auto appendDrawables = [&](const ModelDrawInput& input, std::span<const std::shared_ptr<RENDER::MeshInterface>> meshes,
									 std::span<const std::uint8_t> meshVisibility = {}) {
		if (meshes.empty()) {
			return;
		}
		auto& owner = input.owner;
		auto& transform = input.transform;
		auto* skeletonComp = input.skeletonComp;
		auto animComp = owner.getComponent<ECS::SkeletalAnimationComponent>();
		std::shared_ptr<SKELETON::Skeleton> skeleton = skeletonComp ? skeletonComp->getSkeleton() : nullptr;
		auto* animationPlayable = skeletonComp ? skeletonComp->finalPlayable(animComp ? animComp->getPlayable() : nullptr) : nullptr;
		auto* animOffset = skeletonComp ? skeletonComp->mAnimOffset.get() : nullptr;
		auto* animLocalTransform = skeletonComp ? skeletonComp->mAnimLocalTransform.get() : nullptr;
		auto* animGlobalTransform = skeletonComp ? skeletonComp->mAnimGlobalTransform.get() : nullptr;
		auto& chunk = ctx.getChunk(ctx.getChunkIdxForObj(owner.getID()));
		const auto localCameraPos = chunk.camera ? chunk.camera->obj->tryGetTransform()->getWorldPosition() : cameraPosition;
		const auto distanceToActor = MATH::Vector3f::Distance(transform.getWorldPosition(), localCameraPos);
		const auto& materials = input.materialRenderer.getMaterials();

		for (std::size_t index = 0; index < meshes.size(); ++index) {
			if (!meshVisibility.empty() && !meshVisibility[index]) {
				continue;
			}
			const auto& mesh = meshes[index];
			std::shared_ptr<RENDER::MaterialInterface> material;
			if (mesh->getMaterialIndex() < MAX_MATERIAL_COUNT) {
				material = materials.at(mesh->getMaterialIndex());
				if (!material || (!material->getShader() && !material->isDeferred())) {
					material = defaultMaterial;
				}
			}
			if (!material) {
				continue;
			}
			RENDER::Drawable element = { ctx.previousWorld(transform), transform.getWorldMatrix(), mesh, material, skeleton, animationPlayable,
				animOffset, animLocalTransform, animGlobalTransform, static_cast<int>(owner.getID().getUniqueId()) };
			if (auto facial = owner.getComponent<ECS::FacialAnimationComponent>()) {
				facial->bind(*input.model);
				element.facialAnimation = facial.get();
			}

			if (material->isBlendable()) {
				auto& queue = material->isDeferred() ? chunk.transparentDrawablesDeferred : chunk.transparentDrawablesForward;
				queue.emplace(distanceToActor, element);
			}
			else {
				auto& queue = material->isDeferred() ? chunk.opaqueDrawablesDeferred : chunk.opaqueDrawablesForward;
				queue.emplace(distanceToActor, element);
			}
		}
	};

	auto& modelRenderers = scene.getWorld().getComponentManager()->getComponentsArray<ECS::ModelRenderer>();
	const auto rendererCount = static_cast<std::size_t>(modelRenderers.getSize());
	constexpr std::size_t minParallelModels = 1024;
	// The full Scene benchmark amortized snapshot and scheduling costs at 16K visible-parent mesh checks.
	constexpr std::size_t minParallelMeshChecks = 16'384;
	TASK::TaskSystem* tasks = nullptr;
	if ((mode == FrustumCullMode::Parallel || (mode == FrustumCullMode::Automatic && rendererCount >= minParallelModels))
		&& RESOURCES::ServiceManager::Check<TASK::TaskSystem>()) {
		auto& taskSystem = RESOURCES::ServiceManager::Get<TASK::TaskSystem>();
		if (taskSystem.isReady() && taskSystem.getThreadCounts() > 0) {
			tasks = &taskSystem;
		}
	}

	enum class ParentVisibility : std::uint8_t { Unchecked, Culled, Visible };
	ScratchVector<ParentVisibility> checkedParents { MEMORY::StlAllocator<ParentVisibility>(ctx.domain) };
	if (tasks && mode == FrustumCullMode::Automatic) {
		IKIGAI_PROFILE_SCOPE("Estimate Linear Frustum Mesh Work");
		std::size_t remainingMeshChecks = minParallelMeshChecks;
		std::size_t rendererIndex = 0;
		for (const auto& renderer : modelRenderers) {
			const auto index = rendererIndex++;
			if (renderer.getFrustumBehaviour() != ECS::EFrustumBehaviour::CULL_MESHES) {
				continue;
			}
			const auto& owner = renderer.obj.get();
			if (!owner.getIsVisible() || !owner.getIsActive()) {
				continue;
			}
			const auto model = renderer.getModel();
			auto* transform = owner.tryGetTransform();
			if (!model || model->getMeshes().size() < 2 || !transform) {
				continue;
			}
			if (checkedParents.empty()) {
				checkedParents.resize(rendererCount, ParentVisibility::Unchecked);
			}
			const bool visible = frustum.boundingSphereInFrustum(model->getBoundingSphere(), transform->getTransform());
			checkedParents[index] = visible ? ParentVisibility::Visible : ParentVisibility::Culled;
			if (visible) {
				remainingMeshChecks -= std::min(remainingMeshChecks, model->getMeshes().size());
				if (remainingMeshChecks == 0) {
					break;
				}
			}
		}
		if (remainingMeshChecks != 0) {
			tasks = nullptr;
		}
	}

	ScratchVector<ModelDrawInput> pending { MEMORY::StlAllocator<ModelDrawInput>(ctx.domain) };
	ScratchVector<RENDER::FrustumModelQuery> modelQueries { MEMORY::StlAllocator<RENDER::FrustumModelQuery>(ctx.domain) };
	ScratchVector<RENDER::BoundingSphere> meshQueries { MEMORY::StlAllocator<RENDER::BoundingSphere>(ctx.domain) };
	ScratchVector<std::shared_ptr<RENDER::MeshInterface>> serialMeshes { MEMORY::StlAllocator<std::shared_ptr<RENDER::MeshInterface>>(ctx.domain) };
	if (tasks) {
		pending.reserve(rendererCount);
		modelQueries.reserve(rendererCount);
	}
	{
		IKIGAI_PROFILE_SCOPE("Extract Linear Frustum Models");
		std::size_t rendererIndex = 0;
		for (const auto& renderer : modelRenderers) {
			const auto index = rendererIndex++;
			auto& owner = renderer.obj.get();
			if (!owner.getIsVisible() || !owner.getIsActive()) {
				continue;
			}
			auto model = renderer.getModel();
			if (!model || model->getMeshes().empty()) {
				continue;
			}
			auto materials = owner.getComponent<ECS::MaterialRenderer>();
			if (!materials) {
				continue;
			}
			auto skeleton = owner.getComponent<ECS::SkeletalComponent>();
			ModelDrawInput input { std::move(model), owner, owner.getTransform()->getTransform(), *materials, skeleton ? skeleton.get() : nullptr };
			const auto behavior = renderer.getFrustumBehaviour();
			const bool bypass = behavior == ECS::EFrustumBehaviour::DISABLED || (skeleton && skeleton->finalPlayable(nullptr))
				|| DeformationPadding(*materials) > 0;
			if (bypass) {
				if (tasks) {
					pending.push_back(std::move(input));
				}
				else {
					appendDrawables(input, input.model->getMeshes());
				}
				continue;
			}

			const auto& modelBounds
				= behavior == ECS::EFrustumBehaviour::CULL_CUSTOM ? renderer.getCustomBoundingSphere() : input.model->getBoundingSphere();
			const auto& meshes = input.model->getMeshes();
			const bool perMesh = behavior == ECS::EFrustumBehaviour::CULL_MESHES && meshes.size() > 1;
			const auto parent = checkedParents.empty() ? ParentVisibility::Unchecked : checkedParents[index];
			if (parent == ParentVisibility::Culled) {
				continue;
			}
			if (!tasks) {
				if (parent != ParentVisibility::Visible && !frustum.boundingSphereInFrustum(modelBounds, input.transform)) {
					continue;
				}
				if (!perMesh) {
					appendDrawables(input, meshes);
					continue;
				}
				serialMeshes.clear();
				for (const auto& mesh : meshes) {
					if (frustum.boundingSphereInFrustum(mesh->getBoundingSphere(), input.transform)) {
						serialMeshes.push_back(mesh);
					}
				}
				appendDrawables(input, serialMeshes);
				continue;
			}
			if (mode == FrustumCullMode::Automatic && perMesh && parent == ParentVisibility::Unchecked
				&& !frustum.boundingSphereInFrustum(modelBounds, input.transform)) {
				continue;
			}

			RENDER::FrustumModelQuery query { modelBounds, input.transform.getWorldPosition(), input.transform.getWorldRotation(),
				input.transform.getWorldScale(), meshQueries.size() };
			if (perMesh) {
				query.meshCount = meshes.size();
				for (const auto& mesh : meshes) {
					meshQueries.push_back(mesh->getBoundingSphere());
				}
			}
			input.queryIndex = modelQueries.size();
			modelQueries.push_back(query);
			pending.push_back(std::move(input));
		}
	}
	if (tasks) {
		auto* cullTasks = mode == FrustumCullMode::Parallel || meshQueries.size() >= minParallelMeshChecks ? tasks : nullptr;
		ScratchVector<std::uint8_t> visibleModels(modelQueries.size(), std::uint8_t {}, MEMORY::StlAllocator<std::uint8_t>(ctx.domain));
		ScratchVector<std::uint8_t> visibleMeshes(meshQueries.size(), std::uint8_t {}, MEMORY::StlAllocator<std::uint8_t>(ctx.domain));
		RENDER::evaluateFrustumVisibility(frustum, modelQueries, meshQueries, visibleModels, visibleMeshes, cullTasks);
		IKIGAI_PROFILE_SCOPE("Merge Linear Frustum Drawables");
		// Equivalent multimap keys retain their original model and mesh insertion order.
		for (const auto& input : pending) {
			std::span<const std::uint8_t> meshVisibility;
			if (input.queryIndex) {
				const auto index = *input.queryIndex;
				if (!visibleModels[index]) {
					continue;
				}
				const auto& query = modelQueries[index];
				meshVisibility = std::span<const std::uint8_t>(visibleMeshes).subspan(query.firstMesh, query.meshCount);
			}
			appendDrawables(input, input.model->getMeshes(), meshVisibility);
		}
	}

	AppendSurfaceDrawables(scene, ctx, cameraPosition, &frustum, defaultMaterial);
	return std::move(ctx.chunks);
}

std::vector<IKIGAI::RENDER::RenderChunk> SceneRenderData::findAndSortDrawables(Scene& scene, const MATH::Vector3f& cameraPosition,
	std::shared_ptr<RENDER::MaterialInterface> defaultMaterial, RENDER::GpuSceneSnapshot* gpuScene,
	std::shared_ptr<MEMORY::IAllocator> allocationDomain) {
	IKIGAI_PROFILE_SCOPE("Extract and Sort Drawables");
	ChunkContext ctx(std::move(allocationDomain));
	ctx.init(scene.getWorld());

	auto cm = scene.getWorld().getComponentManager();
	for (auto& modelRenderer : cm->getComponentsArray<ECS::ModelRenderer>()) {
		if (modelRenderer.obj->getIsVisible() && modelRenderer.obj->getIsActive() && modelRenderer.obj->getName() != "Skybox") {
			if (auto model = modelRenderer.getModel()) {
				if (auto materialRenderer = modelRenderer.obj->getComponent<ECS::MaterialRenderer>()) {
					auto& transform = modelRenderer.obj->getTransform()->getTransform();
					auto skeletonComp = modelRenderer.obj->getComponent<ECS::SkeletalComponent>();
					auto animComp = modelRenderer.obj->getComponent<ECS::SkeletalAnimationComponent>();
					std::shared_ptr<SKELETON::Skeleton> skeleton = skeletonComp ? skeletonComp->getSkeleton() : nullptr;
					SKELETON::IAnimationPlayable* animationPlayable
						= skeletonComp ? skeletonComp->finalPlayable(animComp ? animComp->getPlayable() : nullptr) : nullptr;
					SKELETON::AnimOffset* animOffset = skeletonComp ? skeletonComp->mAnimOffset.get() : nullptr;
					SKELETON::AnimLocalTransform* animLocalTransform = skeletonComp ? skeletonComp->mAnimLocalTransform.get() : nullptr;
					SKELETON::AnimGlobalTransform* animGlobalTransform = skeletonComp ? skeletonComp->mAnimGlobalTransform.get() : nullptr;

					size_t chunkIdx = ctx.getChunkIdxForObj(modelRenderer.obj->getID());
					auto& chunk = ctx.getChunk(chunkIdx);

					MATH::Vector3f localCameraPos = cameraPosition;
					if (chunk.camera) {
						localCameraPos = chunk.camera->obj->tryGetTransform()->getWorldPosition();
					}

					float distanceToActor = MATH::Vector3f::Distance(transform.getWorldPosition(), localCameraPos);

					const ECS::MaterialRenderer::MaterialList& materials = materialRenderer->getMaterials();

					const bool gpuObject = gpuScene && !chunk.camera && !skeletonComp && DeformationPadding(*materialRenderer) == 0;
					const auto behavior = modelRenderer.getFrustumBehaviour();
					for (const auto mesh : model->getMeshes()) {
						std::shared_ptr<RENDER::MaterialInterface> material;
						if (mesh->getMaterialIndex() < MAX_MATERIAL_COUNT) {
							material = materials.at(mesh->getMaterialIndex());
							if (!material || (!material->getShader() && !material->isDeferred())) {
								material = materials.at(0);
							}
						}

						if (material) {
							RENDER::Drawable element
								= { ctx.previousWorld(transform), transform.getWorldMatrix(), mesh, material, skeleton, animationPlayable, animOffset,
									  animLocalTransform, animGlobalTransform, static_cast<int>(modelRenderer.obj->getID().getUniqueId()) };
							if (auto facial = modelRenderer.obj->getComponent<ECS::FacialAnimationComponent>()) {
								facial->bind(*model);
								element.facialAnimation = facial.get();
							}
							if (gpuObject && !element.facialAnimation && material == materials.at(mesh->getMaterialIndex())
								&& IsOrdinaryGpuMesh(*mesh, *material)) {
								RENDER::BoundingSphere modelBounds { {}, -1.f };
								RENDER::BoundingSphere meshBounds { {}, -1.f };
								if (behavior != ECS::EFrustumBehaviour::DISABLED || gpuScene->UseBvhBounds) {
									const auto& bounds = behavior == ECS::EFrustumBehaviour::CULL_CUSTOM ? modelRenderer.getCustomBoundingSphere()
																										 : model->getBoundingSphere();
									modelBounds = getGlobalBoundingSphere(bounds, transform);
									if (!gpuScene->UseBvhBounds && behavior == ECS::EFrustumBehaviour::CULL_MESHES && model->getMeshes().size() > 1) {
										meshBounds = getGlobalBoundingSphere(mesh->getBoundingSphere(), transform);
									}
								}
								gpuScene->Instances.push_back(
									{ element.OwnerId, mesh, material, { element.world, element.prevWorld, element.tint }, modelBounds, meshBounds });
							}

							if (material->isBlendable()) {
								if (!material->isDeferred()) {
									chunk.transparentDrawablesForward.emplace(distanceToActor, element);
								}
								else {
									chunk.transparentDrawablesDeferred.emplace(distanceToActor, element);
								}
							}
							else {
								if (!material->isDeferred()) {
									chunk.opaqueDrawablesForward.emplace(distanceToActor, element);
								}
								else {
									chunk.opaqueDrawablesDeferred.emplace(distanceToActor, element);
								}
							}
						}
					}
				}
			}
		}
	}

	AppendSurfaceDrawables(scene, ctx, cameraPosition, nullptr, defaultMaterial, gpuScene);
	return std::move(ctx.chunks);
}

std::vector<IKIGAI::RENDER::RenderChunk> SceneRenderData::findDrawables(Scene& scene, const MATH::Vector3f& cameraPosition,
	const RENDER::Camera& camera, const RENDER::Frustum* customFrustum, std::shared_ptr<RENDER::MaterialInterface> defaultMaterial,
	std::shared_ptr<MEMORY::IAllocator> allocationDomain) {
	IKIGAI_PROFILE_SCOPE("Extract Camera Drawables");
	auto cullingProfile = PROFILER::Profiler::IsEnabled() ? std::make_unique<PROFILER::ProfilerSpy>("Scene::findDrawables") : nullptr;
	if (camera.isFrustumGeometryBVHCulling()) {
		const auto& frustum = customFrustum ? *customFrustum : camera.getFrustum();
		return findAndSortFrustumCulledBVHDrawables(scene, cameraPosition, frustum, defaultMaterial, std::move(allocationDomain));
	}
	else if (camera.isFrustumGeometryCulling()) {
		const auto& frustum = customFrustum ? *customFrustum : camera.getFrustum();
		return findAndSortFrustumCulledDrawables(
			scene, cameraPosition, frustum, defaultMaterial, FrustumCullMode::Automatic, std::move(allocationDomain));
	}
	else {
		return findAndSortDrawables(scene, cameraPosition, defaultMaterial, nullptr, std::move(allocationDomain));
	}
}
