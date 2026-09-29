#pragma once
#include <any>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <variant>
#include <vector>
#include <stdexcept>

#include "mathModule/math.h"

namespace IKIGAI::UTILS {
template <std::size_t size> struct fnv1a_traits;

template <> struct fnv1a_traits<4> {
	using type = std::uint32_t;
	static constexpr std::uint32_t offset = 2166136261;
	static constexpr std::uint32_t prime = 16777619;
};

template <> struct fnv1a_traits<8> {
	using type = std::uint64_t;
	static constexpr std::uint64_t offset = 14695981039346656037ull;
	static constexpr std::uint64_t prime = 1099511628211ull;
};

constexpr std::size_t string_hash_seed(std::size_t seed, const char *str,
																			 std::size_t N) noexcept {
	using Traits = fnv1a_traits<sizeof(std::size_t)>;
	std::size_t value = seed;

	for (std::size_t i = 0; i < N; i++)
		value = (value ^ static_cast<Traits::type>(str[i])) * Traits::prime;

	return value;
}

constexpr std::size_t tring_hash_seed(std::size_t seed,
																			const char *curr) noexcept {
	using Traits = fnv1a_traits<sizeof(std::size_t)>;
	std::size_t value = seed;

	while (*curr) {
		value = (value ^ static_cast<Traits::type>(*(curr++))) * Traits::prime;
	}

	return value;
}

constexpr std::size_t string_hash(const char *str, std::size_t N) noexcept {
	using Traits = fnv1a_traits<sizeof(std::size_t)>;
	return string_hash_seed(Traits::offset, str, N);
}

constexpr std::size_t string_hash(std::string_view str) noexcept {
	return string_hash(str.data(), str.size());
}

using TypeName = std::string;
using TypeId = size_t;

// Custom Any implementation to support Move-Only types
class Any {
public:
	Any() = default;

	template <typename T,
						typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Any>>>
	Any(T &&value)
			: mStorage(
						std::make_unique<Model<std::decay_t<T>>>(std::forward<T>(value))) {}

	Any(const Any &other) {
		if (other.mStorage)
			mStorage = other.mStorage->clone();
	}

	Any(Any &&other) noexcept = default;
	Any &operator=(Any &&other) noexcept = default;

	Any &operator=(const Any &other) {
		if (this != &other) {
			if (other.mStorage)
				mStorage = other.mStorage->clone();
			else
				mStorage.reset();
		}
		return *this;
	}

	bool has_value() const { return mStorage != nullptr; }
	const std::type_info &type() const {
		return mStorage ? mStorage->type() : typeid(void);
	}

	template <typename T> friend T *any_cast(Any *operand);

	template <typename T> friend const T *any_cast(const Any *operand);

private:
	struct Base {
		virtual ~Base() = default;
		virtual std::unique_ptr<Base> clone() const = 0;
		virtual const std::type_info &type() const noexcept = 0;
	};

	template <typename T> struct Model final : Base {
		T data;
		explicit Model(T &&val) : data(std::move(val)) {}
		explicit Model(const T &val) : data(val) {}

		std::unique_ptr<Base> clone() const override {
			if constexpr (std::is_copy_constructible_v<T>) {
				return std::make_unique<Model<T>>(data);
			} else {
				throw std::runtime_error(
						"Attempted to copy Any containing a move-only type");
			}
		}

		const std::type_info &type() const noexcept override { return typeid(T); }
	};

