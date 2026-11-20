#include "view.h"
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace IKIGAI::UI {
namespace {
	[[noreturn]] void fail(const std::string& message) {
		throw std::runtime_error(message);
	}
	void keys(const Json& object, std::initializer_list<const char*> allowed) {
		if (!object.is_object())
			fail("Expected an object");
		for (const auto& [key, value] : object.items()) {
			bool found = false;
			for (auto a : allowed)
				found |= key == a;
			if (!found)
				fail("Unknown field '" + key + "'");
		}
	}
	ValueRef ref(const Json& object, const char* key, Json fallback = nullptr) {
		if (!object.contains(key))
			return { std::move(fallback), { } };
		const auto& value = object.at(key);
		if (value.is_object()) {
			keys(value, { "bind" });
			auto name = value.at("bind").get<std::string>();
			if (name.empty())
				fail(std::string(key) + ": empty binding");
			return { nullptr, name };
		}
		return { value, { } };
	}
	Action action(const Json& node, const char* key) {
		if (!node.contains(key))
			return { };
		const auto& a = node.at(key);
		keys(a, { "command", "args" });
		Action result { a.at("command").get<std::string>(), a.value("args", Json::object()) };
		if (result.command.empty() || !result.args.is_object())
			fail(std::string(key) + ": expected command and argument object");
		for (const auto& [name, value] : result.args.items())
			if (!(value.is_boolean() || value.is_number() || value.is_string()))
				fail(std::string(key) + ": arguments must be scalar values");
		return result;
	}
	Node parseNode(const Json& j, std::set<std::string>& ids, int depth, const std::string& path) {
		try {
			if (depth > 64)
				fail("View nesting exceeds 64 levels");
			keys(j,
				{ "type", "id", "label", "widget", "value", "visible", "enabled", "min", "max", "items", "children", "onClick", "onChange",
				"onCommit" });
			Node n;
			n.id = j.at("id").get<std::string>();
			if (n.id.empty() || !ids.insert(n.id).second)
				fail("Empty or duplicate id '" + n.id + "'");
			n.type = j.at("type").get<std::string>();
			static const std::set<std::string> types { "text", "button", "checkbox", "inputText", "sliderFloat", "sliderInt", "combo", "column",
				"row", "group", "separator", "custom" };
			if (!types.contains(n.type))
				fail("Unknown widget type '" + n.type + "'");
			n.label = j.value("label", n.id);
			n.widget = j.value("widget", std::string());
			if (n.type == "custom" && n.widget.empty())
				fail("custom requires widget");
			n.value = ref(j, "value", n.type == "text" ? Json(n.label) : Json());
			n.visible = ref(j, "visible", true);
			n.enabled = ref(j, "enabled", true);
			n.min = j.value("min", 0.0);
			n.max = j.value("max", 1.0);
			if (!std::isfinite(n.min) || !std::isfinite(n.max) || n.min > n.max || std::abs(n.min) > std::numeric_limits<float>::max()
				|| std::abs(n.max) > std::numeric_limits<float>::max())
				fail("Invalid min/max");
			if (n.type == "sliderInt"
				&& (std::trunc(n.min) != n.min || std::trunc(n.max) != n.max || n.min < std::numeric_limits<int>::min()
					|| n.max > std::numeric_limits<int>::max()))
				fail("sliderInt bounds must fit int");
			if (j.contains("items"))
				n.items = j.at("items").get<std::vector<std::string>>();
			if (n.type == "combo" && n.items.empty())
				fail("combo requires nonempty items");
			n.onClick = action(j, "onClick");
			n.onChange = action(j, "onChange");
			n.onCommit = action(j, "onCommit");
			if (j.contains("children")) {
				if (n.type != "row" && n.type != "column" && n.type != "group")
					fail("Only containers accept children");
				if (!j.at("children").is_array())
					fail("children must be an array");
				size_t i = 0;
				for (const auto& child : j.at("children"))
					n.children.push_back(parseNode(child, ids, depth + 1, path + "/children/" + std::to_string(i++)));
			}
			return n;
		} catch (const std::exception& e) {
			fail(path + " (id=" + j.value("id", std::string("?")) + "): " + e.what());
		}
	}
	void validateRef(const ValueRef& r, ValueType expected, const Controller& controller, bool writable = false) {
		if (!r.binding.empty()) {
			const auto& p = controller.properties.at(r.binding);
			if (p.type != expected)
				fail("Property '" + r.binding + "' has incompatible type");
			if (writable && !p.set)
				fail("Property '" + r.binding + "' is read-only");
		}
		else if (writable)
			fail("Editable value requires a binding");
		CheckValue(expected, r.read(controller));
	}
	void validateNode(const Node& n, const Controller& c) {
		try {
			validateRef(n.visible, ValueType::Boolean, c);
			validateRef(n.enabled, ValueType::Boolean, c);
			if (n.type == "text")
				validateRef(n.value, ValueType::String, c);
			if (n.type == "inputText")
				validateRef(n.value, ValueType::String, c, true);
			if (n.type == "checkbox")
				validateRef(n.value, ValueType::Boolean, c, true);
			if (n.type == "sliderFloat")
				validateRef(n.value, ValueType::Number, c, true);
			if (n.type == "sliderInt" || n.type == "combo")
				validateRef(n.value, ValueType::Integer, c, true);
			if (n.type == "sliderInt" || n.type == "combo") {
				const auto value = n.value.read(c).get<int64_t>();
				if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
					fail("Integer widget value must fit int");
				if (n.type == "combo" && (value < 0 || value >= static_cast<int64_t>(n.items.size())))
					fail("Combo index out of range");
			}
			if (n.type == "sliderFloat" && std::abs(n.value.read(c).get<double>()) > std::numeric_limits<float>::max())
				fail("Float widget value exceeds float range");
			if (!n.onClick.command.empty() && n.type != "button" && n.type != "custom")
				fail("onClick requires a button/custom widget");
			for (const auto* a : { &n.onClick, &n.onChange, &n.onCommit })
				if (!a->command.empty())
					c.checkCommand(a->command, a->args);
			for (const auto& child : n.children)
				validateNode(child, c);
		} catch (const std::exception& e) {
			fail("Widget '" + n.id + "': " + e.what());
		}
	}
}
ValueType ParseValueType(const std::string& name) {
	if (name == "boolean")
		return ValueType::Boolean;
	if (name == "integer")
		return ValueType::Integer;
	if (name == "number")
		return ValueType::Number;
	if (name == "string")
		return ValueType::String;
	fail("Unknown value type '" + name + "'");
}
void CheckValue(ValueType type, const Json& v) {
	bool ok = type == ValueType::Boolean ? v.is_boolean()
		: type == ValueType::Integer	 ? v.is_number_integer()
		: type == ValueType::Number		 ? v.is_number()
										 : v.is_string();
	if (v.is_number())
		ok &= std::isfinite(v.get<double>());
	if (type == ValueType::Integer && v.is_number_unsigned())
		ok &= v.get<uint64_t>() <= uint64_t(INT64_MAX);
	if (!ok)
		fail("Value does not match its declared type");
}
Json Controller::get(const std::string& name) const {
	if (!properties.contains(name))
		fail("Unknown property '" + name + "'");
	const auto& property = properties.at(name);
	auto value = property.get();
	CheckValue(property.type, value);
	return value;
}
void Controller::set(const std::string& name, const Json& value) {
	if (!properties.contains(name))
		fail("Unknown property '" + name + "'");
	const auto& property = properties.at(name);
	CheckValue(property.type, value);
	if (!property.set)
		fail("Read-only property '" + name + "'");
	const auto before = get(name);
	property.set(value);
	const auto after = get(name);
	if (before != after) {
		const auto snapshot = observers->listeners;
		for (const auto& [id, listener] : snapshot)
			if (listener.property == name && observers->listeners.contains(id))
				listener.callback(before, after);
	}
}
Controller::Connection Controller::subscribe(const std::string& property, std::function<void(const Json&, const Json&)> callback) {
	if (!properties.contains(property) || !callback)
		fail("Subscription requires a property and callback");
	const auto id = ++observers->next;
	observers->listeners.emplace(id, Observers::Listener { property, std::move(callback) });
	return { observers, id };
}
void Controller::checkCommand(const std::string& name, const Json& args) const {
	if (!commands.contains(name))
		fail("Unknown command '" + name + "'");
	const auto& command = commands.at(name);
	if (!args.is_object() || args.size() != command.arguments.size())
		fail("Wrong arguments for command '" + name + "'");
	for (const auto& [key, type] : command.arguments)
		CheckValue(type, args.at(key));
}
void Controller::invoke(const std::string& name, const Json& args) {
	checkCommand(name, args);
	commands.at(name).call(args);
}
Json ValueRef::read(const Controller& controller) const {
	return binding.empty() ? constant : controller.get(binding);
}
void Action::invoke(Controller& controller) const {
	if (!command.empty())
		controller.invoke(command, args);
}
ViewDefinition ViewDefinition::parse(const std::string& source) {
	auto j = Json::parse(source);
	keys(j, { "$schema", "version", "id", "title", "controller", "children", "initialSize", "initialPosition" });
	if (j.at("version") != 1)
		fail("Unsupported view version");
	ViewDefinition view;
	view.id = j.at("id").get<std::string>();
	if (view.id.empty() || view.id.find("###") != std::string::npos)
		fail("Invalid view id");
	view.title = j.at("title").get<std::string>();
	if (j.contains("initialSize")) {
		if (!j.at("initialSize").is_array() || j.at("initialSize").size() != 2)
			fail("initialSize requires two numbers");
		view.initialSize = j.at("initialSize").get<std::array<float, 2>>();
	}
	for (auto size : view.initialSize)
		if (!std::isfinite(size) || size < 64)
			fail("initialSize must contain two finite sizes >= 64");
	if (j.contains("initialPosition")) {
		if (!j.at("initialPosition").is_array() || j.at("initialPosition").size() != 2)
			fail("initialPosition requires two numbers");
		view.initialPosition = j.at("initialPosition").get<std::array<float, 2>>();
		for (auto value : *view.initialPosition)
			if (!std::isfinite(value))
				fail("initialPosition must contain two finite coordinates");
	}
	if (view.title.find("###") != std::string::npos)
		fail("Window title must not contain ###");
	view.controller = j.at("controller");
	keys(view.controller, { "backend", "name", "path", "host", "properties", "commands" });
	const auto backend = view.controller.at("backend").get<std::string>();
	if (backend == "native") {
		if (view.controller.at("name").get<std::string>().empty())
			fail("Native controller requires name");
		if (view.controller.contains("path") || view.controller.contains("host") || view.controller.contains("properties")
			|| view.controller.contains("commands"))
			fail("Native controller only accepts backend/name");
	}
	else if (backend == "lua" || backend == "ikigai") {
		auto path = view.controller.at("path").get<std::string>();
		if (!path.starts_with("/") || path.find("..") != std::string::npos || path.find(':') != std::string::npos)
			fail("Script path must be an absolute virtual path without ..");
		if (view.controller.contains("name"))
			fail("Script controller does not accept name");
		if (view.controller.contains("host"))
			view.controller.at("host").get<std::string>();
		const auto exports = view.controller.value("properties", Json::object());
		const auto commands = view.controller.value("commands", Json::object());
		if (!exports.is_object() || !commands.is_object())
			fail("Script properties/commands must be objects");
		for (const auto& [name, p] : exports.items()) {
			keys(p, { "type", "get", "set", "commit" });
			ParseValueType(p.at("type").get<std::string>());
			if (p.at("get").get<std::string>().empty())
				fail("Property getter cannot be empty");
		}
		for (const auto& [name, c] : commands.items()) {
			keys(c, { "call", "args" });
			if (c.at("call").get<std::string>().empty())
				fail("Command function cannot be empty");
		}
	}
	else
		fail("Unknown controller backend '" + backend + "'");
	std::set<std::string> ids;
	if (!j.at("children").is_array())
		fail("children must be an array");
	size_t i = 0;
	for (const auto& child : j.at("children"))
		view.children.push_back(parseNode(child, ids, 0, "/children/" + std::to_string(i++)));
	return view;
}
void ViewDefinition::validate(const Controller& bindings) const {
	for (const auto& child : children)
		validateNode(child, bindings);
}
}
