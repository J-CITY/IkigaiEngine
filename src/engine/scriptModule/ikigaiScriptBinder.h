#pragma once
#if USE_IKIGAI_SCRIPT
#include <Library/ikigaiScript.h>
#include <ecsModule/world.h>
#include <mathModule/math.h>
#include <utilsModule/weakPtr.h>
namespace IKIGAI::ECS {
class Object;
class ComponentBase;
}
#include <any>
#include <stdexcept>
#include <typeindex>

namespace IKIGAI::SCRIPTING {
// Each runtime owns its bindings. Script handles never own engine entities.
class IkigaiScriptBinder {
	using V = IkigaiScript::ValuePtr;
	using C = IkigaiScript::Class;
	using Args = IkigaiScript::List;
	IkigaiScript::IkigaiScriptInterpreter& vm;
	std::unordered_map<std::type_index, std::string> names;
	struct Handle {
		std::type_index type;
		std::any value;
		V script;
		std::weak_ptr<ECS::ComponentManager> components {};
		std::function<bool()> alive = [] { return true; };
	};
	std::vector<Handle> handles;

public:
	V classValue(const std::shared_ptr<C>& c) {
		auto result = vm.makeValue(c);
		result->typeDescriptor.subtype = c->name;
		return result;
	}
	template <class T> void mathName(const std::string& name) {
		names.emplace(typeid(T), name);
	}
	struct MathsOnly { };
	IkigaiScriptBinder(IkigaiScript::IkigaiScriptInterpreter& interpreter, MathsOnly)
		: vm(interpreter) {
		bindMaths();
	}
	explicit IkigaiScriptBinder(IkigaiScript::IkigaiScriptInterpreter& interpreter);
	V object(ECS::Object& owner);
	void bindMaths();
	void bindActor();
	void bindComponents();
	void bindGlobals();

