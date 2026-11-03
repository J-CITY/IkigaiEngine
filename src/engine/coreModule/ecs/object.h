#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <typeindex>
#include <vector>

#include "componentTypes.h"
#include "components/scriptComponent.h"
#include "components/transform.h"
#include "ecsModule/commandBuffer.h"
#include "ecsModule/world.h"
#include "utilsModule/serialization/jsonValueAdaptor.h"
#include <scriptModule/scriptInterpreter.h>
#include <utilsModule/event.h>

namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}

namespace IKIGAI::ECS {
class Object : public std::enable_shared_from_this<Object> {
public:
	struct Descriptor {
		std::string Name;
		std::string Tag;
		int Id;
		int ParentId;
		bool IsActive = false;
		bool IsVisible = true;
		std::vector<nlohmann::json> Components;

		template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
			using Self = Descriptor;
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Self::Name, "Name")
				.field(&Self::Tag, "Tag", default_ { "" })
				.field(&Self::Id, "Id")
				.field(&Self::ParentId, "ParentId", default_ { -1 })
				.field(&Self::IsActive, "IsActive", default_ { true })
				.field(&Self::IsVisible, "IsVisible", default_ { true })
				.field(&Self::Components, "Components");
		}
	};

	Descriptor getDescriptor() const;

	// using Id_ = Id<Object>;
	using Id = ECS::Entity;
	using ObjPtr = std::shared_ptr<Object>;

	Object(ECS::Entity actorID, const std::string& name, const std::string& tag, ECS::World* world = nullptr);
	explicit Object(const Descriptor& actorID, ECS::World* world = nullptr, bool deferred = false);
	ECS::World& getWorld() const {
		return *mWorld;
	}
	void addSerializedComponent(const nlohmann::json& descriptor);
	void setPendingActive(bool active) {
		mIsActive = active;
	}
	void activateLoaded();
	~Object();
	void dispose();

	[[nodiscard]] const std::string& getName() const;
	[[nodiscard]] const std::string& getTag() const;

	void setName(const std::string& name);
	void setTag(const std::string& tag);
	void setActive(bool val);
	void setVisible(bool val);

	[[nodiscard]] bool getIsSelfActive() const;
	[[nodiscard]] bool getIsActive() const;

	[[nodiscard]] bool getIsSelfVisible() const;
	[[nodiscard]] bool getIsVisible() const;

	void setID(ECS::Entity id);
	[[nodiscard]] ECS::Entity getID() const;

	void setParent(ObjPtr p_parent);
	void setParentInPos(std::shared_ptr<Object> _parent, int pos);

	void detachFromParent();

	[[nodiscard]] bool hasParent() const;
	[[nodiscard]] ObjPtr getParent() const;
	[[nodiscard]] ECS::Entity getParentID() const;
	std::span<ObjPtr> getChildren();

	void markAsDestroy();

	bool isAlive() const;

	void onStart();
	void onEnable();
	void onDisable();
	void onDestroy();

	void onUpdate(std::chrono::duration<double> dt);
	void onFixedUpdate(std::chrono::duration<double> dt);
	void onLateUpdate(std::chrono::duration<double> dt);

	template <typename T, typename... Args> inline UTILS::WeakPtr<T> addComponent(Args&&... args) {
		if (mDisposed || mDisposing) {
			throw std::logic_error("Cannot add a component to a disposed object");
		}
		static_assert(std::is_base_of_v<ComponentBase, T>, "T should derive from Component");

		if (auto found = getComponent<T>(); !found) {
			auto instance = T(*this, args...);
			if (!mDeferred && getIsActive()) {
				// TODO: call systems methods
				instance.onAwake();
				instance.onEnable();
				instance.onStart();
			}
			getWorld().getComponentManager()->addComponent<T>(getID(), std::move(instance));

			auto component = getComponent<T>();
			componentAddedEvent.run(component);
			// TODO: move it to ScriptComponent::onStart()
#if USE_LUA

			if constexpr (std::is_same_v<T, ScriptComponent>) {
				if (!mDeferred) {
					ScriptComponentEvents::createdEvent.run(component);
				}
			}
#endif
			mComponents[std::type_index(typeid(T))] = component;
			return component;
		}
		else {
			return found;
		}
	}

	template <typename T> inline bool removeComponent() {
		static_assert(std::is_base_of_v<ComponentBase, T>, "T should derive from Component");
		if constexpr (std::is_same<T, TransformComponent>::value) {
			return false;
		}
		if (CommandBuffer* commands = activeCommandBuffer()) {
			if (!getComponent<T>()) {
				return false;
			}
			const Entity entity = mEntity;
			const std::type_index type = typeid(T);
			if (std::shared_ptr<Object> self = sharedFromThisOrNull()) {
				commands->addCommand([self, entity, type]() { self->template removeComponentNow<T>(entity, type); });
				return true;
			}
		}
		return removeComponentNow<T>(mEntity, std::type_index(typeid(T)));
	}

	template <typename T> inline UTILS::WeakPtr<T> getComponent() const {
		if (mDisposed) {
			return nullptr;
		}
		static_assert(std::is_base_of_v<ComponentBase, T>, "T should derive from Component");

		if (getWorld().getComponentManager()->checkComponent<T>(getID())) {
			return getWorld().getComponentManager()->getComponent<T>(getID());
		}
		return nullptr;
	}

	template <typename T> inline UTILS::WeakPtr<ComponentBase> getComponentBase() const {
		if (mDisposed) {
			return nullptr;
		}
		static_assert(std::is_base_of_v<ComponentBase, T>, "T should derive from Component");

		if (getWorld().getComponentManager()->checkComponent<T>(getID())) {
			return getWorld().getComponentManager()->getComponentBase<T>(getID());
		}

		return nullptr;
	}

	auto getComponents() const {
		return mComponents;
	}

	[[nodiscard]] UTILS::WeakPtr<TransformComponent> getTransform() const;

	// ObjectData getObjectData();

