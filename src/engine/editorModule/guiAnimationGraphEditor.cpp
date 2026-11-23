#include "guiAnimationGraphEditor.h"

#ifdef USE_EDITOR
#include "coreModule/gui/components/guiAnimationComponent.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/serialization/typedDescriptor.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <future>
#include <magic_enum.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace IKIGAI::EDITOR {
namespace {
	using Node = ECS::GuiAnimationNode;
	using Kind = ECS::GuiAnimationNodeKind;
	using FileSystem = RESOURCES::FileSystem;
	using Easing = MATH::CURVES::InterpolationType;
	using NodePath = std::vector<std::size_t>;

	enum class OpenMode { Existing, New, ExistingOrNew };

	struct OpenRequest {
		OpenMode mode = OpenMode::Existing;
		std::string path {};
	};

	struct GraphEditor {
		bool open = false;
		bool focus = false;
		bool dirty = false;
		std::string pathInput {};
		std::string documentPath {};
		std::string savedPath {};
		std::string status {};
		std::optional<Node> root {};
		NodePath selection {};
		std::optional<NodePath> expand {};
		std::optional<OpenRequest> requested {};
		std::string pendingPath {};
		std::shared_future<void> save {};
	};

	GraphEditor& editor() {
		static GraphEditor state;
		return state;
	}

	bool isGroup(Kind kind) {
		return kind == Kind::Sequence || kind == Kind::Parallel;
	}

