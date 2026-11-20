#include "viewStore.h"
#include <stdexcept>

namespace IKIGAI::UI {
void ViewInstance::finishEdit(const Node& node) {
	auto it = edits.find(node.id);
	if (it == edits.end() || !it->second.editing)
		return;
	auto& edit = it->second;
	edit.editing = false;
	const auto after = controller->get(node.value.binding);
	if (edit.before != after) {
		const auto& property = controller->properties.at(node.value.binding);
		if (property.commit)
			property.commit(edit.before, after);
		node.onCommit.invoke(*controller);
	}
}
void ViewInstance::finishEdits(const std::vector<Node>& nodes) {
	for (const auto& node : nodes) {
		finishEdit(node);
		finishEdits(node.children);
	}
}
namespace {
	void checkWidgets(const std::vector<Node>& nodes, const std::set<std::string>& registered) {
		for (const auto& node : nodes) {
			if (node.type == "custom" && !registered.contains(node.widget))
				throw std::runtime_error("Unregistered custom widget '" + node.widget + "' in '" + node.id + "'");
			checkWidgets(node.children, registered);
		}
	}
}
bool ViewStore::reload(const std::string& path, const Reader& read, bool force) {
	auto& entry = windows[path];
	std::string source, script;
	try {
		source = read(path);
		// Check the dependent script too, without reparsing unchanged JSON.
		if (!force && source == entry.attemptedSource) {
			script = entry.attemptedScriptPath.empty() ? std::string() : read(entry.attemptedScriptPath);
			if (script == entry.attemptedScript)
				return false;
		}
		auto definition = ViewDefinition::parse(source);
		auto backend = definition.controller.at("backend").get<std::string>();
		entry.attemptedScriptPath = backend == "native" ? std::string() : definition.controller.at("path").get<std::string>();
		if (backend != "native")
			script = read(entry.attemptedScriptPath);
		if (!force && source == entry.attemptedSource && script == entry.attemptedScript)
			return false;
		entry.attemptedSource = source;
		entry.attemptedScript = script;
		for (const auto& [otherPath, other] : windows)
			if (otherPath != path && other.instance && other.instance->definition.id == definition.id)
				throw std::runtime_error("Duplicate window id '" + definition.id + "'");
		auto candidate = std::make_unique<ViewInstance>();
		if (backend == "native") {
			candidate->controller = factories.at(definition.controller.at("name").get<std::string>())();
		}
		else {
			std::shared_ptr<Controller> host;
			if (definition.controller.contains("host"))
				host = factories.at(definition.controller.at("host").get<std::string>())();
			candidate->controller = CreateScriptController(definition.controller, script, std::move(host));
		}
		if (!candidate->controller)
			throw std::runtime_error("Controller factory returned null");
		checkWidgets(definition.children, customWidgets);
		definition.validate(*candidate->controller);
		candidate->definition = std::move(definition);
		// Persist the open state. Active edit buffers reference the old contract
		// and intentionally end at reload; scripts start with fresh state.
		if (entry.instance)
			candidate->open = entry.instance->open;
		if (entry.instance && !entry.instance->faulted)
			entry.instance->finishEdits(entry.instance->definition.children);
		candidate->controller->activate();
		entry.instance = std::move(candidate);
		entry.error.clear();
		return true;
	} catch (const std::exception& e) {
		entry.attemptedSource = source;
		entry.attemptedScript = script;
		entry.error = path + ": " + e.what();
		return false;
	}
}
}
