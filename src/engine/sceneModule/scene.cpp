#include "scene.h"
#include "sceneObjectDescriptor.h"
#include "traceModule/trace.h"
#include <algorithm>
#include <cstdint>
#include <span>

#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/object.h"
#include "resourceModule/jsonLoader.h"
#include "utilsModule/serviceManager.h"
#include "utilsModule/log/loggerDefine.h"
#include <stdexcept>
#include <unordered_set>
#include <utility>

using namespace IKIGAI;
using namespace IKIGAI::SCENE_SYSTEM;

Scene::Scene(ECS::World* world, std::unique_ptr<SceneSimulation> simulation)
	: mSimulation(std::move(simulation))
	, mWorld(world) { }

SceneSimulation& Scene::requireSimulation() {
	if (!mSimulation) {
		throw std::logic_error("Scene has no engine simulation");
	}
	return *mSimulation;
}

void Scene::setPhysicsFactories(PhysicsFactories physics) {
	requireSimulation().setPhysicsFactories(std::move(physics));
}

#if USE_PHYSICS_3D
PHYSICS::PhysicWorld& Scene::physics3d() {
	return requireSimulation().physics3d(*this);
}
std::shared_ptr<PHYSICS::PhysicWorld> Scene::physics3dShared() {
	return requireSimulation().physics3dShared(*this);
}
void Scene::stepPhysics3D(std::chrono::duration<double> dt, bool beginDestructionFrame) {
	requireSimulation().stepPhysics3D(*this, dt, beginDestructionFrame);
}
bool Scene::physics3dPaused() const {
	return mSimulation && mSimulation->physics3dPaused();
}
void Scene::setPhysics3dPaused(bool paused) {
	requireSimulation().setPhysics3dPaused(*this, paused);
}
void Scene::presentDestructibles(float alpha) {
	if (mSimulation) {
		mSimulation->presentDestructibles(*this, alpha);
	}
}
#endif

#if USE_PHYSICS_2D
PHYSICS2D::World& Scene::physics2d() {
	return requireSimulation().physics2d(*this);
}
void Scene::stepPhysics2D(std::chrono::duration<double> dt) {
	requireSimulation().stepPhysics2D(*this, dt);
}
#endif

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
	for (const auto& object : objects) {
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
	if (mSimulation) {
		mSimulation->afterStart(*this);
	}
}

bool Scene::getIsExecute() const {
	return isExecute;
}

void Scene::beginSimulationFrame() {
	if (mSimulation) {
		mSimulation->beginFrame(*this);
	}
}

void Scene::simulateFixedStep(std::chrono::duration<double> dt) {
	IKIGAI_PROFILE_SCOPE("Fixed Simulation Step");
	fixedUpdate(dt);
	getWorld().getSystemManager()->runFixedUpdate(dt);
	if (mSimulation) {
#if USE_PHYSICS_3D
		mSimulation->stepPhysics3D(*this, dt, false);
#endif
#if USE_PHYSICS_2D
		mSimulation->stepPhysics2D(*this, dt);
#endif
	}
	finishFixedUpdate(dt);
}

void Scene::simulateFrame(std::chrono::duration<double> dt, [[maybe_unused]] float interpolationAlpha) {
	IKIGAI_PROFILE_SCOPE("Frame Simulation");
	auto* systems = getWorld().getSystemManager();
	systems->runUpdate(dt);
	update(dt);
#if USE_PHYSICS_3D
	presentDestructibles(interpolationAlpha);
#endif
	// Object lifecycle dispatch owns component callbacks, including LogicComponent.
	lateUpdate(dt);
	systems->runLateUpdate(dt);
}

void Scene::update(std::chrono::duration<double> dt) {
	if (mSimulation) {
		mSimulation->beforeObjectUpdate(*this, dt);
	}
	for (auto& object : objects) {
		object->onUpdate(dt);
	}
	if (mSimulation) {
		mSimulation->afterObjectUpdate(*this, dt);
	}
}

