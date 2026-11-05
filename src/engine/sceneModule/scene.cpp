#include "scene.h"
#include "coreModule/ecs/components/capsuleColliderComponent.h"
#include "coreModule/ecs/components/characterMotorComponent.h"
#include "coreModule/ecs/components/skeletalStateGraphComponent.h"
#include "coreModule/ecs/components/thirdPersonCameraComponent.h"
#include "coreModule/ecs/components/traversalComponent.h"
#include "coreModule/ecs/components/variableContextComponent.h"
#include "coreModule/ecs/systems/behaviorTreeSystem.h"
#include "coreModule/ecs/systems/characterSystem.h"
#include "coreModule/ecs/systems/environmentSystem.h"
#include "coreModule/ecs/systems/skeletalStateGraphSystem.h"
#include "coreModule/ecs/systems/surfaceSystem.h"
#include "coreModule/ecs/systems/variableContextSystem.h"
#include "coreModule/testHooks.h"
#include "profilerModule/profiler.h"
#include "profilerModule/profilerSpy.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/components/ragdollComponent.h"
#include "coreModule/ecs/systems/destructionSystem.h"
#include "coreModule/ecs/systems/physics3dSystem.h"
#include "coreModule/ecs/systems/ragdollSystem.h"
#endif
#if USE_PHYSICS_2D
#include "coreModule/ecs/systems/physics2dSystem.h"
#endif

#include <algorithm>

#include "coreModule/ecs/componentTypes.h"
#include <renderModule/backends/interface/renderEnums.h>
#include <resourceModule/parser/assimpParser.h>

#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/components/renderTargetComponent.h"
#include "coreModule/ecs/components/skeletalAnimationComponent.h"
#include "coreModule/ecs/components/skeletalComponent.h"
#include "coreModule/ecs/object.h"
#include "renderModule/backends/interface/meshInterface.h"
#include "renderModule/spatial/frustumBvh.h"
#include "utilsModule/log/loggerDefine.h"
#include <unordered_map>
#include <unordered_set>

namespace IKIGAI {
namespace ECS { }
}

namespace IKIGAI {
namespace RENDER {
	enum class CullingOptions;
}
}

using namespace IKIGAI;
using namespace IKIGAI::SCENE_SYSTEM;

struct BVHData {
	UTILS::Ref<ECS::Object> obj;
	std::shared_ptr<RENDER::MeshInterface> mesh;

	BVHData(UTILS::Ref<ECS::Object> obj, std::shared_ptr<RENDER::MeshInterface> mesh)
		: obj(obj)
		, mesh(mesh) { }

	const ECS::Transform& getTransform() {
		return obj->mTransform->getTransform();
	}
};

struct Scene::SpatialIndex {
	RENDER::FrustumBvh<std::shared_ptr<BVHData>> meshesTree;
	std::unordered_map<ECS::Object::Id, std::list<std::shared_ptr<BVHData>>> objectToBVHElements;
};
Scene::Scene(ECS::World* world, PhysicsFactories physics)
	: mPhysicsFactories(std::move(physics))
	, mWorld(world) { }
void Scene::setPhysicsFactories(PhysicsFactories physics) {
#if USE_PHYSICS_2D
	if (mPhysics2D) {
		throw std::logic_error("Configure physics before creating the scene's world");
	}
#endif
#if USE_PHYSICS_3D
	if (mPhysics3D) {
		throw std::logic_error("Configure physics before creating the scene's world");
	}
#endif
	mPhysicsFactories = std::move(physics);
}
#if USE_PHYSICS_3D
PHYSICS::PhysicWorld& Scene::physics3d() {
	if (!mPhysics3D) {
		mPhysics3D = std::make_unique<ECS::Physics3DSystem>(
			*this, mPhysicsFactories.physics3d ? mPhysicsFactories.physics3d() : std::make_unique<PHYSICS::World>());
	}
	return mPhysics3D->simulation();
}
void Scene::stepPhysics3D(std::chrono::duration<double> dt, bool beginDestructionFrame) {
	physics3d();
	if (beginDestructionFrame) {
		ECS::DestructionSystem::beginFrame(*this);
	}
	const bool destructionStep = !mPhysics3D->paused || mPhysics3D->singleStep;
	if (destructionStep) {
		ECS::DestructionSystem::beforePhysics(*this, float(dt.count()));
	}
	ECS::RagdollSystem::beforePhysics(*this, float(dt.count()));
	ECS::CommandBuffer commands;
	mPhysics3D->onFixedUpdate(getWorld(), commands, dt);
	commands.execute();
	ECS::RagdollSystem::afterPhysics(*this);
	if (destructionStep) {
		ECS::DestructionSystem::afterPhysics(*this, float(dt.count()));
	}
	if (beginDestructionFrame) {
		presentDestructibles(1);
	}
}
void Scene::presentDestructibles(float alpha) {
	ECS::DestructionSystem::present(*this, mPhysics3D && mPhysics3D->paused ? 1.f : alpha);
}
std::shared_ptr<PHYSICS::PhysicWorld> Scene::physics3dShared() {
	physics3d();
	return mPhysics3D->sharedSimulation();
}
#ifdef USE_EDITOR
void Scene::drawPhysics3DDebug() {
	if (mPhysics3D) {
		mPhysics3D->drawDebug();
	}
}
#endif
#endif
#if USE_PHYSICS_2D
PHYSICS2D::World& Scene::physics2d() {
	if (!mPhysics2D) {
		mPhysics2D = std::make_unique<ECS::Physics2DSystem>(
			*this, mPhysicsFactories.physics2d ? mPhysicsFactories.physics2d() : std::make_unique<PHYSICS2D::World>());
	}
	return mPhysics2D->simulation();
}
void Scene::stepPhysics2D(std::chrono::duration<double> dt) {
	physics2d();
	ECS::CommandBuffer commands;
	mPhysics2D->onFixedUpdate(getWorld(), commands, dt);
	commands.execute();
}
#ifdef USE_EDITOR
void Scene::drawPhysics2DDebug() {
	if (mPhysics2D) {
		mPhysics2D->drawDebug();
	}
}
#endif
#endif
Scene::SpatialIndex& Scene::spatial() {
	if (!mSpatial) {
		mSpatial = std::make_unique<SpatialIndex>();
	}
	return *mSpatial;
}
ECS::World& Scene::getWorld() const {
	return mWorld ? *mWorld : RESOURCES::ServiceManager::Get<ECS::World>();
}

