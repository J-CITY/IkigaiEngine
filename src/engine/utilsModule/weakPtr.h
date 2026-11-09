#pragma once
#include <atomic>
#include <functional>
#include <memory>

namespace IKIGAI::ECS {
class Component;
template <class T, template <class, class> class CONT, unsigned ChunkSize> class ComponentArray;
}

namespace IKIGAI::UTILS {
class ControlBlockHandler;

class ControlBlock {
public:
	explicit ControlBlock(ControlBlockHandler* ptr)
		: mPtr(ptr) { }
	std::atomic_size_t mRC = 1;
	std::atomic<ControlBlockHandler*> mPtr;
};

class ControlBlockHandler {
	template <class T, template <class, class> class CONT, unsigned ChunkSize> friend class ECS::ComponentArray;

	std::atomic<ControlBlock*> mCb = nullptr;
	void swapControlBlocks(ControlBlockHandler& other) noexcept;
	void invalidateWeakReferences() noexcept;

public:
	ControlBlockHandler() noexcept = default;
	// Value construction creates a distinct identity. Assignment retains the destination identity.
	ControlBlockHandler(const ControlBlockHandler&) noexcept;
	ControlBlockHandler& operator=(const ControlBlockHandler&) noexcept;
	ControlBlockHandler(ControlBlockHandler&&) noexcept;
	ControlBlockHandler& operator=(ControlBlockHandler&&) noexcept;
	virtual ~ControlBlockHandler();
	virtual ControlBlock* getControlBlock();
};

template <typename Ptr, bool = std::is_class<Ptr>::value> struct has_smart_pointer_ops {
	using false_test = char;
	template <typename T> struct true_test {
		false_test dummy[2];
	};

	template <typename T> static false_test test_op_star(...);
	template <typename T> static true_test<decltype(*std::declval<T const&>())> test_op_star(T*);

	template <typename T> static false_test test_op_arrow(...);
	template <typename T> static true_test<decltype(std::declval<T const&>().operator->())> test_op_arrow(T*);

	template <typename T> static false_test test_get(...);
	template <typename T> static true_test<decltype(std::declval<T const&>().get())> test_get(T*);

	static constexpr bool value = !std::is_same<decltype(test_get<Ptr>(0)), false_test>::value
		&& !std::is_same<decltype(test_op_arrow<Ptr>(0)), false_test>::value && !std::is_same<decltype(test_op_star<Ptr>(0)), false_test>::value;
};

/// Non-class types can't be smart pointers
template <typename Ptr> struct has_smart_pointer_ops<Ptr, false> : std::false_type { };

/// Ensure that the smart pointer operations give consistent return
/// types
template <typename Ptr>
struct smart_pointer_ops_consistent
	: std::integral_constant<bool,
		  std::is_pointer<decltype(std::declval<Ptr const&>().get())>::value && std::is_reference<decltype(*std::declval<Ptr const&>())>::value
			  && std::is_pointer<decltype(std::declval<Ptr const&>().operator->())>::value
			  && std::is_same<decltype(std::declval<Ptr const&>().get()), decltype(std::declval<Ptr const&>().operator->())>::value
			  && std::is_same<decltype(*std::declval<Ptr const&>().get()), decltype(*std::declval<Ptr const&>())>::value> { };

/// Assume Ptr is a smart pointer if it has the relevant ops and they
/// are consistent
template <typename Ptr, bool = has_smart_pointer_ops<Ptr>::value>
struct is_smart_pointer : std::integral_constant<bool, smart_pointer_ops_consistent<Ptr>::value> { };

/// If Ptr doesn't have the relevant ops then it can't be a smart
/// pointer
template <typename Ptr> struct is_smart_pointer<Ptr, false> : std::false_type { };

/// Check if Ptr is a smart pointer that holds a pointer convertible to
/// T*
template <typename Ptr, typename T, bool = is_smart_pointer<Ptr>::value>
struct is_convertible_smart_pointer : std::integral_constant<bool, std::is_convertible<decltype(std::declval<Ptr const&>().get()), T*>::value> { };

/// If Ptr isn't a smart pointer then we don't want it
template <typename Ptr, typename T> struct is_convertible_smart_pointer<Ptr, T, false> : std::false_type { };

template <typename T>
concept ComponentT = requires(T t) { std::is_base_of_v<ECS::Component, decltype(t)>; };

// A weak reference tracks identity across ECS relocation. Dereference requires the storage to stay live.
template <ComponentT T> class WeakPtr {
	template <ComponentT U> friend class WeakPtr;

	ControlBlock* mCb = nullptr;

	void retain() {
		if (mCb) {
			mCb->mRC.fetch_add(1, std::memory_order_relaxed);
		}
	}

public:
	using element_type = std::remove_extent_t<T>;
	WeakPtr() = default;

	constexpr WeakPtr(nullptr_t) noexcept { }

	explicit WeakPtr(ControlBlockHandler& component) {
		mCb = component.getControlBlock();
		retain();
	}

	explicit WeakPtr(T* component) {
		if (!component) {
			return;
		}
		mCb = component->getControlBlock();
		retain();
	}

	~WeakPtr() {
		cleanup();
	}

	WeakPtr(const WeakPtr& obj) {
		mCb = obj.mCb;
		retain();
	}

	template <class T1> explicit WeakPtr(const WeakPtr& obj, T1*) {
		mCb = obj.mCb;
		retain();
	}

	template <class T1> explicit WeakPtr(WeakPtr&& obj, T1*) noexcept {
		mCb = obj.mCb;
		obj.mCb = nullptr;
	}

	template <typename U, typename = std::enable_if_t<std::is_convertible<U*, T*>::value>> WeakPtr(U* ptr_) {
		if (!ptr_) {
			return;
		}
		mCb = ptr_->getControlBlock();
		retain();
	}

	template <typename U, typename = std::enable_if_t<std::is_convertible<U*, T*>::value>> WeakPtr(const WeakPtr<U>& other) noexcept {
		mCb = other.mCb;
		retain();
	}

	WeakPtr& operator=(const WeakPtr& obj) {
		if (this == &obj) {
			return *this;
		}
		cleanup();
		mCb = obj.mCb;
		retain();
		return *this;
	}

	WeakPtr& operator=(nullptr_t) noexcept {
		cleanup();
		mCb = nullptr;
		return *this;
	}

	WeakPtr(WeakPtr&& obj) noexcept {
		mCb = obj.mCb;
		obj.mCb = nullptr;
	}

	WeakPtr& operator=(WeakPtr&& obj) noexcept {
		if (this == &obj) {
			return *this;
		}
		cleanup();
		mCb = obj.mCb;
		obj.mCb = nullptr;
		return *this;
	}

	bool operator==(const WeakPtr& other) const noexcept {
		return currentPointer() == other.currentPointer();
	}

	explicit operator bool() const {
		return isAlive();
	}

	[[nodiscard]] bool isAlive() const noexcept {
		return currentPointer() != nullptr;
	}

	T& operator*() const {
		return *alivePointer();
	}

	T* operator->() const {
		return alivePointer();
	}

	T* get() {
		return alivePointer();
	}

	const T* get() const {
		return alivePointer();
	}

private:
	friend struct std::hash<WeakPtr<T>>;

	ControlBlockHandler* currentPointer() const noexcept {
		return mCb ? mCb->mPtr.load(std::memory_order_acquire) : nullptr;
	}

	T* alivePointer() const {
		auto* ptr = currentPointer();
		if (!ptr) {
			throw std::bad_weak_ptr();
		}
		return static_cast<T*>(ptr);
	}

	void cleanup() const {
		if (!mCb) {
			return;
		}
		if (mCb->mRC.fetch_sub(1, std::memory_order_acq_rel) == 1) {
			delete mCb;
		}
	}
};
}

namespace std {
template <typename T> struct hash<IKIGAI::UTILS::WeakPtr<T>> {
	size_t operator()(IKIGAI::UTILS::WeakPtr<T> const& p) const noexcept {
		auto* ptr = static_cast<T*>(p.currentPointer());
		return ptr ? hash<T*>()(ptr) : 0;
	}
};

template <typename To, typename From>
typename std::enable_if<sizeof(decltype(static_cast<To*>(std::declval<From*>()))) != 0, IKIGAI::UTILS::WeakPtr<To>>::type static_pointer_cast(
	IKIGAI::UTILS::WeakPtr<From> p) {
	return IKIGAI::UTILS::WeakPtr(static_cast<To*>(p.get()));
}

template <typename To, typename From>
typename std::enable_if<sizeof(decltype(dynamic_cast<To*>(std::declval<From*>()))) != 0, IKIGAI::UTILS::WeakPtr<To>>::type dynamic_pointer_cast(
	IKIGAI::UTILS::WeakPtr<From> p) {
	return IKIGAI::UTILS::WeakPtr(dynamic_cast<To*>(p.get()));
}

}