	std::unique_ptr<Base> mStorage;
};

template <typename T> T *any_cast(Any *operand) {
	if (operand && operand->mStorage && operand->mStorage->type() == typeid(T)) {
		return &static_cast<Any::Model<T> *>(operand->mStorage.get())->data;
	}
	return nullptr;
}

template <typename T> const T *any_cast(const Any *operand) {
	if (operand && operand->mStorage && operand->mStorage->type() == typeid(T)) {
		return &static_cast<const Any::Model<T> *>(operand->mStorage.get())->data;
	}
	return nullptr;
}

template <typename T> T any_cast(Any &operand) {
	using U = std::remove_cv_t<std::remove_reference_t<T>>;
	auto *ptr = any_cast<U>(&operand);
	if (!ptr)
		throw std::bad_cast();
	return static_cast<T>(*ptr);
}

template <typename T> T any_cast(const Any &operand) {
	using U = std::remove_cv_t<std::remove_reference_t<T>>;
	auto *ptr = any_cast<U>(&operand);
	if (!ptr)
		throw std::bad_cast();
	return static_cast<const U &>(*ptr);
}

template <typename T> T any_cast(Any &&operand) {
	using U = std::remove_cv_t<std::remove_reference_t<T>>;
	auto *ptr = any_cast<U>(&operand);
	if (!ptr)
		throw std::bad_cast();
	return static_cast<T>(std::move(*ptr));
}

using MethodName = std::string;
using FieldName = std::string;

// Flexible Metadata System
using MetaDataId = size_t;
using MetaValue = Any;

class Metadata {
	std::unordered_map<MetaDataId, Any> mMap;

public:
	Metadata() = default;

	// Initialize from string keys (hashes them automatically)
	Metadata(std::initializer_list<std::pair<std::string_view, Any>> init) {
		for (auto &[k, v] : init) {
			mMap[string_hash(k)] = std::move(v);
		}
	}

	// Initialize from pre-hashed IDs (e.g. constants)
	Metadata(std::initializer_list<std::pair<MetaDataId, Any>> init) {
		for (auto &[k, v] : init) {
			mMap[k] = std::move(v);
		}
	}

	void add(std::string_view key, Any value) {
		mMap[string_hash(key)] = std::move(value);
	}

	void add(MetaDataId key, Any value) { mMap[key] = std::move(value); }

	template <typename T> T get(MetaDataId key, T defaultValue = {}) const {
		auto it = mMap.find(key);
		if (it == mMap.end())
			return defaultValue;
		if (auto *p = any_cast<T>(&it->second))
			return *p;
		return defaultValue;
	}

	template <typename T> T get(std::string_view key, T defaultValue = {}) const {
		return get<T>(string_hash(key), defaultValue);
	}

	bool contains(MetaDataId key) const { return mMap.contains(key); }

	bool contains(std::string_view key) const {
		return mMap.contains(string_hash(key));
	}

	const std::unordered_map<MetaDataId, Any> &getMap() const { return mMap; }
};

class MethodInfo;
class FieldInfo;
class ConstructorInfo;

class FieldInfo {
public:
	std::string name;
	TypeId typeId;
	Metadata metadata;
	bool isStatic = false;
	bool isConst = false;

	// Type-erased accessors
	std::function<void(void *, Any)> setter;
	std::function<Any(void *)> getter;

	template <typename T> void set(void *instance, T value) const {
		if (setter)
			setter(instance, std::move(value));
	}

	template <typename T> T get(void *instance) const {
		if (!getter)
			return T{};
		return any_cast<T>(getter(instance));
	}
};

class MethodInfo {
public:
	std::string name;
	TypeId returnType;
	std::vector<TypeId> argumentTypes;
	Metadata metadata;
	bool isStatic = false;
	bool isConst = false;

	using Invoker = std::function<Any(void *instance, const std::vector<Any> &args)>;
	Invoker invoke;
};

class ConstructorInfo {
public:
	std::vector<TypeId> argumentTypes;
	Metadata metadata;

	using Creator = std::function<Any(const std::vector<Any> &args)>;
	Creator invoke;
};

class EnumInfo {
public:
	std::string name;
	TypeId typeId;
	std::unordered_map<std::string, int> nameToValue;
	std::unordered_map<int, std::string> valueToName;
};

class TypeInfo {
public:
	TypeName mName;
	TypeId mId;

