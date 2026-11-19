#pragma once

#include <any>
#include <map>
#include <mutex>
#include <typeindex>

namespace IKIGAI::UTILS {

using ServiceType = std::any;

// Service owners keep registered objects alive while consumers can access them.
class ServiceManager {
public:
	template <class T> static void Set(T* service) {
		std::lock_guard<std::mutex> lock(MUTEX);
		SERVICES[std::type_index(typeid(T))] = service;
	}

	template <class T> static T& Get() {
		std::lock_guard<std::mutex> lock(MUTEX);
		return *std::any_cast<T*>(SERVICES.at(std::type_index(typeid(T))));
	}

	template <class T> static bool Check() {
		std::lock_guard<std::mutex> lock(MUTEX);
		return SERVICES.contains(std::type_index(typeid(T)));
	}

	template <class T> static void Remove() {
		std::lock_guard<std::mutex> lock(MUTEX);
		SERVICES.erase(std::type_index(typeid(T)));
	}

private:
	static std::mutex MUTEX;
	static std::map<std::type_index, ServiceType> SERVICES;
};

}

// Existing callers and the utils API share the same service registry.
namespace IKIGAI::RESOURCES {
using UTILS::ServiceManager;
using UTILS::ServiceType;
}