void Scene::init() { }

COROUTINE::CoroutineScope& Scene::coroutines() {
	if (!mCoroutineScope) {
		mCoroutineScope = std::make_unique<COROUTINE::CoroutineScope>(RESOURCES::ServiceManager::Get<COROUTINE::Scheduler>());
	}
	return *mCoroutineScope;
}
void Scene::cancelTasks() {
	mCoroutineScope.reset();
}

Scene::Scene(const Descriptor& _descriptor)
	: Scene() {
	for (const auto& object : _descriptor.Objects) {
		createObject(object);
	}
}

Scene::~Scene() {
	cancelTasks();
	// The spatial index owns mesh references; release them before the GPU driver.
	for (const auto& object : objects) {
		if (auto component = object->getComponent<ECS::ModelRenderer>()) {
			removeFromBVH(component.get());
		}
		object->dispose();
	}
	objects.clear();
}

void Scene::go() {
	isExecute = true;

	for (auto& o : objects) {
		if (o->getIsActive()) {
			o->onEnable();
		}
	}
	for (auto& o : objects) {
		if (o->getIsActive()) {
			o->onStart();
		}
	}
	// Resolve the environment's typed light reference before scene loading reports success.
	if (auto* environment = ECS::EnvironmentSystem::active(*this)) {
		environment->update(*this, 0.0);
	}
}

bool Scene::getIsExecute() const {
	return isExecute;
}

void Scene::update(std::chrono::duration<double> dt) {
	ECS::SurfaceSystem::beginFrame(*this);
	ECS::EnvironmentSystem::update(*this, dt.count());
#if USE_PHYSICS_3D
	ECS::RagdollSystem::update(*this, std::clamp(float(dt.count()), 0.f, .1f));
	ECS::CharacterSystem::update(*this, std::clamp(float(dt.count()), 0.f, .1f));
	if (TESTHOOK::waitForFrame) {
		TESTHOOK::waitForFrame(*this);
	}
#endif
	for (auto& o : objects) {
		o->onUpdate(dt);
	}
	// A scene owns its particle instances through its objects. No render pass ticks them.
	for (auto& o : objects) {
		if (o->getIsActive()) {
			if (auto c = o->getComponent<ECS::ParticleSystem2DComponent>()) {
				c->tick(dt.count());
			}
			if (auto c = o->getComponent<ECS::ParticleSystem3DComponent>()) {
				c->tick(dt.count());
			}
			if (auto c = o->getComponent<ECS::SpriteParticleComponent>()) {
				c->tick(dt.count());
			}
		}
	}
}

void Scene::fixedUpdate(std::chrono::duration<double> dt) {
	for (auto& o : objects) {
		o->onFixedUpdate(dt);
	}
}
void Scene::finishFixedUpdate(std::chrono::duration<double> dt) {
	// Called after ECS producers and physics have completed for this tick.
	// Scene-local ordered systems. Bound properties may target any component;
	// these phases deliberately run serially, after producers and before consumers.
	ECS::BehaviorTreeSystem::applyCommands(*this);
#if USE_PHYSICS_3D
	ECS::CharacterSystem::fixedUpdate(*this, float(dt.count()));
#endif
	ECS::VariableContextSystem::update(*this);
	ECS::BehaviorTreeSystem::update(*this, dt.count());
	ECS::SkeletalStateGraphSystem::update(*this, float(dt.count()));
#if USE_PHYSICS_3D
	ECS::RagdollSystem::afterAnimation(*this, float(dt.count()));
#endif
#if USE_PHYSICS_3D
	if (TESTHOOK::afterAnimation) {
		TESTHOOK::afterAnimation(*this, float(dt.count()));
	}
#endif
}

