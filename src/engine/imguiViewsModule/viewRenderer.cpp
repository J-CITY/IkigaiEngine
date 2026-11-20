#include "viewRenderer.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace IKIGAI::UI {
namespace {
	struct WindowScope {
		~WindowScope() {
			ImGui::End();
		}
	};
	struct IdScope {
		explicit IdScope(const char* id) {
			ImGui::PushID(id);
		}
		~IdScope() {
			ImGui::PopID();
		}
	};
	struct DisabledScope {
		explicit DisabledScope(bool b) {
			ImGui::BeginDisabled(b);
		}
		~DisabledScope() {
			ImGui::EndDisabled();
		}
	};
	struct GroupScope {
		GroupScope() {
			ImGui::BeginGroup();
		}
		~GroupScope() {
			ImGui::EndGroup();
		}
	};
	int integer(const Json& value) {
		const auto v = value.get<int64_t>();
		if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
			throw std::runtime_error("Widget value exceeds int range");
		return static_cast<int>(v);
	}
}
void ViewRenderer::drawNode(const Node& n, ViewInstance& instance) {
	auto& c = *instance.controller;
	if (!n.visible.read(c).get<bool>()) {
		instance.finishEdit(n);
		instance.finishEdits(n.children);
		return;
	}
	IdScope id(n.id.c_str());
	DisabledScope disabled(!n.enabled.read(c).get<bool>());
	if (n.type == "row" || n.type == "column" || n.type == "group") {
		GroupScope group;
		bool first = true;
		for (const auto& child : n.children) {
			if (!child.visible.read(c).get<bool>()) {
				instance.finishEdit(child);
				instance.finishEdits(child.children);
				continue;
			}
			if (n.type == "row" && !first)
				ImGui::SameLine();
			drawNode(child, instance);
			first = false;
		}
		return;
	}
	if (n.type == "text") {
		const auto text = n.value.read(c).get<std::string>();
		ImGui::TextUnformatted(text.c_str());
		return;
	}
	if (n.type == "separator") {
		ImGui::Separator();
		return;
	}
	if (n.type == "button") {
		if (ImGui::Button(n.label.c_str()))
			n.onClick.invoke(c);
		return;
	}
	if (n.type == "custom") {
		custom.at(n.widget)(n, c);
		return;
	}
	auto before = n.value.read(c);
	auto after = before;
	auto& edit = instance.edits[n.id];
	bool changed = false;
	// Labels are display text; ### keeps identity independent of the label.
	const auto label = n.label + "###value";
	if (n.type == "checkbox") {
		bool value = before.get<bool>();
		changed = ImGui::Checkbox(label.c_str(), &value);
		after = value;
	}
	else if (n.type == "inputText") {
		if (!edit.editing)
			edit.text = before.get<std::string>();
		changed = ImGui::InputText(label.c_str(), &edit.text);
		after = edit.text;
	}
	else if (n.type == "sliderFloat") {
		const auto v = before.get<double>();
		if (std::abs(v) > std::numeric_limits<float>::max())
			throw std::runtime_error("Widget value exceeds float range");
		float value = static_cast<float>(v);
		changed = ImGui::SliderFloat(label.c_str(), &value, float(n.min), float(n.max));
		after = value;
	}
	else if (n.type == "sliderInt") {
		int value = integer(before);
		changed = ImGui::SliderInt(label.c_str(), &value, int(n.min), int(n.max));
		after = value;
	}
	else if (n.type == "combo") {
		int value = integer(before);
		if (value < 0 || value >= static_cast<int>(n.items.size()))
			throw std::runtime_error("Combo index out of range");
		std::vector<const char*> items;
		for (const auto& item : n.items)
			items.push_back(item.c_str());
		changed = ImGui::Combo(label.c_str(), &value, items.data(), static_cast<int>(items.size()));
		after = value;
	}
	if (ImGui::IsItemActivated() || (changed && !edit.editing)) {
		edit.before = before;
		edit.editing = true;
	}
	if (changed) {
		c.set(n.value.binding, after);
		n.onChange.invoke(c);
	}
	if (edit.editing && !ImGui::IsItemActive())
		instance.finishEdit(n);
}

void ViewRenderer::draw(ViewInstance& instance, const std::string& error) {
	if (!instance.open) {
		if (!instance.faulted)
			instance.finishEdits(instance.definition.children);
		return;
	}
	const auto& size = instance.definition.initialSize;
	ImGui::SetNextWindowSize({ size[0], size[1] }, ImGuiCond_FirstUseEver);
	if (instance.definition.initialPosition) {
		const auto origin = ImGui::GetMainViewport()->WorkPos;
		const auto& pos = *instance.definition.initialPosition;
		ImGui::SetNextWindowPos({ origin.x + pos[0], origin.y + pos[1] }, ImGuiCond_FirstUseEver);
	}
	const auto title = instance.definition.title + "###json-view/" + instance.definition.id;
	const bool visible = ImGui::Begin(title.c_str(), &instance.open);
	WindowScope window;
	if (!visible || !instance.open) {
		if (!instance.faulted)
			instance.finishEdits(instance.definition.children);
		return;
	}
	if (!error.empty())
		ImGui::TextWrapped("%s", error.c_str());
	if (instance.faulted) {
		ImGui::TextUnformatted("Controller failed. Fix the source and reload (F8).");
		return;
	}
	for (const auto& node : instance.definition.children)
		drawNode(node, instance);
}
}