	std::unordered_map<FieldName, FieldInfo> mFields;
	std::unordered_map<MethodName, MethodInfo> mMethods;
	std::vector<ConstructorInfo> mConstructors;
};

namespace DETAILS {
// Helper to invoke function with vector of Any
template <typename Func, typename Tuple, size_t... I>
auto call_helper(Func &&f, const std::vector<Any> &args,
								 std::index_sequence<I...>) {
	return f(any_cast<typename std::tuple_element<I, Tuple>::type>(args[I])...);
}

template <typename R, typename... Args>
Any invoke_method(std::function<R(Args...)> func,
									const std::vector<Any> &args) {
	if (args.size() != sizeof...(Args))
		throw std::runtime_error("Argument count mismatch");
	if constexpr (std::is_void_v<R>) {
		call_helper<decltype(func), std::tuple<Args...>>(
				std::move(func), args, std::index_sequence_for<Args...>{});
		return {};
	} else {
		return call_helper<decltype(func), std::tuple<Args...>>(
				std::move(func), args, std::index_sequence_for<Args...>{});
	}
}

// For member functions
template <class C, typename R, typename... Args>
Any invoke_member(R (C::*func)(Args...), void *instance,
									const std::vector<Any> &args) {
	if (args.size() != sizeof...(Args))
		throw std::runtime_error("Argument count mismatch");
	auto *obj = static_cast<C *>(instance);
	auto wrapper = [obj, func](Args... params) -> R {
		return (obj->*func)(params...);
	};

	if constexpr (std::is_void_v<R>) {
		call_helper<decltype(wrapper), std::tuple<Args...>>(
				std::move(wrapper), args, std::index_sequence_for<Args...>{});
		return {};
	} else {
		return call_helper<decltype(wrapper), std::tuple<Args...>>(
				std::move(wrapper), args, std::index_sequence_for<Args...>{});
	}
}

// Const member functions
template <class C, typename R, typename... Args>
Any invoke_member_const(R (C::*func)(Args...) const, void *instance,
												const std::vector<Any> &args) {
	if (args.size() != sizeof...(Args))
		throw std::runtime_error("Argument count mismatch");
	auto *obj = static_cast<C *>(instance);
	auto wrapper = [obj, func](Args... params) -> R {
		return (obj->*func)(params...);
	};

	if constexpr (std::is_void_v<R>) {
		call_helper<decltype(wrapper), std::tuple<Args...>>(
				std::move(wrapper), args, std::index_sequence_for<Args...>{});
		return {};
	} else {
		return call_helper<decltype(wrapper), std::tuple<Args...>>(
				std::move(wrapper), args, std::index_sequence_for<Args...>{});
	}
}
} // namespace DETAILS

class ReflectionManager {
	std::unordered_map<TypeId, TypeInfo> mTypes;
	std::unordered_map<TypeId, EnumInfo> mEnums;
	std::unordered_map<TypeId, TypeId> mTypeAliases;
	std::unordered_map<std::string, std::vector<TypeInfo*>> mTypeGroups;
	TypeInfo mGlobalScope;
	std::unordered_map<std::string, Any> mInternalProperties;
	std::unordered_map<std::string, FieldInfo> mGlobalProperties;

public:
	static ReflectionManager &Instance() {
		static ReflectionManager instance;
		return instance;
	}

	const std::unordered_map<std::string, FieldInfo>& getGlobalProperties() const {
			return mGlobalProperties;
	}

	const std::unordered_map<TypeId, TypeInfo>& getTypes() const {
		return mTypes;
	}

	void addTypeToGroup(TypeId id, const std::string& groupName) {
		if (auto* typeInfo = getType(id)) {
			mTypeGroups[groupName].push_back(typeInfo);
		}
	}

	template<class CLASS> 
	void addTypeToGroup(const std::string& groupName) {
		addTypeToGroup(GetTypeId<CLASS>(), groupName);
	}

	const std::vector<TypeInfo*>& getTypesInGroup(const std::string& groupName) const {
		static std::vector<TypeInfo*> empty;
		auto it = mTypeGroups.find(groupName);
		if (it != mTypeGroups.end()) {
			return it->second;
		}
		return empty;
	}

	template <typename T> static constexpr TypeId GetTypeId() {
		return string_hash(NAMEOF_TYPE(T));
	}

