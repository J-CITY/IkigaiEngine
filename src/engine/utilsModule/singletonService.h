#pragma once

#include "serviceManager.h"

#include <mutex>

namespace IKIGAI::UTILS {

template <typename T> class SingletonService {
protected:
	explicit SingletonService() = default;
	inline static std::once_flag mFlag;

public:
	inline static T& GetInstance() {
		if (!ServiceManager::Check<T>()) {
			std::call_once(mFlag, []() { ServiceManager::Set<T>(new T()); });
		}
		return ServiceManager::Get<T>();
	}

	SingletonService(const SingletonService&) = delete;
	SingletonService& operator=(const SingletonService&) = delete;
	SingletonService(SingletonService&&) = default;
	SingletonService& operator=(SingletonService&&) = default;
	virtual ~SingletonService() {
		ServiceManager::Remove<T>();
	}
};

}

#define FRIEND_SINGLETON_SERVICE(S) friend IKIGAI::UTILS::SingletonService<S>;