	template <class T>
	static constexpr bool vector = std::is_same_v<T, MATH::Vector2f> || std::is_same_v<T, MATH::Vector3f> || std::is_same_v<T, MATH::Vector4f>;
	template <class T> static constexpr bool matrix = std::is_same_v<T, MATH::Matrix3f> || std::is_same_v<T, MATH::Matrix4f>;
	template <class T> static constexpr bool mathValue = vector<T> || matrix<T> || std::is_same_v<T, MATH::QuaternionF>;
	template <class T> void type(const std::string& name) {
		names.emplace(typeid(T), name);
		vm.newClass(name, { },
			[](C*, IkigaiScript::ScopePtr, const Args&) -> V { throw std::runtime_error("Engine references cannot be constructed directly"); }, { });
		if constexpr (std::is_base_of_v<ECS::ComponentBase, T>) {
			method<T>("GetOwner", [this](T& c, const Args&) { return object(c.obj.get()); });
		}
	}
	template <class T> V value(T&& input) {
		using U = std::remove_cvref_t<T>;
		if constexpr (std::is_same_v<U, V>)
			return input;
		else if constexpr (std::is_same_v<U, bool>)
			return vm.makeValue(input);
		else if constexpr (std::is_enum_v<U> || std::is_integral_v<U>)
			return vm.makeValue(static_cast<IkigaiScript::Int>(input));
		else if constexpr (std::is_floating_point_v<U>)
			return vm.makeValue(static_cast<IkigaiScript::Float>(input));
		else if constexpr (std::is_convertible_v<U, std::string>)
			return vm.makeValue(std::string(input));
		else if constexpr (mathValue<U>) {
			auto c = std::make_shared<C>(vm.resolveScope(names.at(typeid(U))));
			if constexpr (matrix<U>) {
				constexpr size_t count = std::is_same_v<U, MATH::Matrix3f> ? 9 : 16;
				for (size_t i = 0; i < count; ++i)
					c->variables["m" + std::to_string(i)] = value(input.getData()[i]);
			}
			else {
				c->variables["x"] = value(input.x);
				c->variables["y"] = value(input.y);
				if constexpr (!std::is_same_v<U, MATH::Vector2f>)
					c->variables["z"] = value(input.z);
				if constexpr (std::is_same_v<U, MATH::Vector4f> || std::is_same_v<U, MATH::QuaternionF>)
					c->variables["w"] = value(input.w);
			}
			return classValue(c);
		}
		else
			return owned(std::make_shared<U>(std::forward<T>(input)));
	}
	template <class T> std::remove_cvref_t<T> read(const V& v) {
		using U = std::remove_cvref_t<T>;
		if (!v)
			throw std::runtime_error("Missing native argument");
		if constexpr (std::is_same_v<U, V>)
			return v;
		else if constexpr (std::is_same_v<U, bool>) {
			// Relational operators in the embedded language return Int(0/1).
			if (v->getType() != IkigaiScript::Type::Bool && v->getType() != IkigaiScript::Type::Int)
				throw std::runtime_error("Expected Bool or integer condition");
			return v->getBool();
		}
		else if constexpr (std::is_enum_v<U> || std::is_integral_v<U>) {
			if (v->getType() != IkigaiScript::Type::Int)
				throw std::runtime_error("Expected Int");
			return static_cast<U>(v->getInt());
		}
		else if constexpr (std::is_floating_point_v<U>) {
			if (v->getType() == IkigaiScript::Type::Int)
				return static_cast<U>(v->getInt());
			if (v->getType() != IkigaiScript::Type::Float)
				throw std::runtime_error("Expected number");
			return static_cast<U>(v->getFloat());
		}
		else if constexpr (std::is_same_v<U, std::string>) {
			if (v->getType() != IkigaiScript::Type::String)
				throw std::runtime_error("Expected String");
			return v->getString();
		}
		else if constexpr (mathValue<U>) {
			if (v->getType() != IkigaiScript::Type::Class || v->getClass()->name != names.at(typeid(U)))
				throw std::runtime_error("Expected " + names.at(typeid(U)) + ", got "
					+ (v->getType() == IkigaiScript::Type::Class ? v->getClass()->name : std::to_string(int(v->getType()))));
			auto& fields = v->getClass()->variables;
			U r;
			if constexpr (matrix<U>) {
				constexpr size_t count = std::is_same_v<U, MATH::Matrix3f> ? 9 : 16;
				for (size_t i = 0; i < count; ++i)
					r.getData()[i] = read<float>(fields.at("m" + std::to_string(i)));
			}
			else {
				r.x = read<float>(fields.at("x"));
				r.y = read<float>(fields.at("y"));
				if constexpr (!std::is_same_v<U, MATH::Vector2f>)
					r.z = read<float>(fields.at("z"));
				if constexpr (std::is_same_v<U, MATH::Vector4f> || std::is_same_v<U, MATH::QuaternionF>)
					r.w = read<float>(fields.at("w"));
			}
			return r;
		}
		else {
			if (v->getType() != IkigaiScript::Type::Class)
				throw std::runtime_error("Expected engine reference");
			return *pointer<U>(v->getClass().get());
		}
	}
	template <class T> T* pointer(C* c) {
		if (!c || !c->variables.contains("__handle")) {
			throw std::runtime_error("Invalid engine handle");
		}
		auto id = read<size_t>(c->variables.at("__handle"));
		if (id >= handles.size() || handles[id].type != typeid(T)) {
			throw std::runtime_error("Wrong engine handle type");
		}
		if (!handles[id].alive()) {
			throw std::runtime_error("Engine reference has expired");
		}
		auto& h = handles[id].value;
		if constexpr (std::is_same_v<T, ECS::Object>) {
			auto p = std::any_cast<std::weak_ptr<T>&>(h).lock();
			if (!p || !p->isAlive()) {
				throw std::runtime_error("Object has been destroyed");
			}
			return p.get();
		}
		else if constexpr (std::is_base_of_v<ECS::ComponentBase, T>) {
			const auto manager = handles[id].components.lock();
			auto* component = manager ? manager->resolveComponent(std::any_cast<const ECS::ComponentHandle<T>&>(h)) : nullptr;
			if (!component) {
				throw std::runtime_error("Component has been removed");
			}
			return component;
		}
		else {
			return std::any_cast<std::shared_ptr<T>&>(h).get();
		}
	}
	template <class T, class H> V handle(H ref, std::weak_ptr<ECS::ComponentManager> components = {}) {
		if (!ref) {
			return vm.makeValue();
		}
		// Reuse wrappers, including components relocated by sparse ECS storage.
		for (auto& h : handles) {
			if (h.type == typeid(T)) {
				auto* existing = std::any_cast<H>(&h.value);
				if (existing && *existing == ref) {
					return h.script;
				}
			}
		}
		auto c = std::make_shared<C>(vm.resolveScope(names.at(typeid(T))));
		c->variables["__handle"] = value(handles.size());
		auto result = classValue(c);
		handles.push_back({ typeid(T), std::move(ref), result, std::move(components) });
		return result;
	}
	template <class T> T* reference(const V& v) {
		if (!v || v->getType() != IkigaiScript::Type::Class)
			throw std::runtime_error("Expected engine reference");
		return pointer<T>(v->getClass().get());
	}
	template <class T> std::shared_ptr<T> resource(const V& v) {
		if (v && v->getType() == IkigaiScript::Type::Null)
			return { };
		reference<T>(v);
		auto id = read<size_t>(v->getClass()->variables.at("__handle"));
		return std::any_cast<std::shared_ptr<T>>(handles.at(id).value);
	}
	template <class T> V owned(std::shared_ptr<T> ref) {
		return handle<T>(std::move(ref));
	}
	template <class T> V component(UTILS::WeakPtr<T> ref) {
		if (!ref) {
			return vm.makeValue();
		}
		auto& owner = ref->obj.get();
		auto& world = owner.getWorld();
		auto* manager = world.getComponentManager();
		const auto identity = manager->getComponentHandle<T>(owner.getID());
		return manager->resolveComponent(identity) == ref.get() ? handle<T>(identity, world.getComponentManagerWeak()) : vm.makeValue();
	}
	template <class T> V component(ECS::World& world, ECS::Entity owner) {
		return handle<T>(world.getComponentManager()->getComponentHandle<T>(owner), world.getComponentManagerWeak());
	}
	template <class T, class F> void method(const char* name, F fn) {
		auto scope = vm.resolveScope(names.at(typeid(T)));
		vm.newFunction(name, scope, IkigaiScript::ClassLambda([this, fn](C* self, IkigaiScript::ScopePtr, const Args& args) -> V {
			if constexpr (mathValue<T>) {
				auto v = vm.makeValue(std::make_shared<C>(*self));
				auto target = read<T>(v);
				auto result = fn(target, args);
				self->variables = value(target)->getClass()->variables;
				return result;
			}
			else
				return fn(*pointer<T>(self), args);
		}));
	}
	template <class R, class T, class... A> void member(const char* name, R (T::*fn)(A...)) {
		memberImpl<T, R, A...>(name, fn);
	}
	template <class R, class T, class... A> void member(const char* name, R (T::*fn)(A...) const) {
		memberImpl<T, R, A...>(name, fn);
	}
	template <class T, class R, class... A, class F> void memberImpl(const char* name, F fn) {
		method<T>(name, [this, fn](T& t, const Args& args) -> V {
			if (args.size() != sizeof...(A))
				throw std::runtime_error("Wrong native argument count");
			return [&]<size_t... I>(std::index_sequence<I...>) -> V {
				if constexpr (std::is_void_v<R>) {
					std::invoke(fn, t, read<A>(args[I])...);
					return vm.makeValue();
				}
				else
					return value(std::invoke(fn, t, read<A>(args[I])...));
			}(std::index_sequence_for<A...> { });
		});
	}
	template <class R, class... A> void freeFunction(const std::string& scope, const std::string& name, R (*fn)(A...)) {
		function(scope, name, [this, fn](const Args& args) -> V {
			if (args.size() != sizeof...(A))
				throw std::runtime_error("Wrong native argument count");
			return [&]<size_t... I>(std::index_sequence<I...>) -> V { return value(fn(read<A>(args[I])...)); }(std::index_sequence_for<A...> { });
		});
	}
	template <class T, class U, class F> void binaryOperator(const char* op, F fn) {
		auto body = IkigaiScript::Lambda([this, fn](const Args& a) { return value(fn(read<T>(a.at(0)), read<U>(a.at(1)))); });
		auto f = std::make_shared<IkigaiScript::Function>(op, body);
		f->type = IkigaiScript::FunctionType::Operator;
		f->argNames = { "lhs", "rhs" };
		auto td = vm.checkTypeInScope(names.at(typeid(T)), vm.getGlobalScope());
		auto ud = td;
		if constexpr (std::is_same_v<U, float>)
			ud = vm.checkTypeInScope("Float", vm.getGlobalScope());
		else
			ud = vm.checkTypeInScope(names.at(typeid(U)), vm.getGlobalScope());
		f->argTypes = { td, ud };
		vm.resolveScope(names.at(typeid(T)))->operators[op].push_back(f);
	}
	void function(const std::string& scope, const std::string& name, IkigaiScript::Lambda fn);
};
}
#endif