void Scene::fixedUpdate(std::chrono::duration<double> dt) {
	for (auto& o : objects) {
		o->onFixedUpdate(dt);
	}
}
void Scene::finishFixedUpdate(std::chrono::duration<double> dt) {
	if (mSimulation) {
		mSimulation->finishFixedUpdate(*this, dt);
	}
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
	observeComponents(*instance);
	if (isExecute) {
		instance->setActive(true);
		if (instance->getIsActive()) {
			instance->onEnable();
			instance->onStart();
		}
	}

	return instance;
}

std::shared_ptr<IKIGAI::ECS::Object> Scene::createObject(const ECS::Object::Descriptor& data) {
	objects.push_back(MEMORY::MakeShared<ECS::Object, MEMORY::Category::Node>(data, &getWorld()));
	auto& instance = objects.back();
	observeComponents(*instance);
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
	observeComponents(*instance);
	if (isExecute) {
		instance->setActive(true);
		if (instance->getIsActive()) {
			instance->onEnable();
			instance->onStart();
		}
	}
	return instance;
}

std::weak_ptr<Scene::ComponentChanges> Scene::componentChanges() const {
	return mComponentChanges;
}

void Scene::notifyComponentChange(ComponentChange change, UTILS::WeakPtr<ECS::ComponentBase> component) {
	mComponentChanges->run(change, component);
}

void Scene::observeComponents(ECS::Object& object) {
	auto relay = [changes = componentChanges()](ComponentChange change) {
		return [changes, change](UTILS::WeakPtr<ECS::ComponentBase> component) {
			if (auto source = changes.lock()) {
				source->run(change, component);
			}
		};
	};
	object.componentAddedEvent.add(relay(ComponentChange::Added));
	object.componentRemovedEvent.add(relay(ComponentChange::Removed));
	object.componentChangedEvent.add(relay(ComponentChange::Updated));
}

void Scene::postLoad() {
	for (const auto& object : objects) {
		postLoadObject(*object);
	}
	isSceneReady = true;
}

void Scene::postLoadObject(ECS::Object& object) {
	for (const auto& [type, component] : object.getComponents()) {
		notifyComponentChange(ComponentChange::Added, component);
	}
}

std::shared_ptr<ECS::Object> Scene::createDeferredObject(const ECS::Object::Descriptor& descriptor) {
	auto shell = descriptor;
	shell.Components.clear();
	shell.IsActive = false;
	objects.push_back(MEMORY::MakeShared<ECS::Object, MEMORY::Category::Node>(shell, &getWorld(), true));
	observeComponents(*objects.back());
	return objects.back();
}

void Scene::removeLastObject() {
	if (objects.empty()) {
		return;
	}
	objects.back()->detachFromParent();
	objects.back()->dispose();
	objects.pop_back();
}

const DOCUMENT::Instance* Scene::prefabFor(int objectId) const {
	for (const auto& instance : prefabInstances) {
		if (DOCUMENT::OwnsId(instance.entry, objectId)) {
			return &instance;
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
		const auto owned = DOCUMENT::OwnedIds(instance.entry);
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
		descriptors.push_back(ReadSceneObjectDescriptor(value));
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
				const auto ids = DOCUMENT::OwnedIds(instance.entry);
				for (auto value : serializeObjects()) {
					const int id = DOCUMENT::Id(value.at("Id"));
					if (ids.contains(id)) {
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
	observeComponents(**resIt);
	return *resIt;
}

std::shared_ptr<ECS::Object> Scene::createObjectBefore(ECS::Object::Id parentId, const std::string& name, const std::string& tag) {
	const auto iter = std::ranges::find_if(objects, [parentId](auto& e) { return e->getID() == parentId; });
	auto resIt = objects.insert(iter, MEMORY::MakeShared<ECS::Object, MEMORY::Category::Node>(getWorld().createEntity(), name, tag, &getWorld()));
	observeComponents(**resIt);
	return *resIt;
}

bool Scene::destroyObject(std::shared_ptr<ECS::Object> p_target) {
	auto found
		= std::ranges::find_if(objects.begin(), objects.end(), [p_target](std::shared_ptr<ECS::Object> element) { return element == p_target; });

	if (found != objects.end()) {
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
