#pragma once
#ifdef USE_EDITOR
#include "editorRender.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "sceneModule/sceneManager.h"
#include <algorithm>
#include <utility>

namespace IKIGAI::EDITOR::PREFABS {
inline std::string error;
inline SCENE_SYSTEM::SceneManager& Manager() {
	return RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>();
}
inline bool Member(const ECS::Object& object) {
	return Manager().getCurrentScene().prefabFor(object.getIDInt());
}
inline bool Locked(const ECS::Object& object) {
	return Member(object) && !Manager().getCurrentScene().isPrefabRoot(object.getIDInt());
}
template <class F> void Try(F&& action) {
	try {
		action();
		error.clear();
	} catch (const std::exception& e) {
		error = e.what();
	}
}
class Command final : public ICommand {
	SCENE_SYSTEM::Scene* scene;
	std::weak_ptr<void> lifetime;
	std::string name;
	std::function<void(SCENE_SYSTEM::Scene&)> apply, revert;
	bool alreadyApplied;
	void Run(const std::function<void(SCENE_SYSTEM::Scene&)>& action) {
		if (lifetime.expired() || !Manager().hasCurrentScene() || &Manager().getCurrentScene() != scene) {
			return;
		}
		if (Manager().isEditingLocked()) {
			throw std::runtime_error("Wait for the scene operation to finish");
		}
		action(*scene);
	}

public:
	Command(std::string name, std::function<void(SCENE_SYSTEM::Scene&)> apply, std::function<void(SCENE_SYSTEM::Scene&)> revert, bool applied = false)
		: scene(&Manager().getCurrentScene())
		, lifetime(scene->lifetime)
		, name(std::move(name))
		, apply(std::move(apply))
		, revert(std::move(revert))
		, alreadyApplied(applied) { }
	void execute() override {
		if (alreadyApplied) {
			alreadyApplied = false;
			return;
		}
		Run(apply);
	}
	void undo() override {
		Run(revert);
	}
	std::string getName() const override {
		return name;
	}
};
inline SCENE_SYSTEM::DOCUMENT::Expanded Snapshot(int rootId) {
	auto& scene = Manager().getCurrentScene();
	SCENE_SYSTEM::DOCUMENT::Expanded result;
	auto root = scene.findObjectByID(ECS::Entity(ECS::Entity::ID(rootId)));
	if (!root) {
		throw std::runtime_error("Object no longer exists");
	}
	std::set<int> ids;
	std::vector<std::shared_ptr<ECS::Object>> stack { root };
	while (!stack.empty()) {
		auto object = stack.back();
		stack.pop_back();
		if (!ids.insert(object->getIDInt()).second) {
			throw std::runtime_error("Scene cycle");
		}
		for (auto child : object->getChildren()) {
			stack.push_back(child);
		}
	}
	for (auto object : scene.serializeObjects()) {
		if (ids.contains(SCENE_SYSTEM::DOCUMENT::Id(object.at("Id")))) {
			result.objects.push_back(std::move(object));
		}
	}
	for (const auto& instance : scene.prefabInstances) {
		if (ids.contains(SCENE_SYSTEM::DOCUMENT::Id(instance.entry.at("Id")))) {
			result.instances.push_back(instance);
		}
	}
	return result;
}
inline void RecordCreation(int rootId) {
	auto snapshot = Snapshot(rootId);
	EditorRender::GlobalState.mCommandHistory.addCommand(std::make_unique<Command>(
		"Instantiate Prefab", [snapshot](auto& scene) { EditorRender::GlobalState.mSelectObject = scene.appendDocument(snapshot); },
		[rootId](auto& scene) {
			scene.destroySubtree(rootId);
			EditorRender::GlobalState.mSelectObject.reset();
		},
		true));
}
inline void Unpack(int rootId) {
	const auto* source = Manager().getCurrentScene().prefabFor(rootId);
	if (!source) {
		return;
	}
	auto instance = *source;
	EditorRender::GlobalState.mCommandHistory.addCommand(std::make_unique<Command>(
		"Unpack Prefab", [rootId](auto& scene) { scene.unpackPrefab(rootId); },
		[instance](auto& scene) { scene.prefabInstances.push_back(instance); }));
}
inline void Delete(int rootId) {
	auto snapshot = Snapshot(rootId);
	auto& scene = Manager().getCurrentScene();
	auto root = scene.findObjectByID(ECS::Entity(ECS::Entity::ID(rootId)));
	const auto objects = scene.getObjects();
	const auto objectIndex = static_cast<std::size_t>(std::find(objects.begin(), objects.end(), root) - objects.begin());
	int siblingIndex = 0;
	if (auto parent = root->getParent()) {
		auto children = parent->getChildren();
		siblingIndex = static_cast<int>(std::find(children.begin(), children.end(), root) - children.begin());
	}
	EditorRender::GlobalState.mCommandHistory.addCommand(std::make_unique<Command>(
		"Delete Subtree",
		[rootId](auto& scene) {
			scene.destroySubtree(rootId);
			EditorRender::GlobalState.mSelectObject.reset();
		},
		[snapshot, rootId, objectIndex, siblingIndex](auto& scene) {
			scene.appendDocument(snapshot);
			scene.restoreObjectPosition(rootId, objectIndex, siblingIndex);
			EditorRender::GlobalState.mSelectObject = scene.findObjectByID(ECS::Entity(ECS::Entity::ID(rootId)));
		}));
}
inline void Move(int id, int parentId, int position = -1) {
	auto& scene = Manager().getCurrentScene();
	auto object = scene.findObjectByID(ECS::Entity(ECS::Entity::ID(id)));
	const int oldParent = object->getParentId();
	auto all = scene.getObjects();
	const auto objectIndex = static_cast<std::size_t>(std::find(all.begin(), all.end(), object) - all.begin());
	int oldPosition = 0;
	if (auto parent = object->getParent()) {
		auto children = parent->getChildren();
		oldPosition = static_cast<int>(std::find(children.begin(), children.end(), object) - children.begin());
	}
	auto move = [id](auto& scene, int parentId, int index) {
		auto object = scene.findObjectByID(ECS::Entity(ECS::Entity::ID(id)));
		if (!object) {
			throw std::runtime_error("Moved object no longer exists");
		}
		if (parentId <= 0) {
			object->detachFromParent();
		}
		else {
			auto parent = scene.findObjectByID(ECS::Entity(ECS::Entity::ID(parentId)));
			if (!parent) {
				throw std::runtime_error("Parent no longer exists");
			}
			object->detachFromParent();
			if (index < 0) {
				object->setParent(parent);
			}
			else {
				object->setParentInPos(parent, std::min(index, static_cast<int>(parent->getChildren().size())));
			}
		}
	};
	EditorRender::GlobalState.mCommandHistory.addCommand(std::make_unique<Command>(
		"Move Subtree", [move, parentId, position](auto& scene) { move(scene, parentId, position); },
		[move, oldParent, oldPosition, objectIndex, id](auto& scene) {
			move(scene, oldParent, oldPosition);
			scene.restoreObjectPosition(id, objectIndex, oldPosition);
		}));
}
inline int dialogMode = 0, dialogRoot = 0;
inline std::string dialogPath = "/prefabs/new.prefab.json";
inline bool openDialog = false;
inline std::weak_ptr<void> dialogLifetime;
inline SCENE_SYSTEM::SceneLoadHandle pending;
inline std::weak_ptr<void> pendingLifetime;
inline std::function<void()> deferred;
inline void Open(int mode, int root = -1) {
	dialogMode = mode;
	dialogRoot = root;
	openDialog = true;
	dialogLifetime = Manager().getCurrentScene().lifetime;
	error.clear();
}
inline void Instantiate(const std::string& path, int parent = -1) {
	pending = Manager().instantiatePrefabAsync(path, parent);
	pendingLifetime = Manager().getCurrentScene().lifetime;
}
inline void CompletePending() {
	if (pending && pending.done()) {
		if (pending.status() == SCENE_SYSTEM::SceneLoadStatus::Completed && !pendingLifetime.expired()) {
			Try([&] {
				const int root = pending.resultRootId();
				RecordCreation(root);
				EditorRender::GlobalState.mSelectObject = Manager().getCurrentScene().findObjectByID(ECS::Entity(ECS::Entity::ID(root)));
			});
		}
		else {
			error = pending.error();
		}
		pending = {};
	}
}
inline void Draw() {
	if (deferred) {
		auto action = std::exchange(deferred, {});
		Try(action);
	}
	CompletePending();
	if (openDialog) {
		ImGui::OpenPopup("Prefab file");
		openDialog = false;
	}
	if (ImGui::BeginPopupModal("Prefab file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted(dialogMode == 1 ? "Create prefab from selected subtree" : "Instantiate prefab");
		ImGui::SetNextItemWidth(420);
		ImGui::InputText("VFS path", &dialogPath);
		if (ImGui::Button("OK")) {
			Try([&] {
				if (dialogLifetime.expired()) {
					throw std::runtime_error("Scene changed; open the dialog again");
				}
				if (dialogMode == 1) {
					auto previous = Snapshot(dialogRoot).instances;
					Manager().createPrefab(dialogRoot, dialogPath);
					auto instance = *Manager().getCurrentScene().prefabFor(dialogRoot);
					const int root = dialogRoot;
					EditorRender::GlobalState.mCommandHistory.addCommand(std::make_unique<Command>(
						"Create Prefab",
						[instance, previous](auto& scene) {
							for (const auto& nested : previous) {
								scene.unpackPrefab(SCENE_SYSTEM::DOCUMENT::Id(nested.entry.at("Id")));
							}
							scene.prefabInstances.push_back(instance);
						},
						[root, previous](auto& scene) {
							scene.unpackPrefab(root);
							scene.prefabInstances.insert(scene.prefabInstances.end(), previous.begin(), previous.end());
						},
						true));
				}
				else {
					Instantiate(dialogPath, dialogRoot);
				}
				ImGui::CloseCurrentPopup();
			});
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			ImGui::CloseCurrentPopup();
		}
		if (!error.empty()) {
			ImGui::TextWrapped("%s", error.c_str());
		}
		ImGui::EndPopup();
	}
	if (!error.empty()) {
		ImGui::TextWrapped("Prefab: %s", error.c_str());
		if (ImGui::SmallButton("Dismiss prefab error")) {
			error.clear();
		}
	}
}
}
#endif