	template <class Enum> bool editEnum(const char* label, Enum& value, std::span<const Enum> allowed = magic_enum::enum_values<Enum>()) {
		bool changed = false;
		if (ImGui::BeginCombo(label, magic_enum::enum_name(value).data())) {
			for (const auto option : allowed) {
				const bool selected = option == value;
				if (ImGui::Selectable(magic_enum::enum_name(option).data(), selected)) {
					changed = !selected;
					value = option;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	std::string graphPath(const std::string& path) {
		auto normalized = FileSystem::normalizePath(path);
		if (normalized == "/") {
			throw std::invalid_argument("Enter a graph asset file path.");
		}
		return normalized;
	}

	void openDocument(GraphEditor& state, const OpenRequest& request) {
		try {
			auto path = graphPath(request.path);
			auto& fs = RESOURCES::ServiceManager::Get<FileSystem>();
			if (fs.isDir(path)) {
				throw std::runtime_error("Choose a graph file inside the directory.");
			}
			auto file = fs.getLiveFile(path);
			const bool existing = file && file->isValid();
			if (request.mode == OpenMode::New && existing) {
				throw std::runtime_error("A file already exists at this path. Open it to edit.");
			}
			if (request.mode == OpenMode::Existing && !existing) {
				throw std::runtime_error("Graph asset was not found.");
			}
			Node root;
			if (existing) {
				root = DATA::readDescriptor<Node>(nlohmann::json::parse(file->readStr()));
			}
			else {
				root.Kind = Kind::Sequence;
				root.Children.emplace_back();
			}
			state.root = std::move(root);
			state.pathInput = path;
			state.documentPath = path;
			state.savedPath = existing ? path : std::string {};
			state.selection.clear();
			state.expand = NodePath {};
			state.dirty = !existing;
			state.status.clear();
			ECS::validateGuiAnimationGraph(*state.root);
		} catch (const std::exception& error) {
			state.status = error.what();
		}
	}

	std::string reloadSceneBindings(const std::string& path) {
		if (!RESOURCES::ServiceManager::Check<SCENE_SYSTEM::SceneManager>()) {
			return {};
		}
		auto& manager = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>();
		if (!manager.hasCurrentScene()) {
			return {};
		}
		std::string issues;
		for (const auto& object : manager.getCurrentScene().getObjects()) {
			auto component = object->getComponent<ECS::GuiAnimationComponent>();
			if (!component) {
				continue;
			}
			const auto& bindings = component->bindings();
			const auto usesGraph = [&path](const ECS::GuiAnimationBinding& binding) {
				try {
					return !binding.GraphPath.empty() && FileSystem::normalizePath(binding.GraphPath) == path;
				} catch (const std::runtime_error&) {
					return false;
				}
			};
			if (std::ranges::any_of(bindings, usesGraph) && !component->setBindings(bindings)) {
				issues += "\n" + object->getName() + " (" + component->validationError() + ")";
			}
		}
		return issues;
	}

	void startSave(GraphEditor& state) {
		try {
			const auto path = graphPath(state.pathInput);
			auto& fs = RESOURCES::ServiceManager::Get<FileSystem>();
			if (path != state.savedPath && fs.isFileExist(path)) {
				throw std::runtime_error("A file already exists at this path. Open it to edit or choose a new path.");
			}
			ECS::validateGuiAnimationGraph(*state.root);
			const auto data = serde::serialize<nlohmann::json>(*state.root);
			state.save = fs.writeTextAtomic(path, data.dump(2));
			state.pendingPath = path;
			state.status = "Saving graph...";
		} catch (const std::exception& error) {
			state.status = error.what();
		}
	}

	void finishSave(GraphEditor& state) {
		if (!state.save.valid() || state.save.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
			return;
		}
		auto save = std::exchange(state.save, {});
		try {
			save.get();
			state.documentPath = std::exchange(state.pendingPath, {});
			state.savedPath = state.documentPath;
			state.pathInput = state.documentPath;
			state.dirty = false;
			state.status = "Saved " + state.savedPath;
			const auto issues = reloadSceneBindings(state.savedPath);
			if (!issues.empty()) {
				state.status += "\nLive binding errors" + issues;
			}
		} catch (const std::exception& error) {
			state.status = error.what();
		}
	}

	void drawGraphTree(GraphEditor& state, const Node& node, NodePath& path) {
		auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (path == state.selection) {
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		if (node.Children.empty()) {
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		if (path.empty()) {
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}
		if (state.expand == path) {
			ImGui::SetNextItemOpen(true);
			state.expand.reset();
		}

		bool expanded = false;
		if (node.Kind == Kind::Tween) {
			const auto* targetName = node.Target.empty() ? "Binding target" : node.Target.c_str();
			expanded = ImGui::TreeNodeEx("node", flags, "%s to %.3g (%s)", magic_enum::enum_name(node.Property).data(), node.Value, targetName);
		}
		else if (node.Kind == Kind::Wait) {
			expanded = ImGui::TreeNodeEx("node", flags, "Wait %.3g s", node.Time);
		}
		else {
			expanded = ImGui::TreeNodeEx("node", flags, "%s", magic_enum::enum_name(node.Kind).data());
		}
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
			state.selection = path;
		}
		if (expanded && !node.Children.empty()) {
			for (std::size_t index = 0; index < node.Children.size(); ++index) {
				ImGui::PushID(static_cast<int>(index));
				path.push_back(index);
				drawGraphTree(state, node.Children[index], path);
				path.pop_back();
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
	}

	void drawEasingPreview(const Node& node) {
		std::array<float, 81> values {};
		auto curve = MATH::CURVES::makeUnitBezierCurve();
		curve.mLeftTangent = node.Control1;
		curve.mRightTangent = node.Control2;
		const auto easing = MATH::CURVES::getEasingOrLinear(node.Easing);
		try {
			for (std::size_t index = 0; index < values.size(); ++index) {
				const float time = static_cast<float>(index) / static_cast<float>(values.size() - 1);
				values[index] = node.Easing == Easing::CUSTOM ? MATH::CURVES::evaluateAtTime(curve, time) : easing(time);
			}
		} catch (const std::invalid_argument& error) {
			ImGui::TextWrapped("%s", error.what());
			return;
		}
		const auto [minimum, maximum] = std::ranges::minmax(values);
		const float bottom = std::min(minimum, 0.0f);
		const float top = std::max(maximum, 1.0f);
		if (!std::isfinite(top - bottom)) {
			ImGui::TextUnformatted("The curve exceeds the preview range.");
			return;
		}
		ImGui::PlotLines("##easingPreview", values.data(), static_cast<int>(values.size()), 0, "Eased progress", bottom, top,
			ImVec2(ImGui::GetContentRegionAvail().x, 100.0f));
	}

	bool drawNodeFields(Node& node) {
		constexpr std::array groupKinds { Kind::Sequence, Kind::Parallel };
		auto kind = node.Kind;
		bool changed = node.Children.empty() ? editEnum("Kind", kind) : editEnum<Kind>("Kind", kind, groupKinds);
		if (changed) {
			auto children = std::move(node.Children);
			node = Node {};
			node.Kind = kind;
			node.Children = std::move(children);
		}
		if (isGroup(node.Kind)) {
			ImGui::TextWrapped("%s", node.Kind == Kind::Sequence ? "Runs children in order." : "Starts all children together.");
			return changed;
		}
		changed |= ImGui::InputFloat("Duration (seconds)", &node.Time, 0.01f, 0.1f, "%.3f");
		if (node.Kind == Kind::Wait) {
			return changed;
		}

		changed |= ImGui::InputTextWithHint("Target", "Binding target", &node.Target);
		ImGui::SetItemTooltip("Leave empty to use the binding target, or enter the name of one of its direct children.");
		changed |= editEnum("Property", node.Property);
		changed |= ImGui::InputFloat("End value", &node.Value, 0.01f, 0.1f, "%.3f");
		changed |= editEnum("Easing", node.Easing);
		if (node.Easing == Easing::CUSTOM) {
			changed |= ImGui::InputFloat2("Control point 1", &node.Control1.x, "%.3f");
			changed |= ImGui::InputFloat2("Control point 2", &node.Control2.x, "%.3f");
		}
		drawEasingPreview(node);
		return changed;
	}

	void drawNodeEditor(GraphEditor& state) {
		Node* node = &*state.root;
		Node* parent = nullptr;
		for (const auto index : state.selection) {
			parent = node;
			node = &parent->Children[index];
		}

		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.52f);
		state.dirty |= drawNodeFields(*node);
		ImGui::PopItemWidth();

		if (isGroup(node->Kind)) {
			if (ImGui::Button("Add child")) {
				ImGui::OpenPopup("Child type");
			}
			std::optional<Kind> newChild;
			if (ImGui::BeginPopup("Child type")) {
				for (const auto kind : magic_enum::enum_values<Kind>()) {
					if (ImGui::MenuItem(magic_enum::enum_name(kind).data())) {
						newChild = kind;
					}
				}
				ImGui::EndPopup();
			}
			if (newChild) {
				node->Children.emplace_back().Kind = *newChild;
				state.expand = state.selection;
				state.selection.push_back(node->Children.size() - 1);
				state.dirty = true;
				return;
			}
		}
		if (!parent) {
			return;
		}

		ImGui::Spacing();
		auto& siblings = parent->Children;
		auto& index = state.selection.back();
		ImGui::BeginDisabled(index == 0);
		const bool moveUp = ImGui::Button("Move up");
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(index + 1 == siblings.size());
		const bool moveDown = ImGui::Button("Move down");
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Delete node")) {
			siblings.erase(siblings.begin() + static_cast<std::ptrdiff_t>(index));
			state.selection.pop_back();
			state.dirty = true;
		}
		else if (moveUp || moveDown) {
			const auto destination = moveUp ? index - 1 : index + 1;
			std::swap(siblings[index], siblings[destination]);
			index = destination;
			state.dirty = true;
		}
	}

	void openRequestedDocument(GraphEditor& state) {
		if (!state.requested || state.save.valid()) {
			return;
		}
		if (!state.dirty) {
			openDocument(state, *std::exchange(state.requested, {}));
			return;
		}
		if (!ImGui::IsPopupOpen("Discard graph edits?")) {
			ImGui::OpenPopup("Discard graph edits?");
		}
		if (ImGui::BeginPopupModal("Discard graph edits?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextWrapped("Open %s and discard the unsaved edits?", state.requested->path.c_str());
			if (ImGui::Button("Discard edits")) {
				openDocument(state, *std::exchange(state.requested, {}));
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep editing")) {
				state.requested.reset();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
}

void openGuiAnimationGraphEditor(const std::string& path) {
	auto& state = editor();
	state.open = true;
	state.focus = true;
	if (!path.empty()) {
		try {
			auto normalized = graphPath(path);
			if (normalized != state.documentPath) {
				if (!state.root) {
					state.pathInput = normalized;
				}
				state.requested = OpenRequest { OpenMode::ExistingOrNew, std::move(normalized) };
			}
		} catch (const std::exception& error) {
			state.status = error.what();
		}
	}
}

void drawGuiAnimationGraphEditor() {
	auto& state = editor();
	finishSave(state);
	if (!state.open) {
		return;
	}
	if (std::exchange(state.focus, false)) {
		ImGui::SetNextWindowFocus();
	}
	ImGui::SetNextWindowSize(ImVec2(820.0f, 520.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("GUI Animation Graph", &state.open, state.dirty ? ImGuiWindowFlags_UnsavedDocument : 0)) {
		ImGui::BeginDisabled(state.save.valid());
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##graph_asset_path", "/animations/gui/example.json", &state.pathInput);
		ImGui::SetItemTooltip("Virtual asset path");
		const bool hasPath = !state.pathInput.empty();
		ImGui::BeginDisabled(!hasPath);
		if (ImGui::Button("Open")) {
			state.requested = OpenRequest { OpenMode::Existing, state.pathInput };
		}
		ImGui::SameLine();
		if (ImGui::Button("New")) {
			state.requested = OpenRequest { OpenMode::New, state.pathInput };
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!state.root.has_value());
		if (ImGui::Button(state.root && state.pathInput != state.documentPath ? "Save as" : "Save")) {
			startSave(state);
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		if (!state.status.empty()) {
			ImGui::TextWrapped("%s", state.status.c_str());
		}
		openRequestedDocument(state);

		if (state.root) {
			ImGui::Separator();
			const float treeWidth = ImGui::GetContentRegionAvail().x * 0.39f;
			ImGui::BeginDisabled(state.save.valid());
			if (ImGui::BeginChild("Graph nodes", ImVec2(treeWidth, 0.0f))) {
				ImGui::SeparatorText("Nodes");
				NodePath path;
				drawGraphTree(state, *state.root, path);
			}
			ImGui::EndChild();
			ImGui::SameLine();
			if (ImGui::BeginChild("Graph node properties")) {
				ImGui::SeparatorText("Node properties");
				drawNodeEditor(state);
			}
			ImGui::EndChild();
			ImGui::EndDisabled();
		}
	}
	ImGui::End();
}
}
#endif