void Scene::lateUpdate(std::chrono::duration<double> dt) {
	for (auto& o : objects) {
		o->onLateUpdate(dt);
	}
}

std::shared_ptr<IKIGAI::ECS::Object> Scene::createObject() {
	return createObject("New Actor");
}

std::shared_ptr<IKIGAI::ECS::Object> Scene::createObject(ECS::Entity actorID, const std::string& name, const std::string& tag) {
	objects.push_back(MEMORY::MakeShared<ECS::Object, MEMORY::Category::Node>(actorID, name, tag, &getWorld()));
	auto& instance = objects.back();
	if (isExecute) {
		instance->setActive(true);
		if (instance->getIsActive()) {
			instance->onEnable();
			instance->onStart();
		}
	}

	if (isSceneReady) {
		instance->componentAddedEvent.add(std::bind(&Scene::addToBVH, this, std::placeholders::_1));
		instance->componentRemovedEvent.add(std::bind(&Scene::removeFromBVH, this, std::placeholders::_1));
		instance->componentChangedEvent.add(std::bind(&Scene::updateInBVH, this, std::placeholders::_1));
	}
	return instance;
}

std::shared_ptr<IKIGAI::ECS::Object> Scene::createObject(const ECS::Object::Descriptor& data) {
	objects.push_back(MEMORY::MakeShared<ECS::Object, MEMORY::Category::Node>(data, &getWorld()));
	auto& instance = objects.back();
	if (isExecute) {
		instance->setActive(data.IsActive);
		if (instance->getIsActive()) {
			instance->onEnable();
			instance->onStart();
		}
	}
	if (data.ParentId > 0) {
		auto p = findObjectByID(ECS::Entity(ECS::Entity::ID(data.ParentId)));
		if (!p) {
			LOG_ERROR << "Object::setParentId: can not find actor with id: " + std::to_string(data.ParentId);
		}
		else {
			instance->setParent(p);
		}
	}
	return instance;
}

std::shared_ptr<IKIGAI::ECS::Object> Scene::createObject(const std::string& name, const std::string& tag) {
	objects.push_back(MEMORY::MakeShared<ECS::Object, MEMORY::Category::Node>(getWorld().createEntity(), name, tag, &getWorld()));
	auto& instance = objects.back();
	if (isExecute) {
		instance->setActive(true);
		if (instance->getIsActive()) {
			instance->onEnable();
			instance->onStart();
		}
	}
	return instance;
}

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

