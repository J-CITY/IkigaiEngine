#pragma once
#include "entityManager.h"
#include <typeindex>
#include <utilsModule/event.h>

namespace IKIGAI::ECS {

template <typename T> struct OnAdd {
	Entity entity;
	T* component;
};

template <typename T> struct OnRemove {
	Entity entity;
	T* component;
};

template <typename T> struct OnSet {
	Entity entity;
	T* component;
};

class EventManager {
	// Map type_index of Event -> Signal
	// But utilsModule/event.h is Event<ArgTypes...>.
	// We want generic event type E.
	// So we map type_index -> std::any (wrapping Event<const E&>)
	// Or we can simple have a template method to access static event.

public:
	template <typename E> using EventSignal = IKIGAI::EVENT::Event<const E&>;

	template <typename E> [[nodiscard]] EventSignal<E>* findSignal() {
		if (mEvents.empty()) {
			return nullptr;
		}
		const auto found = mEvents.find(std::type_index(typeid(E)));
		return found == mEvents.end() ? nullptr : static_cast<EventSignal<E>*>(found->second.get());
	}

	template <typename E> EventSignal<E>& getSignal() {
		// Static variable per event type - simple and effective for global event manager.
		// But ComponentManager is per World. We want per-World events.
		// So we need member storage.
		// Type erasure or virtual base?
		// Since we know E at call site, we can use std::any or void* map.
		auto type = std::type_index(typeid(E));
		if (!mEvents.contains(type)) {
			mEvents[type] = std::make_shared<EventSignal<E>>();
		}
		return *static_cast<EventSignal<E>*>(mEvents[type].get());
	}

	template <typename E> void emit(const E& event) {
		getSignal<E>().run(event);
	}

	template <typename E> IKIGAI::EVENT::EventListener subscribe(std::function<void(const E&)> callback) {
		return getSignal<E>().add(callback);
	}

	template <typename E> void unsubscribe(IKIGAI::EVENT::EventListener id) {
		getSignal<E>().removeListener(id);
	}

private:
	std::unordered_map<std::type_index, std::shared_ptr<void>> mEvents;
};
}
