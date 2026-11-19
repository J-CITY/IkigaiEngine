#pragma once

namespace IKIGAI::UTILS {
template <typename T> class Singleton {
	explicit Singleton() = default;

public:
	inline static T& GetInstance() {
		static T instance {};
		return instance;
	}

	Singleton(const Singleton&) = delete;
	Singleton& operator=(const Singleton&) = delete;
	Singleton(Singleton&&) = default;
	Singleton& operator=(Singleton&&) = default;
	virtual ~Singleton() = default;
};

}

#define FRIEND_SINGLETON(S) friend IKIGAI::UTILS::Singleton<S>;