void Scene::addToBVH(UTILS::WeakPtr<ECS::ComponentBase> component) {
	if (component->getName() != "ModelRenderer") {
		return;
	}

	auto modelRenderer = component->obj->getComponent<ECS::ModelRenderer>();
	if (auto model = modelRenderer->getModel()) {
		RENDER::CullingOptions cullingOptions = RENDER::CullingOptions::NONE;

		if (modelRenderer->getFrustumBehaviour() != ECS::EFrustumBehaviour::DISABLED) {
			cullingOptions |= RENDER::CullingOptions::FRUSTUM_PER_MODEL;
		}

		if (modelRenderer->getFrustumBehaviour() == ECS::EFrustumBehaviour::CULL_MESHES) {
			cullingOptions |= RENDER::CullingOptions::FRUSTUM_PER_MESH;
		}

		const auto& modelBoundingSphere = modelRenderer->getFrustumBehaviour() == ECS::EFrustumBehaviour::CULL_CUSTOM
			? modelRenderer->getCustomBoundingSphere()
			: model->getBoundingSphere();
		// TODO: add support RENDER::CullingOptions::FRUSTUM_PER_MESH
		// TODO: add event for change CullingOptions
		auto gbs = getGlobalBoundingSphere(modelBoundingSphere, modelRenderer->obj->mTransform->getTransform());
		if (auto materials = modelRenderer->obj->getComponent<ECS::MaterialRenderer>()) {
			const auto scale = modelRenderer->obj->mTransform->getWorldScale();
			gbs.radius += DeformationPadding(*materials) * std::max({ 1.0f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
		}
#if USE_PHYSICS_3D
		if (auto ragdoll = modelRenderer->obj->getComponent<ECS::RagdollComponent>(); ragdoll && ragdoll->state() == ECS::RagdollState::Simulated) {
			auto skeletal = modelRenderer->obj->getComponent<ECS::SkeletalComponent>();
			auto* pose = skeletal ? dynamic_cast<SKELETON::RagdollInstance*>(skeletal->finalPlayable(nullptr)) : nullptr;
			if (pose) {
				gbs.radius += pose->boundsRadius() + (gbs.position - modelRenderer->obj->mTransform->getWorldPosition()).magnitude();
				gbs.position = modelRenderer->obj->mTransform->getWorldPosition();
			}
		}
#endif
		for (auto mesh : model->getMeshes()) {
			auto node = std::make_shared<BVHData>(component->obj, mesh);
			spatial().meshesTree.upsert(node, gbs);
			spatial().objectToBVHElements[component->obj->getID()].push_back(node);
		}
	}
}

void Scene::removeFromBVH(UTILS::WeakPtr<ECS::ComponentBase> component) {
	if (!component || (component->getName() != "ModelRenderer" && component->getName() != "TransformComponent")) {
		return;
	}
	if (!component->obj->getComponent<ECS::ModelRenderer>()) {
		return;
	}

	if (spatial().objectToBVHElements.contains(component->obj->getID())) {
		for (auto e : spatial().objectToBVHElements[component->obj->getID()]) {
			spatial().meshesTree.remove(e);
		}
		spatial().objectToBVHElements.erase(component->obj->getID());
	}
}

// TODO: add subscribe for it
void Scene::updateInBVH(UTILS::WeakPtr<ECS::ComponentBase> component) {
	if (component->getName() != "ModelRenderer") {
		return;
	}

	removeFromBVH(component);
	addToBVH(component);
}

void Scene::postLoad() {
	for (auto obj : objects) {
		obj->componentAddedEvent.add(std::bind(&Scene::addToBVH, this, std::placeholders::_1));
		obj->componentRemovedEvent.add(std::bind(&Scene::removeFromBVH, this, std::placeholders::_1));
		obj->componentChangedEvent.add(std::bind(&Scene::updateInBVH, this, std::placeholders::_1));
		if (auto component = obj->getComponent<ECS::ModelRenderer>()) {
			addToBVH(component.get());
		}
	}
	isSceneReady = true;
}

void Scene::postLoadObject(ECS::Object& object) {
	object.componentAddedEvent.add(std::bind(&Scene::addToBVH, this, std::placeholders::_1));
	object.componentRemovedEvent.add(std::bind(&Scene::removeFromBVH, this, std::placeholders::_1));
	object.componentChangedEvent.add(std::bind(&Scene::updateInBVH, this, std::placeholders::_1));
	if (auto component = object.getComponent<ECS::ModelRenderer>()) {
		addToBVH(component.get());
	}
}

std::shared_ptr<ECS::Object> Scene::createDeferredObject(const ECS::Object::Descriptor& descriptor) {
	auto shell = descriptor;
	shell.Components.clear();
	shell.IsActive = false;
	objects.push_back(MEMORY::MakeShared<ECS::Object, MEMORY::Category::Node>(shell, &getWorld(), true));
	return objects.back();
}

void Scene::removeLastObject() {
	if (objects.empty()) {
		return;
	}
	if (auto component = objects.back()->getComponent<ECS::ModelRenderer>()) {
		removeFromBVH(component.get());
	}
	objects.back()->detachFromParent();
	objects.back()->dispose();
	objects.pop_back();
}

const DOCUMENT::Instance* Scene::prefabFor(int objectId) const {
	for (const auto& instance : prefabInstances) {
		for (auto [local, id] : DOCUMENT::ReadMap(instance.entry)) {
			if (id == objectId) {
				return &instance;
			}
		}
	}
	return nullptr;
}
bool Scene::isPrefabRoot(int objectId) const {
	const auto* instance = prefabFor(objectId);
	return instance && DOCUMENT::Id(instance->entry.at("Id")) == objectId;
}
nlohmann::json Scene::serializeObjects() const {
	auto result = nlohmann::json::array();
	std::vector<std::shared_ptr<ECS::Object>> stack;
	for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
		if (!(*it)->hasParent()) {
			stack.push_back(*it);
		}
	}
	std::unordered_set<int> visited;
	while (!stack.empty()) {
		auto object = stack.back();
		stack.pop_back();
		if (!visited.insert(object->getIDInt()).second) {
			throw std::runtime_error("Scene hierarchy contains a cycle or duplicate ID");
		}
		auto json = UTILS::ToJson(object->getDescriptor());
		if (json.isErr()) {
			throw std::runtime_error(json.unwrapErr().text);
		}
		result.push_back(json.unwrap());
		auto children = object->getChildren();
		for (auto it = children.rbegin(); it != children.rend(); ++it) {
			stack.push_back(*it);
		}
	}
	if (visited.size() != objects.size()) {
		throw std::runtime_error("Scene contains orphaned or cyclic objects");
	}
	return result;
}
void Scene::capturePrefabBaselines() {
	for (auto& instance : prefabInstances) {
		if (!instance.baseline.empty()) {
			continue;
		}
		const auto descriptors = serializeObjects();
		std::unordered_set<int> owned;
		for (auto [local, id] : DOCUMENT::ReadMap(instance.entry)) {
			owned.insert(id);
		}
		for (const auto& object : descriptors) {
			if (owned.contains(DOCUMENT::Id(object.at("Id")))) {
				instance.baseline.push_back(object);
			}
		}
	}
}
void Scene::unpackPrefab(int rootId) {
	std::erase_if(prefabInstances, [rootId](const auto& instance) { return DOCUMENT::Id(instance.entry.at("Id")) == rootId; });
}
std::shared_ptr<ECS::Object> Scene::appendDocument(const DOCUMENT::Expanded& document) {
	const auto first = objects.size();
	const auto firstInstance = prefabInstances.size();
	std::vector<ECS::Object::Descriptor> descriptors;
	auto combined = serializeObjects();
	for (auto value : document.objects) {
		combined.push_back(value);
		auto descriptor = UTILS::FromJson<ECS::Object::Descriptor>(value);
		if (descriptor.isErr()) {
			throw std::runtime_error(descriptor.unwrapErr().text);
		}
		descriptors.push_back(descriptor.unwrap());
	}
	DOCUMENT::Validate(combined);
	try {
		for (const auto& descriptor : descriptors) {
			createDeferredObject(descriptor);
		}
		for (std::size_t i = 0; i < descriptors.size(); ++i) {
			auto& object = objects[first + i];
			object->setVisible(descriptors[i].IsVisible);
			for (const auto& component : descriptors[i].Components) {
				object->addSerializedComponent(component);
			}
		}
		for (std::size_t i = 0; i < descriptors.size(); ++i) {
			if (descriptors[i].ParentId > 0) {
				objects[first + i]->setParent(findObjectByID(ECS::Entity(ECS::Entity::ID(descriptors[i].ParentId))));
			}
			objects[first + i]->setPendingActive(descriptors[i].IsActive);
		}
		for (std::size_t i = first; i < objects.size(); ++i) {
			postLoadObject(*objects[i]);
		}
		for (const auto& source : document.instances) {
			auto instance = source;
			if (instance.baseline.empty()) {
				const auto ids = DOCUMENT::ReadMap(instance.entry);
				for (auto value : serializeObjects()) {
					const int id = DOCUMENT::Id(value.at("Id"));
					if (std::any_of(ids.begin(), ids.end(), [id](auto pair) { return pair.second == id; })) {
						instance.baseline.push_back(std::move(value));
					}
				}
			}
			prefabInstances.push_back(std::move(instance));
		}
		for (std::size_t i = first; i < objects.size(); ++i) {
			objects[i]->activateLoaded();
		}
		if (!document.instances.empty()) {
			return findObjectByID(ECS::Entity(ECS::Entity::ID(DOCUMENT::Id(document.instances.front().entry.at("Id")))));
		}
		return first < objects.size() ? objects[first] : nullptr;
	} catch (...) {
		while (objects.size() > first) {
			removeLastObject();
		}
		prefabInstances.resize(firstInstance);
		throw;
	}
}
void Scene::destroySubtree(int rootId) {
	auto root = findObjectByID(ECS::Entity(ECS::Entity::ID(rootId)));
	if (!root) {
		return;
	}
	std::unordered_set<int> ids;
	std::vector<std::shared_ptr<ECS::Object>> stack { root };
	while (!stack.empty()) {
		auto object = stack.back();
		stack.pop_back();
		if (!ids.insert(object->getIDInt()).second) {
			continue;
		}
		for (auto child : object->getChildren()) {
			stack.push_back(child);
		}
	}
	for (auto& object : objects) {
		if (ids.contains(object->getIDInt())) {
			object->detachFromParent();
		}
	}
	for (auto& object : objects) {
		if (ids.contains(object->getIDInt())) {
			if (auto component = object->getComponent<ECS::ModelRenderer>()) {
				removeFromBVH(component.get());
			}
			object->dispose();
		}
	}
	std::erase_if(objects, [&](const auto& object) { return ids.contains(object->getIDInt()); });
	std::erase_if(prefabInstances, [&](const auto& instance) { return ids.contains(DOCUMENT::Id(instance.entry.at("Id"))); });
}
void Scene::restoreObjectPosition(int rootId, std::size_t objectIndex, int siblingIndex) {
	auto root = findObjectByID(ECS::Entity(ECS::Entity::ID(rootId)));
	if (!root) {
		return;
	}
	std::erase(objects, root);
	objects.insert(objects.begin() + std::min(objectIndex, objects.size()), root);
	if (auto parent = root->getParent()) {
		const int last = static_cast<int>(parent->getChildren().size()) - 1;
		root->setParentInPos(parent, std::min(siblingIndex, last));
	}
}

std::shared_ptr<ECS::Object> Scene::createObjectAfter(ECS::Object::Id parentId, const std::string& name, const std::string& tag) {
	const auto iter = std::ranges::find_if(objects, [parentId](auto& e) { return e->getID() == parentId; });
	auto resIt = objects.insert(iter + 1, MEMORY::MakeShared<ECS::Object, MEMORY::Category::Node>(getWorld().createEntity(), name, tag, &getWorld()));
	return *resIt;
}

std::shared_ptr<ECS::Object> Scene::createObjectBefore(ECS::Object::Id parentId, const std::string& name, const std::string& tag) {
	const auto iter = std::ranges::find_if(objects, [parentId](auto& e) { return e->getID() == parentId; });
	auto resIt = objects.insert(iter, MEMORY::MakeShared<ECS::Object, MEMORY::Category::Node>(getWorld().createEntity(), name, tag, &getWorld()));
	return *resIt;
}

bool Scene::destroyObject(std::shared_ptr<ECS::Object> p_target) {
	auto found
		= std::ranges::find_if(objects.begin(), objects.end(), [p_target](std::shared_ptr<ECS::Object> element) { return element == p_target; });

	if (found != objects.end()) {
		if (auto renderer = p_target->getComponent<ECS::ModelRenderer>()) {
			removeFromBVH(renderer.get());
		}
		objects.erase(found);
		// External references must not keep removed scene components or physics/render resources active.
		p_target->dispose();
		return true;
	}
	else {
		return false;
	}
}

std::shared_ptr<IKIGAI::ECS::Object> Scene::findObjectByName(const std::string& p_name) {
	auto result = std::ranges::find_if(
		objects.begin(), objects.end(), [p_name](std::shared_ptr<ECS::Object>& element) { return element->getName() == p_name; });

	if (result != objects.end()) {
		return *result;
	}
	else {
		return nullptr;
	}
}

std::shared_ptr<IKIGAI::ECS::Object> Scene::findObjectByTag(const std::string& p_tag) {
	auto result = std::ranges::find_if(
		objects.begin(), objects.end(), [p_tag](std::shared_ptr<ECS::Object>& element) { return (*element).getTag() == p_tag; });

	if (result != objects.end()) {
		return *result;
	}
	else {
		return nullptr;
	}
}

std::shared_ptr<IKIGAI::ECS::Object> Scene::findObjectByID(ECS::Entity p_id) {
	auto result
		= std::ranges::find_if(objects.begin(), objects.end(), [p_id](std::shared_ptr<ECS::Object>& element) { return element->getID() == p_id; });

	if (result != objects.end()) {
		return *result;
	}
	else {
		return nullptr;
	}
}

std::vector<std::shared_ptr<IKIGAI::ECS::Object>> Scene::findObjectsByName(const std::string& p_name) {
	std::vector<std::shared_ptr<IKIGAI::ECS::Object>> actors;
	for (const auto& actor : objects) {
		if (actor->getName() == p_name) {
			actors.push_back(actor);
		}
	}
	return actors;
}

std::vector<std::shared_ptr<IKIGAI::ECS::Object>> Scene::findObjectsByTag(const std::string& p_tag) {
	std::vector<std::shared_ptr<IKIGAI::ECS::Object>> actors;
	for (const auto& actor : objects) {
		if (actor->getTag() == p_tag) {
			actors.push_back(actor);
		}
	}
	return actors;
}

std::span<std::shared_ptr<IKIGAI::ECS::Object>> Scene::getObjects() {
	return objects;
}

// TODO: void Scene::setMainCamera(Entity id) {}

void Scene::commitRenderTransforms() {
	for (auto& component : getWorld().getComponentManager()->getComponentsArray<ECS::TransformComponent>()) {
		auto& transform = component.getTransform();
		transform.setPrevWorldMatrix(transform.getWorldMatrix());
	}
}

IKIGAI::UTILS::WeakPtr<IKIGAI::ECS::CameraComponent> Scene::findMainCamera() {
	auto cm = getWorld().getComponentManager();
	for (auto& camera : cm->getComponentsArray<ECS::CameraComponent>()) {
		if (camera.obj->getIsActive()) {
			if (camera.obj->getName().rfind("__", 0) == 0) { // TODO: refactor VR component and remove it
				continue;
			}
			return camera.getWeak<ECS::CameraComponent>();
			;
		}
	}
	for (auto& camera : cm->getComponentsArray<ECS::VrCameraComponent>()) {
		if (camera.obj->getIsActive()) {
			return camera.getWeak<ECS::CameraComponent>();
		}
	}
	return nullptr;
}

std::vector<IKIGAI::RENDER::LightOGL> Scene::findLightData(std::vector<int>* objectIds) const {
	std::vector<RENDER::LightOGL> result;
	if (objectIds) {
		objectIds->clear();
	}
	auto cm = getWorld().getComponentManager();
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

std::vector<IKIGAI::RENDER::LightOGL> Scene::findLightDataInFrustum(const RENDER::Frustum& p_frustum, std::vector<int>* objectIds) {
	std::vector<RENDER::LightOGL> result;
	if (objectIds) {
		objectIds->clear();
	}

	auto cm = getWorld().getComponentManager();
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

std::vector<std::shared_ptr<RENDER::MeshInterface>> Scene::getMeshesInFrustum(const RENDER::ModelInterface& model,
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
void MapChildrenToChunk(IKIGAI::ECS::Object* obj, size_t chunkIdx, std::unordered_map<IKIGAI::ECS::Entity, size_t>& map,
	const std::unordered_map<IKIGAI::ECS::Entity, size_t>& rtMap) {
	for (auto child : obj->getChildren()) {
		if (rtMap.contains(child->getID())) {
			continue; // child has its own RenderTargetComponent, skip it and its children
		}
		map[child->getID()] = chunkIdx;
		MapChildrenToChunk(child.get(), chunkIdx, map, rtMap);
	}
}

struct ChunkContext {
	std::vector<IKIGAI::RENDER::RenderChunk> chunks;
	std::unordered_map<IKIGAI::ECS::Entity, size_t> objToChunk;
	std::unordered_map<const ECS::Transform*, MATH::Matrix4f> previousWorlds {};

	const MATH::Matrix4f& previousWorld(ECS::Transform& transform) {
		// Each material batch must see the same history even when another batch already advanced the transform.
		return previousWorlds.try_emplace(&transform, transform.getPrevWorldMatrix()).first->second;
	}

	void init(ECS::World& world) {
		chunks.push_back({ nullptr, nullptr }); // Main chunk (index 0)

		auto cm = world.getComponentManager();
		std::unordered_map<IKIGAI::ECS::Entity, size_t> rtMap;

		for (auto& rt : cm->getComponentsArray<IKIGAI::ECS::RenderTargetComponent>()) {
			if (!rt.obj->getIsActive()) {
				continue;
			}

			chunks.push_back({ rt.getFrameBuffer(), rt.getCamera() });
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
void AppendSurfaceDrawables(Scene& scene, ChunkContext& ctx, const MATH::Vector3f& cameraPosition, const RENDER::Frustum* frustum,
	const std::shared_ptr<RENDER::MaterialInterface>& defaultMaterial) {
	auto cm = scene.getWorld().getComponentManager();
	for (auto& modelRenderer : cm->getComponentsArray<ECS::ModelLODRenderer>()) {
		if (modelRenderer.obj->isAlive() && modelRenderer.obj->getIsActive() && modelRenderer.obj->getIsVisible()) {
			size_t chunkIdx = ctx.getChunkIdxForObj(modelRenderer.obj->getID());
			auto& chunk = ctx.getChunk(chunkIdx);
			const auto* localFrustum = SurfaceView(chunk, frustum);

			MATH::Vector3f localCameraPos = cameraPosition;
			if (chunk.camera) {
				localCameraPos = chunk.camera->obj->mTransform->getWorldPosition();
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

std::vector<IKIGAI::RENDER::RenderChunk> Scene::findAndSortFrustumCulledBVHDrawables(
	const MATH::Vector3f& cameraPosition, const RENDER::Frustum& frustum, std::shared_ptr<RENDER::MaterialInterface> defaultMaterial) {
	ChunkContext ctx;
	ctx.init(getWorld());

	std::vector<std::shared_ptr<BVHData>> toDraw;
	spatial().meshesTree.query(RENDER::frustumPredicate(frustum), [&](const auto& payload) { toDraw.push_back(payload); });

	for (const auto& meshData : toDraw) {
		auto owner = meshData->obj;

		if (owner->getIsVisible() && owner->getIsActive()) {
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
					localCameraPos = chunk.camera->obj->mTransform->getWorldPosition();
				}

				float distanceToActor = MATH::Vector3f::Distance(transform.getWorldPosition(), localCameraPos);
				const ECS::MaterialRenderer::MaterialList& materials = materialRenderer->getMaterials();

				std::shared_ptr<RENDER::MaterialInterface> material;
				if (meshData->mesh->getMaterialIndex() < MAX_MATERIAL_COUNT) {
					material = materials.at(meshData->mesh->getMaterialIndex());
					if (!material || (!material->getShader() && !material->isDeferred())) {
						material = defaultMaterial;
					}
				}

				if (material) {
					RENDER::Drawable element = { ctx.previousWorld(transform), transform.getWorldMatrix(), meshData->mesh, material, skeleton,
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

	AppendSurfaceDrawables(*this, ctx, cameraPosition, &frustum, defaultMaterial);
	return std::move(ctx.chunks);
}

std::vector<IKIGAI::RENDER::RenderChunk> Scene::findAndSortFrustumCulledDrawables(
	const MATH::Vector3f& cameraPosition, const RENDER::Frustum& frustum, std::shared_ptr<RENDER::MaterialInterface> defaultMaterial) {
	ChunkContext ctx;
	ctx.init(getWorld());

	auto cm = getWorld().getComponentManager();
	for (const auto& modelRenderer : cm->getComponentsArray<ECS::ModelRenderer>()) {
		auto owner = modelRenderer.obj;

		if (owner->getIsVisible() && owner->getIsActive()) {
			if (auto model = modelRenderer.getModel()) {
				if (auto materialRenderer = modelRenderer.obj.get().getComponent<ECS::MaterialRenderer>()) {
					auto& transform = owner->getTransform()->getTransform();
					auto skeletonComp = modelRenderer.obj.get().getComponent<ECS::SkeletalComponent>();
					auto animComp = modelRenderer.obj.get().getComponent<ECS::SkeletalAnimationComponent>();
					std::shared_ptr<SKELETON::Skeleton> skeleton = skeletonComp ? skeletonComp->getSkeleton() : nullptr;
					SKELETON::IAnimationPlayable* animationPlayable
						= skeletonComp ? skeletonComp->finalPlayable(animComp ? animComp->getPlayable() : nullptr) : nullptr;
					SKELETON::AnimOffset* animOffset = skeletonComp ? skeletonComp->mAnimOffset.get() : nullptr;
					SKELETON::AnimLocalTransform* animLocalTransform = skeletonComp ? skeletonComp->mAnimLocalTransform.get() : nullptr;
					SKELETON::AnimGlobalTransform* animGlobalTransform = skeletonComp ? skeletonComp->mAnimGlobalTransform.get() : nullptr;

					RENDER::CullingOptions cullingOptions = RENDER::CullingOptions::NONE;

					if (modelRenderer.getFrustumBehaviour() != ECS::EFrustumBehaviour::DISABLED) {
						cullingOptions |= RENDER::CullingOptions::FRUSTUM_PER_MODEL;
					}

					if (modelRenderer.getFrustumBehaviour() == ECS::EFrustumBehaviour::CULL_MESHES) {
						cullingOptions |= RENDER::CullingOptions::FRUSTUM_PER_MESH;
					}

					const auto& modelBoundingSphere = modelRenderer.getFrustumBehaviour() == ECS::EFrustumBehaviour::CULL_CUSTOM
						? modelRenderer.getCustomBoundingSphere()
						: model->getBoundingSphere();

					auto meshes = (skeletonComp && skeletonComp->finalPlayable(nullptr)) || DeformationPadding(*materialRenderer) > 0
						? model->getMeshes()
						: getMeshesInFrustum(*model, modelBoundingSphere, transform, frustum, cullingOptions);

					if (!meshes.empty()) {
						size_t chunkIdx = ctx.getChunkIdxForObj(owner->getID());
						auto& chunk = ctx.getChunk(chunkIdx);

						MATH::Vector3f localCameraPos = cameraPosition;
						if (chunk.camera) {
							localCameraPos = chunk.camera->obj->mTransform->getWorldPosition();
						}

						float distanceToActor = MATH::Vector3f::Distance(transform.getWorldPosition(), localCameraPos);
						const ECS::MaterialRenderer::MaterialList& materials = materialRenderer->getMaterials();

						for (const auto mesh : meshes) {
							std::shared_ptr<RENDER::MaterialInterface> material;
							if (mesh->getMaterialIndex() < MAX_MATERIAL_COUNT) {
								material = materials.at(mesh->getMaterialIndex());
								if (!material || (!material->getShader() && !material->isDeferred())) {
									material = defaultMaterial;
								}
							}

							if (material) {
								RENDER::Drawable element
									= { ctx.previousWorld(transform), transform.getWorldMatrix(), mesh, material, skeleton, animationPlayable,
										  animOffset, animLocalTransform, animGlobalTransform, static_cast<int>(owner->getID().getUniqueId()) };
								if (auto facial = owner->getComponent<ECS::FacialAnimationComponent>()) {
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
	}

	AppendSurfaceDrawables(*this, ctx, cameraPosition, &frustum, defaultMaterial);
	return std::move(ctx.chunks);
}

std::vector<IKIGAI::RENDER::RenderChunk> Scene::findAndSortDrawables(
	const MATH::Vector3f& cameraPosition, std::shared_ptr<RENDER::MaterialInterface> defaultMaterial) {
	ChunkContext ctx;
	ctx.init(getWorld());

	auto cm = getWorld().getComponentManager();
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
						localCameraPos = chunk.camera->obj->mTransform->getWorldPosition();
					}

					float distanceToActor = MATH::Vector3f::Distance(transform.getWorldPosition(), localCameraPos);

					const ECS::MaterialRenderer::MaterialList& materials = materialRenderer->getMaterials();

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

	AppendSurfaceDrawables(*this, ctx, cameraPosition, nullptr, defaultMaterial);
	return std::move(ctx.chunks);
}

std::vector<IKIGAI::RENDER::RenderChunk> Scene::findDrawables(const MATH::Vector3f& cameraPosition, const RENDER::Camera& camera,
	const RENDER::Frustum* customFrustum, std::shared_ptr<RENDER::MaterialInterface> defaultMaterial) {
	auto cullingProfile = PROFILER::Profiler::IsEnabled() ? std::make_unique<PROFILER::ProfilerSpy>("Scene::findDrawables") : nullptr;
	if (camera.isFrustumGeometryBVHCulling()) {
		const auto& frustum = customFrustum ? *customFrustum : camera.getFrustum();
		return findAndSortFrustumCulledBVHDrawables(cameraPosition, frustum, defaultMaterial);
	}
	else if (camera.isFrustumGeometryCulling()) {
		const auto& frustum = customFrustum ? *customFrustum : camera.getFrustum();
		return findAndSortFrustumCulledDrawables(cameraPosition, frustum, defaultMaterial);
	}
	else {
		return findAndSortDrawables(cameraPosition, defaultMaterial);
	}
}