	template <class CLASS> void registerType() {
		auto newType = NAMEOF_TYPE(CLASS);
		auto newId = string_hash(newType);
		if (mTypes.find(newId) == mTypes.end()) {
			mTypes[newId] = TypeInfo{std::string(newType), newId};
		}

		// RTTI alias
		std::string_view rttiName = typeid(CLASS).name();
		auto rttiId = string_hash(rttiName);
		if (rttiId != newId) {
			mTypeAliases[rttiId] = newId;
		}

		// Short name alias
		auto lastColon = std::string_view(newType).find_last_of(':');
		if (lastColon != std::string_view::npos) {
			auto shortName = std::string_view(newType).substr(lastColon + 1);
			auto shortId = string_hash(shortName);
			if (shortId != newId) {
				mTypeAliases[shortId] = newId;
			}
		}
	}

	template <class CLASS> TypeInfo *getType() {
		auto id = string_hash(NAMEOF_TYPE(CLASS));
		return getType(id);
	}

	TypeInfo *getType(std::string_view typeName) {
		auto id = string_hash(typeName);
		return getType(id);
	}

	TypeInfo *getType(TypeId id) {
		auto target = mTypes.find(id);
		if (target == mTypes.end()) {
			auto aliasIt = mTypeAliases.find(id);
			if (aliasIt != mTypeAliases.end()) {
				return getType(aliasIt->second);
			}
			return nullptr;
		}
		return &target->second;
	}

	template <typename E>
	void registerEnum(const std::string& enumName, const std::vector<std::pair<std::string, int>>& values) {
		TypeId id = GetTypeId<E>();
		EnumInfo info;
		info.name = enumName;
		info.typeId = id;
		for (const auto& pair : values) {
			info.nameToValue[pair.first] = pair.second;
			info.valueToName[pair.second] = pair.first;
		}
		mEnums[id] = std::move(info);
	}

	EnumInfo *getEnum(TypeId id) {
		auto target = mEnums.find(id);
		if (target == mEnums.end()) {
			return nullptr;
		}
		return &target->second;
	}

	EnumInfo *getEnum(std::string_view enumName) {
		auto id = string_hash(enumName);
		return getEnum(id);
	}

	// --- Field Registration ---

	// 1. Direct Member Pointer
	template <typename C, typename T>
	void registerField(const std::string &fieldName, T C::*fieldPtr,
										 Metadata meta = {}) {
		TypeId typeId = GetTypeId<C>();
		TypeInfo *info = getType(typeId);
		if (!info) {
			registerType<C>();
			info = getType(typeId);
		}

		FieldInfo fieldInfo;
		fieldInfo.name = fieldName;
		fieldInfo.typeId = GetTypeId<T>();
		fieldInfo.metadata = std::move(meta);
		fieldInfo.isStatic = false;
		fieldInfo.isConst = std::is_const_v<T>;

		fieldInfo.getter = [fieldPtr](void *instance) -> Any {
			return static_cast<C *>(instance)->*fieldPtr;
		};
		fieldInfo.setter = [fieldPtr](void *instance, Any value) {
			static_cast<C *>(instance)->*fieldPtr = any_cast<T>(std::move(value));
		};

		info->mFields[fieldName] = std::move(fieldInfo);
	}

	// 2. Member Methods (Getter/Setter)
	template <typename C, typename T>
	void registerField(const std::string &fieldName, T (C::*getter)() const,
										 void (C::*setter)(T), Metadata meta = {}) {
		TypeId typeId = GetTypeId<C>();
		TypeInfo *info = getType(typeId);
		if (!info) {
			registerType<C>();
			info = getType(typeId);
		}

		FieldInfo fieldInfo;
		fieldInfo.name = fieldName;
		fieldInfo.typeId = GetTypeId<T>();
		fieldInfo.metadata = std::move(meta);
		fieldInfo.isStatic = false;
		fieldInfo.isConst = false;

		fieldInfo.getter = [getter](void *instance) -> Any {
			return (static_cast<C *>(instance)->*getter)();
		};
		fieldInfo.setter = [setter](void *instance, Any value) {
			(static_cast<C *>(instance)->*setter)(any_cast<T>(std::move(value)));
		};

		info->mFields[fieldName] = std::move(fieldInfo);
	}

