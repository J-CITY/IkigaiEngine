#include "object.h"

#include "componentCatalog.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/log/loggerDefine.h"

#include <algorithm>
#include <execution>

using namespace IKIGAI;
using namespace IKIGAI::ECS;

EVENT::Event<Object&> Object::destroyedEvent;
EVENT::Event<Object&> Object::createdEvent;
EVENT::Event<Object&, Object&> Object::attachEvent;
EVENT::Event<Object&> Object::dettachEvent;

// Object::Object() : id(-1) {
//	transform = addComponent<TransformComponent>();
//	//createdEvent.run(*this);
// }

Object::Descriptor Object::getDescriptor() const {
	Descriptor d;
	d.Name = getName();
	d.Tag = getTag();
	d.Id = getIDInt();
	d.ParentId = getParentId();
	d.IsActive = mIsActive;
	d.IsVisible = mIsVisible;
	std::vector<std::pair<std::string, nlohmann::json>> components;
	const auto& catalog = ComponentCatalog::instance();
	for (const auto& [type, handle] : mComponents) {
		const auto* component = resolveStoredComponent(handle);
		if (!component) {
			continue;
		}
		const auto* record = catalog.findByType(type);
		if (!record) {
			continue;
		}
		components.emplace_back(record->name, record->write(*component));
	}
	std::sort(components.begin(), components.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
	for (auto& component : components) {
		d.Components.push_back(std::move(component.second));
	}
	return d;
}

Object::Object(ECS::Entity actorID, const std::string& name, const std::string& tag, ECS::World* world)
	: mWorld(world ? world : &RESOURCES::ServiceManager::Get<ECS::World>())
	, mEntity(actorID)
	, mName(name)
	, mTag(tag) {
	addComponent<TransformComponent>();
	createdEvent.run(*this);
}

// TODO: set mParent? when call this
Object::Object(const Descriptor& _descriptor, ECS::World* world, bool deferred)
	: mWorld(world ? world : &RESOURCES::ServiceManager::Get<ECS::World>())
	, mDeferred(deferred)
	, mEntity(ECS::Entity::ID(_descriptor.Id))
	, mName(_descriptor.Name)
	, mTag(_descriptor.Tag) {
	setActive(_descriptor.IsActive);
	setVisible(_descriptor.IsVisible);
	getWorld().registerEntity(mEntity);
	for (const auto& component : _descriptor.Components) {
		ComponentCatalog::instance().read(*this, component);
	}
}

Object::~Object() {
	dispose();
}

std::shared_ptr<Object> Object::sharedFromThisOrNull() {
	try {
		return weak_from_this().lock();
	} catch (const std::bad_weak_ptr&) {
		return nullptr;
	}
}

void Object::dispose() {
	if (mDisposed || mDisposing) {
		return;
	}
	if (CommandBuffer* commands = activeCommandBuffer()) {
		const Entity entity = mEntity;
		if (std::shared_ptr<Object> self = sharedFromThisOrNull()) {
			// A failed ECS batch may discard this command.
			commands->addCommand([self, entity]() { self->disposeImmediate(entity); });
			return;
		}
	}
	disposeImmediate(mEntity);
}

void Object::disposeImmediate(Entity entity) {
	if (mDisposed) {
		return;
	}
	mDisposing = true;
	// Callbacks still need to read this object's components during disposal.
	if (mIsActive) {
		onDisable();
	}
	if (mIsReady) {
		onDestroy();
	}
#if USE_IKIGAI_SCRIPT
	else if (auto script = getComponent<IkigaiScriptComponent>()) {
		// An inactive loaded object can be awake without ever reaching OnStart.
		script->onDestroy();
	}
#endif

	destroyedEvent.run(*this);

	std::vector<std::shared_ptr<Object>> toDetach = mChildren;
	for (auto child : toDetach) {
		child->detachFromParent();
	}
	toDetach.clear();

	detachFromParent();
	for (const auto& [type, handle] : mComponents) {
		if (auto* component = resolveStoredComponent(handle)) {
			componentRemovedEvent.run(component);
		}
	}
	getWorld().getComponentManager()->destroyEntity(entity);
	mComponents.clear();
	mTransform = {};
	mDisposed = true;
	mIsDestroyed = true;
	mIsActive = false;
	mDisposing = false;
}

const std::string& Object::getName() const {
	return mName;
}

const std::string& Object::getTag() const {
	return mTag;
}

void Object::setName(const std::string& val) {
	mName = val;
}

void Object::setTag(const std::string& val) {
	mTag = val;
}

void Object::addSerializedComponent(const nlohmann::json& descriptor) {
	ComponentCatalog::instance().read(*this, descriptor);
}

void Object::activateLoaded() {
	mDeferred = false;
#if USE_LUA
	if (auto script = getComponent<ScriptComponent>()) {
		ScriptComponentEvents::createdEvent.run(script);
	}
#endif
	for (const auto& [type, handle] : mComponents) {
		if (auto* component = resolveStoredComponent(handle)) {
			component->onAwake();
		}
	}
	if (getIsActive()) {
		onEnable();
		onStart();
	}
}

void Object::setActive(bool val) {
	if (val != mIsActive) {
		mIsActive = val;
		recursiveActiveUpdate();
	}
}

bool Object::getIsSelfActive() const {
	return mIsActive;
}

bool Object::getIsActive() const {
	auto p = mParent.lock();
	return mIsActive && (p ? p->getIsActive() : true);
}

void Object::setVisible(bool val) {
	mIsVisible = val;
}

bool Object::getIsSelfVisible() const {
	return mIsVisible;
}

bool Object::getIsVisible() const {
	auto p = mParent.lock();
	return mIsVisible && (p ? p->getIsVisible() : true);
}

void Object::setID(ECS::Entity val) {
	mEntity = val;
}

ECS::Entity Object::getID() const {
	return mEntity;
}

void Object::setParent(std::shared_ptr<Object> _mParent) {
	detachFromParent();

	mParent = _mParent;
	getComponent<TransformComponent>()->setParent(*_mParent->getComponent<TransformComponent>().get());

	_mParent->mChildren.push_back(shared_from_this());
	attachEvent.run(*this, *_mParent);
}

void Object::setParentInPos(std::shared_ptr<Object> _mParent, int pos) {
	if (pos > _mParent->mChildren.size()) {
		// TODO: problem
		return;
	}
	detachFromParent();

	mParent = _mParent;
	getComponent<TransformComponent>()->setParent(*_mParent->getComponent<TransformComponent>().get());

	_mParent->mChildren.insert(_mParent->mChildren.begin() + pos, shared_from_this());
	attachEvent.run(*this, *_mParent);
}

void Object::detachFromParent() {
	dettachEvent.run(*this);

	if (auto p = mParent.lock()) {
		std::erase_if(p->mChildren, [this](const std::shared_ptr<Object>& child) { return child.get() == this; });
	}
	mParent.reset();
	if (auto transform = getComponent<TransformComponent>()) {
		transform->removeParent();
	}
}

bool Object::hasParent() const {
	return mParent.lock() != nullptr;
}

std::shared_ptr<Object> Object::getParent() const {
	return mParent.lock();
}

ECS::Entity Object::getParentID() const {
	if (auto p = mParent.lock()) {
		return p->getID();
	}
	return ECS::Entity(ECS::Entity::ID(0));
}

std::span<std::shared_ptr<Object>> Object::getChildren() {
	return mChildren;
}

void Object::markAsDestroy() {
	mIsDestroyed = true;
	for (auto child : mChildren) {
		child->markAsDestroy();
	}
}

bool Object::isAlive() const {
	return !mIsDestroyed;
}

void Object::onStart() {
	mIsReady = true;
	for (const auto& [type, handle] : mComponents) {
		if (auto* component = resolveStoredComponent(handle)) {
			component->onStart();
		}
	}
}

void Object::onEnable() {
	for (const auto& [type, handle] : mComponents) {
		if (auto* component = resolveStoredComponent(handle)) {
			component->onEnable();
		}
	}
}

void Object::onDisable() {
	for (const auto& [type, handle] : mComponents) {
		if (auto* component = resolveStoredComponent(handle)) {
			component->onDisable();
		}
	}
}

void Object::onDestroy() {
	for (const auto& [type, handle] : mComponents) {
		if (auto* component = resolveStoredComponent(handle)) {
			component->onDestroy();
		}
	}
}

void Object::onUpdate(std::chrono::duration<double> dt) {
	if (getIsActive()) {
		for (const auto& [type, handle] : mComponents) {
			if (auto* component = resolveStoredComponent(handle)) {
				component->onUpdate(dt);
			}
		}
	}
}

void Object::onFixedUpdate(std::chrono::duration<double> dt) {
	if (getIsActive()) {
		for (const auto& [type, handle] : mComponents) {
			if (auto* component = resolveStoredComponent(handle)) {
				component->onFixedUpdate(dt);
			}
		}
	}
}

void Object::onLateUpdate(std::chrono::duration<double> dt) {
	if (getIsActive()) {
		for (const auto& [type, handle] : mComponents) {
			if (auto* component = resolveStoredComponent(handle)) {
				component->onLateUpdate(dt);
			}
		}
	}
}

void Object::recursiveActiveUpdate() {
	if (mDeferred) {
		return;
	}
	bool mIsActive = getIsActive();

	if (mIsActive) {
		onEnable();
		if (!mIsReady) {
			onStart();
		}
	}

	if (!mIsActive) {
		onDisable();
	}

	for (auto child : mChildren) {
		child->recursiveActiveUpdate();
	}
}

ComponentBase* Object::resolveStoredComponent(const ComponentHandle<Component>& handle) const {
	return static_cast<ComponentBase*>(getWorld().getComponentManager()->resolveComponent(handle));
}

std::map<std::type_index, UTILS::WeakPtr<ComponentBase>> Object::getComponents() const {
	std::map<std::type_index, UTILS::WeakPtr<ComponentBase>> components;
	if (!mDisposed) {
		for (const auto& [type, handle] : mComponents) {
			components.try_emplace(type, resolveStoredComponent(handle));
		}
	}
	return components;
}

UTILS::WeakPtr<TransformComponent> Object::getTransform() const {
	return UTILS::WeakPtr<TransformComponent>(tryGetTransform());
}

TransformComponent* Object::tryGetTransform() const {
	return mDisposed ? nullptr : getWorld().getComponentManager()->resolveComponent(mTransform);
}

int Object::getIDInt() const {
	return static_cast<int>(mEntity.getUniqueId());
}

void Object::setIDInt(int id) {
	mEntity = ECS::Entity(ECS::Entity::ID(id));
}

int Object::getParentId() const {
	auto _p = mParent.lock();
	if (!_p) {
		return -1;
	}
	return _p->getIDInt();
}

void Object::setParentId(int _id) {
	if (_id < 0) {
		return;
	}
	auto& scene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
	auto _p = scene.findObjectByID(ECS::Entity::ID(_id));
	if (_p) {
		LOG_ERROR << ("Object::setParentId: can not find actor with id: " + std::to_string(_id));
	}
	setParent(_p);
}
