#pragma once
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace IKIGAI::UI {
using Json = nlohmann::json;
enum class ValueType { Boolean, Integer, Number, String };
ValueType ParseValueType(const std::string& name);
void CheckValue(ValueType type, const Json& value);
using Arguments = std::map<std::string, ValueType>;

// A controller owns its callbacks. Destroying it drops every native/script binding.
class Controller {
	struct Observers {
		uint64_t next = 0;
		struct Listener {
			std::string property;
			std::function<void(const Json&, const Json&)> callback;
		};
		std::map<uint64_t, Listener> listeners;
	};
	std::shared_ptr<Observers> observers = std::make_shared<Observers>();

public:
	class Connection {
		std::weak_ptr<Observers> owner;
		uint64_t id = 0;

	public:
		Connection() = default;
		Connection(std::weak_ptr<Observers> owner, uint64_t id)
			: owner(std::move(owner))
			, id(id) { }
		Connection(const Connection&) = delete;
		Connection& operator=(const Connection&) = delete;
		Connection(Connection&& other) noexcept
			: owner(std::move(other.owner))
			, id(other.id) {
			other.id = 0;
		}
		Connection& operator=(Connection&& other) noexcept {
			if (this != &other) {
				disconnect();
				owner = std::move(other.owner);
				id = other.id;
				other.id = 0;
			}
			return *this;
		}
		void disconnect() {
			if (auto state = owner.lock())
				state->listeners.erase(id);
			owner.reset();
		}
		~Connection() {
			disconnect();
		}
	};
	Controller() = default;
	Controller(const Controller&) = delete;
	Controller& operator=(const Controller&) = delete;
	struct Property {
		ValueType type;
		std::function<Json()> get;
		std::function<void(const Json&)> set;
		// Receives the value before activation and the final value once per edit.
		std::function<void(const Json&, const Json&)> commit;
	};
	struct Command {
		Arguments arguments;
		std::function<void(const Json&)> call;
	};
	virtual ~Controller() = default;
	std::map<std::string, Property> properties;
	std::map<std::string, Command> commands;
	Json get(const std::string& name) const;
	void set(const std::string& name, const Json& value);
	void invoke(const std::string& name, const Json& args);
	void checkCommand(const std::string& name, const Json& args) const;
	Connection subscribe(const std::string& property, std::function<void(const Json&, const Json&)> callback);
	virtual void activate() { }
};

struct ValueRef {
	Json constant;
	std::string binding;
	Json read(const Controller& controller) const;
};
struct Action {
	std::string command;
	Json args = Json::object();
	void invoke(Controller& controller) const;
};
struct Node {
	std::string type, id, label, widget;
	ValueRef value, visible { true, { } }, enabled { true, { } };
	double min = 0, max = 1;
	std::vector<std::string> items;
	std::vector<Node> children;
	Action onClick, onChange, onCommit;
};
struct ViewDefinition {
	std::string id, title;
	std::array<float, 2> initialSize { 420.f, 300.f };
	std::optional<std::array<float, 2>> initialPosition;
	Json controller;
	std::vector<Node> children;
	static ViewDefinition parse(const std::string& source);
	void validate(const Controller& bindings) const;
};

// Script exports are declared in controller.properties/commands. No ECS owner
// is required. The optional host is the explicit native API exposed to a script.
std::shared_ptr<Controller> CreateScriptController(const Json& descriptor, const std::string& source, std::shared_ptr<Controller> host = { });
}