	// 3. Lambdas (Getter/Setter)
	template <typename C, typename T>
	void registerField(const std::string &fieldName, std::function<T(C *)> getter,
										 std::function<void(C *, T)> setter, Metadata meta = {}) {
		TypeId typeId = GetTypeId<C>();
		TypeInfo *info = getType(typeId);
		if (!info) {
			registerType<C>();
			info = getType(typeId);
		}

		FieldInfo fieldInfo;
		fieldInfo.name = fieldName;
		fieldInfo.typeId = GetTypeId<T>();
		fieldInfo.metadata = std::move(meta);
		fieldInfo.isStatic = false;

		if (getter) {
			fieldInfo.getter = [getter](void *instance) -> Any {
				return getter(static_cast<C *>(instance));
			};
		}
		if (setter) {
			fieldInfo.setter = [setter](void *instance, Any value) {
				setter(static_cast<C *>(instance), any_cast<T>(std::move(value)));
			};
		}

		info->mFields[fieldName] = std::move(fieldInfo);
	}

	// --- Static Field Registration ---

	// 1. Pointer to Static Variable
	template <typename C, typename T>
	void registerStaticField(const std::string &fieldName, T *fieldPtr,
													 Metadata meta = {}) {
		TypeId typeId = GetTypeId<C>();
		TypeInfo *info = getType(typeId);
		if (!info) {
			registerType<C>();
			info = getType(typeId);
		}

		FieldInfo fieldInfo;
		fieldInfo.name = fieldName;
		fieldInfo.typeId = GetTypeId<T>();
		fieldInfo.metadata = std::move(meta);
		fieldInfo.isStatic = true;

		fieldInfo.getter = [fieldPtr](void *) -> Any { return *fieldPtr; };
		fieldInfo.setter = [fieldPtr](void *, Any value) {
			*fieldPtr = any_cast<T>(std::move(value));
		};

		info->mFields[fieldName] = std::move(fieldInfo);
	}

	// 2. Static Functions (Getter/Setter)
	template <typename C, typename T>
	void registerStaticField(const std::string &fieldName, T (*getter)(),
													 void (*setter)(T), Metadata meta = {}) {
		TypeId typeId = GetTypeId<C>();
		TypeInfo *info = getType(typeId);
		if (!info) {
			registerType<C>();
			info = getType(typeId);
		}

		FieldInfo fieldInfo;
		fieldInfo.name = fieldName;
		fieldInfo.typeId = GetTypeId<T>();
		fieldInfo.metadata = std::move(meta);
		fieldInfo.isStatic = true;

		fieldInfo.getter = [getter](void *) -> Any { return getter(); };
		fieldInfo.setter = [setter](void *, Any value) {
			setter(any_cast<T>(std::move(value)));
		};

		info->mFields[fieldName] = std::move(fieldInfo);
	}

	// 3. Lambdas (Static Getter/Setter)
	template <typename C, typename T>
	void registerStaticField(const std::string &fieldName,
													 std::function<T()> getter,
													 std::function<void(T)> setter, Metadata meta = {}) {
		TypeId typeId = GetTypeId<C>();
		TypeInfo *info = getType(typeId);
		if (!info) {
			registerType<C>();
			info = getType(typeId);
		}

		FieldInfo fieldInfo;
		fieldInfo.name = fieldName;
		fieldInfo.typeId = GetTypeId<T>();
		fieldInfo.metadata = std::move(meta);
		fieldInfo.isStatic = true;

		if (getter) {
			fieldInfo.getter = [getter](void *) -> Any { return getter(); };
		}
		if (setter) {
			fieldInfo.setter = [setter](void *, Any value) {
				setter(any_cast<T>(std::move(value)));
			};
		}

		info->mFields[fieldName] = std::move(fieldInfo);
	}

