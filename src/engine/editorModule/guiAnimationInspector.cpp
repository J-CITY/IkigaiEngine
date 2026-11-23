#include "guiAnimationInspector.h"

#ifdef USE_EDITOR
#include "coreModule/gui/components/guiAnimationComponent.h"
#include "guiAnimationGraphEditor.h"
#include "imgui.h"
#include "imguiHelper/imguiWidgets.h"
#include "misc/cpp/imgui_stdlib.h"
#include "prefabEditor.h"

#include <cstddef>
#include <magic_enum.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace IKIGAI::EDITOR {
namespace {
	struct BindingEdit {
		std::optional<std::size_t> index;
		ECS::GuiAnimationBinding binding;
		bool graph = false;
	};

	template <class Enum> void drawEnum(const char* label, Enum& value) {
		const auto name = magic_enum::enum_name(value);
		if (!ImGui::BeginCombo(label, name.empty() ? "" : name.data())) {
			return;
		}
		for (const auto& [option, text] : magic_enum::enum_entries<Enum>()) {
			const bool selected = option == value;
			if (ImGui::Selectable(text.data(), selected)) {
				value = option;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	bool applyBindings(ECS::GuiAnimationComponent& component, std::vector<ECS::GuiAnimationBinding> bindings) {
		const auto before = component.bindings();
		if (!component.setBindings(bindings)) {
			return false;
		}
		const int objectId = component.obj->getIDInt();
		auto update = [objectId](auto values) {
			return [objectId, values = std::move(values)](SCENE_SYSTEM::Scene& scene) {
				const auto object = scene.findObjectByID(ECS::Entity(ECS::Entity::ID(objectId)));
				const auto animation = object ? object->getComponent<ECS::GuiAnimationComponent>() : UTILS::WeakPtr<ECS::GuiAnimationComponent> {};
				if (!animation) {
					throw std::runtime_error("GUI animation component no longer exists");
				}
				if (!animation->setBindings(values)) {
					throw std::runtime_error(animation->validationError());
				}
			};
		};
		EditorRender::GlobalState.mCommandHistory.addCommand(
			std::make_unique<PREFABS::Command>("Edit GUI animation bindings", update(std::move(bindings)), update(before), true));
		return true;
	}

	void drawCurveKeys(ECS::GuiAnimationBinding& binding) {
		auto& keys = binding.Keys;
		std::optional<std::size_t> remove;
		for (std::size_t index = 0; index < keys.size(); ++index) {
			ImGui::PushID(static_cast<int>(index));
			auto& key = keys[index];
			if (ImGui::TreeNode("Key", "%.3f s  →  %.3f", key.Time, key.Value)) {
				ImGui::DragFloat("Time (s)", &key.Time, 0.01f, 0.001f, 0.0f);
				ImGui::DragFloat("Value", &key.Value, 0.01f);
				drawEnum("Interpolation", key.Easing);
				if (key.Easing == MATH::CURVES::InterpolationType::CUSTOM) {
					float handles[5] = { key.Control1.x, key.Control1.y, key.Control2.x, key.Control2.y, 0.0f };
					if (ImGui::Bezier("Bézier", handles)) {
						key.Control1 = { handles[0], handles[1] };
						key.Control2 = { handles[2], handles[3] };
					}
				}
				if (ImGui::SmallButton("Remove key")) {
					remove = index;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (remove) {
			keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(*remove));
		}
		if (ImGui::SmallButton("Add key")) {
			ECS::GuiAnimationKey key;
			key.Time = keys.empty() ? 0.15f : keys.back().Time + 0.15f;
			key.Value = keys.empty() ? 1.0f : keys.back().Value;
			keys.push_back(key);
		}
	}

	void drawBindingEdit(BindingEdit& edit) {
		auto& binding = edit.binding;
		drawEnum("Event", binding.Event);
		ImGui::InputTextWithHint("Target", "Self or direct child name", &binding.Target);
		int source = edit.graph ? 1 : 0;
		if (ImGui::Combo("Animation", &source, "Curve\0Node graph\0")) {
			edit.graph = source == 1;
		}
		if (edit.graph) {
			ImGui::InputTextWithHint("Graph path", "/animations/gui/example.json", &binding.GraphPath);
			if (ImGui::SmallButton("Open node editor")) {
				openGuiAnimationGraphEditor(binding.GraphPath);
			}
		}
		else {
			drawEnum("Property", binding.Property);
			drawCurveKeys(binding);
		}
	}
}

void drawGuiAnimationInspector(ECS::GuiAnimationComponent& component) {
	static std::weak_ptr<ECS::Object> editedObject;
	static std::optional<BindingEdit> edit;
	const auto owner = component.obj->weak_from_this();
	if (owner.owner_before(editedObject) || editedObject.owner_before(owner)) {
		editedObject = owner;
		edit.reset();
	}

	ImGui::PushID(&component);
	const auto& currentBindings = component.bindings();
	std::optional<std::size_t> remove;
	if (ImGui::BeginTable("Animation bindings", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableSetupColumn("Event");
		ImGui::TableSetupColumn("Target");
		ImGui::TableSetupColumn("Animation");
		ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();
		for (std::size_t index = 0; index < currentBindings.size(); ++index) {
			const auto& binding = currentBindings[index];
			ImGui::PushID(static_cast<int>(index));
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(magic_enum::enum_name(binding.Event).data());
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(binding.Target.empty() ? "Self" : binding.Target.c_str());
			ImGui::TableSetColumnIndex(2);
			if (binding.GraphPath.empty()) {
				ImGui::TextUnformatted(magic_enum::enum_name(binding.Property).data());
			}
			else if (ImGui::SmallButton("Node graph")) {
				openGuiAnimationGraphEditor(binding.GraphPath);
			}
			ImGui::TableSetColumnIndex(3);
			if (ImGui::SmallButton("Edit")) {
				edit = BindingEdit { index, binding, !binding.GraphPath.empty() };
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove")) {
				remove = index;
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	if (remove) {
		auto bindings = currentBindings;
		bindings.erase(bindings.begin() + static_cast<std::ptrdiff_t>(*remove));
		applyBindings(component, std::move(bindings));
		edit.reset();
	}
	if (ImGui::SmallButton("Add animation binding")) {
		edit.emplace();
		ECS::GuiAnimationKey key;
		key.Time = 0.15f;
		key.Value = 1.0f;
		edit->binding.Keys.push_back(key);
	}
	if (edit) {
		ImGui::Separator();
		drawBindingEdit(*edit);
		const bool complete = edit->graph ? !edit->binding.GraphPath.empty() : !edit->binding.Keys.empty();
		ImGui::BeginDisabled(!complete);
		if (ImGui::Button("Apply binding")) {
			auto bindings = currentBindings;
			auto binding = edit->binding;
			if (edit->graph) {
				binding.Keys.clear();
			}
			else {
				binding.GraphPath.clear();
			}
			if (edit->index && *edit->index < bindings.size()) {
				bindings[*edit->index] = std::move(binding);
			}
			else {
				bindings.push_back(std::move(binding));
			}
			if (applyBindings(component, std::move(bindings))) {
				edit.reset();
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel edit")) {
			edit.reset();
		}
	}
	if (!component.validationError().empty()) {
		ImGui::TextWrapped("%s", component.validationError().c_str());
	}
	ImGui::PopID();
}
}
#endif
