#pragma once
#include <any>
#include <functional>
#include <map>
#include <mutex>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "idGenerator.h"

namespace IKIGAI::EVENT {
template <class... ArgTypes> class Event : public IdGenerator<Event<>> {
public:
	Event() = default;
	using Callback = std::function<void(ArgTypes...)>;

	ID add(Callback p_callback) {
		ID listenerID = generateId();
		mCallbacks.emplace(listenerID, p_callback);
		return listenerID;
	}
	bool removeListener(ID listenerID) {
		return mCallbacks.erase(listenerID) != 0;
	}
	void removeAllListeners() {
		mCallbacks.clear();
	}
	size_t getListenerCount() {
		return mCallbacks.size();
	}
	void run(ArgTypes... p_args) {
		for (auto const& [key, value] : mCallbacks) {
			value(p_args...);
		}
	}

private:
	std::map<ID, Callback> mCallbacks;
};

using EventListener = Id<Event<>>;

class EventBroadcaster : public IdGenerator<EventBroadcaster> {
public:
	using Payload = std::unordered_map<std::string, std::any>;
	using Handler = std::function<void(std::string_view msg, const Payload&)>;

private:
	std::mutex m;

	std::vector<std::pair<std::string, Payload>> mDeferredEventsMsgs;
	std::unordered_map<std::string, std::unordered_map<ID, Handler>> mEventMap;

public:
	enum class ExecutionPolicy { IMMEDIATELY, DEFERRED };

	ID add(std::string_view msg, Handler cb) {
		std::lock_guard lock(m);
		const ID eventId = generateId();
		mEventMap[std::string(msg)][eventId] = cb;
		return eventId;
	}

	void remove(std::string_view msg) {
		std::lock_guard lock(m);
		mEventMap.erase(std::string(msg));
	}

	void remove(std::string_view msg, ID cbId) {
		std::lock_guard lock(m);
		const auto _msg = std::string(msg);
		if (!mEventMap.contains(_msg)) {
			return;
		}
		mEventMap[_msg].erase(cbId);
	}

	void removeAll() {
		std::lock_guard lock(m);
		mEventMap.clear();
	}

	void run(const std::string_view msg, const Payload& payload, ExecutionPolicy policy = ExecutionPolicy::IMMEDIATELY) {
		std::vector<Handler> handlers;
		const auto message = std::string(msg);
		{
			std::lock_guard lock(m);
			const auto found = mEventMap.find(message);
			if (found == mEventMap.end()) {
				return;
			}
			if (policy != ExecutionPolicy::IMMEDIATELY) {
				mDeferredEventsMsgs.push_back({ message, payload });
				return;
			}
			handlers.reserve(found->second.size());
			for (const auto& callback : found->second | std::views::values) {
				handlers.push_back(callback);
			}
		}
		for (const auto& callback : handlers) {
			callback(message, payload);
		}
	}

	void runDeferred() {
		struct Pending {
			std::string message;
			Payload payload;
			std::vector<Handler> handlers;
		};
		std::vector<Pending> pending;
		{
			std::lock_guard lock(m);
			pending.reserve(mDeferredEventsMsgs.size());
			for (auto& entry : mDeferredEventsMsgs) {
				Pending item;
				item.message = std::move(entry.first);
				item.payload = std::move(entry.second);
				if (const auto found = mEventMap.find(item.message); found != mEventMap.end()) {
					item.handlers.reserve(found->second.size());
					for (const auto& callback : found->second | std::views::values) {
						item.handlers.push_back(callback);
					}
				}
				pending.push_back(std::move(item));
			}
			mDeferredEventsMsgs.clear();
		}
		for (const auto& item : pending) {
			for (const auto& callback : item.handlers) {
				callback(item.message, item.payload);
			}
		}
	}
};
}