	// Register Global Property with Internal Storage
	template <typename T>
	void registerProperty(const std::string &fieldName, Metadata meta = {}) {
		mInternalProperties[fieldName] = T{};
		
		FieldInfo fieldInfo;
		fieldInfo.name = fieldName;
		fieldInfo.typeId = GetTypeId<T>();
		fieldInfo.metadata = std::move(meta);
		fieldInfo.isStatic = true; 

		fieldInfo.getter = [this, fieldName](void*) -> Any {
			return mInternalProperties.at(fieldName);
		};
		fieldInfo.setter = [this, fieldName](void*, Any value) {
			mInternalProperties[fieldName] = std::move(value);
		};

		mGlobalProperties[fieldName] = std::move(fieldInfo);
	}

	template <typename T>
	T& property(const std::string &name) {
		if (!mInternalProperties.contains(name)) {
				throw std::runtime_error("Property not found: " + name);
		}
		auto* ptr = any_cast<T>(&mInternalProperties[name]);
		if (!ptr) {
				throw std::runtime_error("Property type mismatch or internal error: " + name);
		}
		return *ptr;
	}

	void removeProperty(const std::string &name) {
		mInternalProperties.erase(name);
		mGlobalProperties.erase(name);
	}

	// --- Method Registration ---

	template <typename C, typename R, typename... Args>
	void registerMethod(const std::string &name, R (C::*methodPtr)(Args...),
											Metadata meta = {}) {
		TypeId typeId = GetTypeId<C>();
		TypeInfo *info = getType(typeId);
		if (!info) {
			registerType<C>();
			info = getType(typeId);
		}

		MethodInfo methodInfo;
		methodInfo.name = name;
		methodInfo.returnType = GetTypeId<R>();
		methodInfo.argumentTypes = {GetTypeId<Args>()...};
		methodInfo.metadata = std::move(meta);
		methodInfo.isStatic = false;
		methodInfo.isConst = false;

		methodInfo.invoke = [methodPtr](void *instance,
																		const std::vector<Any> &args) -> Any {
			return DETAILS::invoke_member(methodPtr, instance, args);
		};

		info->mMethods[name] = std::move(methodInfo);
	}

	template <typename C, typename R, typename... Args>
	void registerMethod(const std::string &name, R (C::*methodPtr)(Args...) const,
											Metadata meta = {}) {
		TypeId typeId = GetTypeId<C>();
		TypeInfo *info = getType(typeId);
		if (!info) {
			registerType<C>();
			info = getType(typeId);
		}

		MethodInfo methodInfo;
		methodInfo.name = name;
		methodInfo.returnType = GetTypeId<R>();
		methodInfo.argumentTypes = {GetTypeId<Args>()...};
		methodInfo.metadata = std::move(meta);
		methodInfo.isStatic = false;
		methodInfo.isConst = true;

		methodInfo.invoke = [methodPtr](void *instance,
																		const std::vector<Any> &args) -> Any {
			return DETAILS::invoke_member_const(methodPtr, instance, args);
		};

		info->mMethods[name] = std::move(methodInfo);
	}

	template <typename C, typename R, typename... Args>
	void registerStaticMethod(const std::string &name, R (*methodPtr)(Args...),
														Metadata meta = {}) {
		TypeId typeId = GetTypeId<C>();
		TypeInfo *info = getType(typeId);
		if (!info) {
			registerType<C>();
			info = getType(typeId);
		}

		MethodInfo methodInfo;
		methodInfo.name = name;
		methodInfo.returnType = GetTypeId<R>();
		methodInfo.argumentTypes = {GetTypeId<Args>()...};
		methodInfo.metadata = std::move(meta);
		methodInfo.isStatic = true;

		auto func = [methodPtr](Args... args) -> R { return methodPtr(args...); };

		methodInfo.invoke = [func](void *, const std::vector<Any> &args) -> Any {
			return DETAILS::invoke_method(std::function<R(Args...)>(func), args);
		};

		info->mMethods[name] = std::move(methodInfo);
	}

	// --- Global Registration ---