private:
	void recursiveActiveUpdate();
	void disposeImmediate(Entity entity);
	std::shared_ptr<Object> sharedFromThisOrNull();

	template <typename T> bool removeComponentNow(Entity entity, std::type_index type) {
		static_assert(std::is_base_of_v<ComponentBase, T>, "T should derive from Component");
		if constexpr (std::is_same<T, TransformComponent>::value) {
			return false;
		}

		auto result = getComponent<T>();
		if (!result) {
			return false;
		}
		// TODO: move it to ScriptComponent::onStart()
#if USE_LUA

		if constexpr (std::is_same<T, ScriptComponent>::value) {
			ScriptComponentEvents::destroyedEvent.run(result);
		}
#endif
		// Removal must run lifecycle callbacks while the component and its
		// dependencies are still alive (e.g. an animator playback lease).
		if (getIsActive()) {
			result->onDisable();
		}
		result->onDestroy();
		componentRemovedEvent.run(result);
		getWorld().getComponentManager()->removeComponents<T>(entity);
		mComponents.erase(type);
		return true;
	}

public:
	EVENT::Event<UTILS::WeakPtr<ComponentBase>> componentAddedEvent;
	EVENT::Event<UTILS::WeakPtr<ComponentBase>> componentRemovedEvent;
	EVENT::Event<UTILS::WeakPtr<ComponentBase>> componentChangedEvent;

	static EVENT::Event<Object&> destroyedEvent;
	static EVENT::Event<Object&> createdEvent;
	static EVENT::Event<Object&, Object&> attachEvent;
	static EVENT::Event<Object&> dettachEvent;

	// TODO: think how do it better
	EVENT::Event<> setModelEvent;

	// for serealization
	int getIDInt() const;
	void setIDInt(int _id);
	int getParentId() const;
	void setParentId(int _id);

protected:
	ECS::World* mWorld = nullptr;
	bool mDeferred = false;
	bool mDisposed = false;
	bool mDisposing = false;
	// Id_ id;
	ECS::Entity mEntity;
	std::string mName;
	std::string mTag;

	bool mIsActive = true;
	bool mIsVisible = true;
	bool mIsDestroyed = false;
	bool mIsReady = false;

	std::weak_ptr<Object> mParent;
	std::vector<ObjPtr> mChildren;
	std::map<std::type_index, UTILS::WeakPtr<ComponentBase>> mComponents;

public:
	UTILS::WeakPtr<TransformComponent> mTransform;
};
};