	template <typename T>
	void registerGlobalVariable(const std::string &name, T *varPtr,
															Metadata meta = {}) {
		FieldInfo fieldInfo;
		fieldInfo.name = name;
		fieldInfo.typeId = GetTypeId<T>();
		fieldInfo.metadata = std::move(meta);
		fieldInfo.isStatic = true;

		fieldInfo.getter = [varPtr](void *) -> Any { return *varPtr; };
		fieldInfo.setter = [varPtr](void *, Any value) {
			*varPtr = any_cast<T>(std::move(value));
		};

		mGlobalScope.mFields[name] = std::move(fieldInfo);
	}

	template <typename R, typename... Args>
	void registerGlobalFunction(const std::string &name, R (*funcPtr)(Args...),
															Metadata meta = {}) {
		MethodInfo methodInfo;
		methodInfo.name = name;
		methodInfo.returnType = GetTypeId<R>();
		methodInfo.argumentTypes = {GetTypeId<Args>()...};
		methodInfo.metadata = std::move(meta);
		methodInfo.isStatic = true;

		auto func = [funcPtr](Args... args) -> R { return funcPtr(args...); };

		methodInfo.invoke = [func](void *, const std::vector<Any> &args) -> Any {
			return DETAILS::invoke_method(std::function<R(Args...)>(func), args);
		};

		mGlobalScope.mMethods[name] = std::move(methodInfo);
	}

	TypeInfo &getGlobalScope() { return mGlobalScope; }
};
} // namespace IKIGAI::UTILS

namespace IKIGAI::UTILS {

enum class MetaParam {
	FLAGS,
	EDIT_RANGE,
	EDIT_STEP,
	EDIT_WIDGET,
	FILE_EXTENSION,
};

enum class MetaInfo {
	NONE = 0,
	USE_IN_EDITOR_ANIMATION = 1,
	USE_IN_EDITOR_COMPONENT_INSPECTOR = 1 << 1,
};

enum class WidgetType : uint32_t {
	NONE,
	DRAG_INT,
	DRAG_FLOAT,
	DRAG_FLOAT_3,
	DRAG_FLOAT_4,
	DRAG_COLOR_3,
	DRAG_COLOR_4,
	COMBO,
	BOOL,
	STRING,
	STRING_WITH_FILE_CHOOSE,
	STRINGS_ARRAY,
	OPT_STRING,

	MATERIAL,
	MODEL_LOD
};

using Meta_t =
		std::map<MetaParam,
						 std::variant<bool, int, float, std::string, UTILS::MetaInfo,
													UTILS::WidgetType, MATH::Vector2f>>;

template <class CLASS_T, class MEMBER_T> struct MemberInfo {
	using Class_t = CLASS_T;
	using Data_t = std::remove_reference<MEMBER_T>::type;

	MemberInfo(std::string_view name, Data_t Class_t::*memberPtr,
						 Meta_t meta = Meta_t())
			: mName(name), mPtr(memberPtr), mMetadata(meta) {
		mGetter = [](const MemberInfo *_this, Class_t &obj) -> MEMBER_T {
			return obj.*(_this->mPtr);
		};
		mSetter = [](const MemberInfo *_this, Class_t &obj, Data_t data) {
			obj.*(_this->mPtr) = data;
		};
	}

	MemberInfo(std::string_view name, MEMBER_T (Class_t::*memberGetPtr)() const,
						 void (Class_t::*memberSetPtr)(MEMBER_T), Meta_t meta = Meta_t())
			: mName(name), mPtrGet(memberGetPtr), mPtrSet(memberSetPtr),
				mMetadata(meta) {
		mGetter = [](const MemberInfo *_this, Class_t &obj) -> MEMBER_T {
			return (obj.*(_this->mPtrGet))();
		};
		mSetter = [](const MemberInfo *_this, Class_t &obj, Data_t data) {
			(obj.*(_this->mPtrSet))(data);
		};
	}

	MemberInfo(std::string_view name,
						 std::function<MEMBER_T(Class_t &)> memberGetPtr,
						 std::function<void(Class_t &, MEMBER_T)> memberSetPtr,
						 Meta_t meta = Meta_t())
			: mName(name), mPtrGetLambda(memberGetPtr), mPtrSetLambda(memberSetPtr),
				mMetadata(meta) {
		mGetter = [](const MemberInfo *_this, Class_t &obj) -> MEMBER_T {
			return _this->mPtrGetLambda(obj);
		};
		mSetter = [](const MemberInfo *_this, Class_t &obj, Data_t data) {
			_this->mPtrSetLambda(obj, data);
		};
	}

	MEMBER_T get(Class_t &obj) const { return mGetter(this, obj); }
	void set(Class_t &obj, MEMBER_T data) const { mSetter(this, obj, data); }

	[[nodiscard]] const std::string &getName() const { return mName; }

	const Meta_t &getMetadata() { return mMetadata; };

	Data_t getPropType() { return Data_t(); }

private:
	const std::string mName;

	Data_t Class_t::*mPtr = nullptr;
	MEMBER_T (Class_t::*mPtrGet)() const = nullptr;
	void (Class_t::*mPtrSet)(MEMBER_T) = nullptr;
	std::function<MEMBER_T(Class_t &)> mPtrGetLambda;
	std::function<void(Class_t &, MEMBER_T)> mPtrSetLambda;

	std::function<MEMBER_T(const MemberInfo *, Class_t &)> mGetter;
	std::function<void(const MemberInfo *, Class_t &, MEMBER_T)> mSetter;
	Meta_t mMetadata;
};

template <class CLASS, class PTR>
MemberInfo<CLASS, PTR> MakeMemberInfo(std::string_view name, PTR CLASS::*ptr) {
	return MemberInfo<CLASS, PTR>(name, ptr);
}

template <class CLASS, class PTR>
MemberInfo<CLASS, PTR> MakeMemberInfo(std::string_view name, PTR CLASS::*ptr,
																			Meta_t meta) {
	return MemberInfo<CLASS, PTR>(name, ptr, std::move(meta));
}

// template <class CLASS, class PTR>
// constexpr MemberInfo<CLASS, PTR> MakeMemberInfo(std::string_view name,
// PTR&(CLASS::* memberGetPtr)() const, void(CLASS::* memberSetPtr)(PTR&)) {
//	return MemberInfo<CLASS, PTR>(name, memberGetPtr, memberSetPtr);
// }
//
// template <class CLASS, class PTR>
// constexpr MemberInfo<CLASS, PTR> MakeMemberInfo(std::string_view name,
// PTR&(CLASS::* memberGetPtr)() const, void(CLASS::* memberSetPtr)(PTR&),
// Meta_t meta) { 	return MemberInfo<CLASS, PTR>(name, memberGetPtr,
//memberSetPtr, std::move(meta));
// }

template <class CLASS, class PTR>
MemberInfo<CLASS, PTR> MakeMemberInfo(std::string_view name,
																			PTR (CLASS::*memberGetPtr)() const,
																			void (CLASS::*memberSetPtr)(PTR)) {
	return MemberInfo<CLASS, PTR>(name, memberGetPtr, memberSetPtr);
}

template <class CLASS, class PTR>
MemberInfo<CLASS, PTR>
MakeMemberInfo(std::string_view name, PTR (CLASS::*memberGetPtr)() const,
							 void (CLASS::*memberSetPtr)(PTR), Meta_t meta) {
	return MemberInfo<CLASS, PTR>(name, memberGetPtr, memberSetPtr,
																std::move(meta));
}

template <class CLASS, class PTR>
MemberInfo<CLASS, PTR>
MakeMemberInfoLambda(std::string_view name,
										 std::function<PTR(CLASS &)> memberGetPtr,
										 std::function<void(CLASS &, PTR)> memberSetPtr) {
	return MemberInfo<CLASS, PTR>(name, memberGetPtr, memberSetPtr);
}

template <class CLASS, class PTR>
MemberInfo<CLASS, PTR> MakeMemberInfoLambda(
		std::string_view name, std::function<PTR(CLASS &)> memberGetPtr,
		std::function<void(CLASS &, PTR)> memberSetPtr, Meta_t meta) {
	return MemberInfo<CLASS, PTR>(name, memberGetPtr, memberSetPtr,
																std::move(meta));
}
} // namespace IKIGAI::UTILS