#include "treeWindow.h"
#include "prefabEditor.h"
#ifdef USE_EDITOR
#include <string>

#include "IconsFontAwesome5.h"
#include "coreModule/core/core.h"
#include "coreModule/ecs/object.h"
#include "editorRender.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "sceneModule/scene.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/stringUtils.h"

using namespace IKIGAI::EDITOR;

std::set<IKIGAI::ECS::Object::Id::ID> searchedObjectsIds;

std::shared_ptr<IKIGAI::ECS::Object> recursiveDraw(IKIGAI::SCENE_SYSTEM::Scene& activeScene, std::shared_ptr<IKIGAI::ECS::Object> parentEntity) {
	std::shared_ptr<IKIGAI::ECS::Object> selectedNode;
	static std::optional<IKIGAI::ECS::Object::Id::ID> editNodeId;
	static std::string editBuffer;

	std::span<std::shared_ptr<IKIGAI::ECS::Object>> nodeList;
	if (parentEntity) {
		nodeList = parentEntity->getChildren();
	}
	else {
		nodeList = activeScene.getObjects();
	}

	unsigned i = 0u;
	for (auto node : nodeList) {
		if (!parentEntity && node->getParent()) {
			continue;
		}
		ImGui::PushID(("node_" + std::to_string(i)).c_str());
		ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow;
		if (EditorRender::GlobalState.mSelectObject == node) {
			nodeFlags |= ImGuiTreeNodeFlags_Selected;
		}

		bool isParent = node->getChildren().size();

		if (!isParent) {
			nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		else {
			nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
		}
		const auto name = node->getName() + (activeScene.isPrefabRoot(node->getIDInt()) ? " [Prefab]" : "");
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f });

		ImGui::BeginDisabled(PREFABS::Locked(*node));
		bool isActive = node->getIsSelfActive();
		if (ImGui::Checkbox("##active", &isActive)) {
			node->setActive(isActive);
		}
		ImGui::SameLine();

		// Visible button
		if (ImGui::Button(node->getIsSelfVisible() ? ICON_FA_EYE : ICON_FA_EYE_SLASH)) {
			node->setVisible(!node->getIsSelfVisible());
		}
		ImGui::SameLine();
		ImGui::EndDisabled();

		bool inSearch = searchedObjectsIds.contains(node->getID().getUniqueId());
		if (inSearch) {
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
		}

		bool isEditing = !PREFABS::Locked(*node) && editNodeId && *editNodeId == node->getID().getUniqueId();
		bool nodeIsOpen = false;

		if (isEditing) {
			nodeIsOpen = ImGui::TreeNodeBehavior(static_cast<int>(node->getID().getUniqueId()), nodeFlags, "###hiddenNodeName");
			ImGui::SameLine();
			ImGui::PushItemWidth(-1);
			if (ImGui::InputText("##rename", &editBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
				node->setName(editBuffer);
				editNodeId.reset();
			}
			ImGui::PopItemWidth();
			if (!ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
				editNodeId.reset();
			}
		}
		else {
			nodeIsOpen = ImGui::TreeNodeBehavior(static_cast<int>(node->getID().getUniqueId()), nodeFlags, name.c_str());
			// Capture the row before popup/drag widgets replace its hover state.
			if (ImGui::IsItemClicked()) {
				selectedNode = node;
			}
			if (!PREFABS::Locked(*node) && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
				editNodeId = node->getID().getUniqueId();
				editBuffer = node->getName();
			}
		}

		if (inSearch) {
			ImGui::PopStyleColor();
		}

		ImGui::PopStyleVar();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 { 5.0f, 5.0f });
		if (ImGui::BeginPopupContextItem("__SCENE_TREE_CONTEXTMENU__")) {
			ImGui::BeginDisabled(PREFABS::Member(*node));
			if (ImGui::MenuItem("Create new child")) {
				PREFABS::deferred = [node] {
					auto object = PREFABS::Manager().getCurrentScene().createObject("new_object");
					object->setParent(node);
				};
			}
			ImGui::EndDisabled();
			ImGui::BeginDisabled(PREFABS::Locked(*node));
			if (ImGui::MenuItem("Create new before")) {
				PREFABS::deferred = [node] {
					auto object = PREFABS::Manager().getCurrentScene().createObjectBefore(node->getID(), "new_object");
					if (node->getParent()) {
						object->setParent(node->getParent());
					}
				};
			}
			if (ImGui::MenuItem("Create new after")) {
				PREFABS::deferred = [node] {
					auto object = PREFABS::Manager().getCurrentScene().createObjectAfter(node->getID(), "new_object");
					if (node->getParent()) {
						object->setParent(node->getParent());
					}
				};
			}
			if (ImGui::MenuItem("Delete")) {
				const int id = node->getIDInt();
				PREFABS::deferred = [id] { PREFABS::Delete(id); };
			}
			ImGui::EndDisabled();
			ImGui::Separator();
			if (!PREFABS::Member(*node) && ImGui::MenuItem("Create Prefab...")) {
				PREFABS::Open(1, node->getIDInt());
			}
			if (!PREFABS::Member(*node) && ImGui::MenuItem("Instantiate Prefab...")) {
				PREFABS::Open(2, node->getIDInt());
			}
			if (activeScene.isPrefabRoot(node->getIDInt()) && ImGui::MenuItem("Unpack Prefab")) {
				const int id = node->getIDInt();
				PREFABS::deferred = [id] { PREFABS::Unpack(id); };
			}

			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 { 10.0f, 10.0f });
		if (!PREFABS::Locked(*node) && ImGui::BeginDragDropSource()) {
			const int id = node->getIDInt();
			ImGui::SetDragDropPayload("__SCENE_NODE_DRAG_TREE_WIN__", &id, sizeof(id));
			ImGui::TextUnformatted(name.c_str());
			ImGui::EndDragDropSource();
		}
		ImGui::PopStyleVar();

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("__SCENE_NODE_DRAG_TREE_WIN__")) {
				auto other = activeScene.findObjectByID(IKIGAI::ECS::Entity(IKIGAI::ECS::Entity::ID(*static_cast<const int*>(payload->Data))));
				if (other && node != other && !PREFABS::Member(*node) && !PREFABS::Locked(*other)) {
					bool cycle = false;
					for (auto p = node; p; p = p->getParent()) {
						if (p == other) {
							cycle = true;
						}
					}
					if (!cycle) {
						PREFABS::deferred = [other, node] { PREFABS::Move(other->getIDInt(), node->getIDInt()); };
					}
				}
			}
			if (const auto* payload = ImGui::AcceptDragDropPayload("DND_PREFAB_PATH")) {
				if (!PREFABS::Member(*node)) {
					const std::string path(static_cast<const char*>(payload->Data));
					const int parent = node->getIDInt();
					PREFABS::deferred = [path, parent] { PREFABS::Instantiate(path, parent); };
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f });
		ImGui::InvisibleButton("__NODE_ORDER_SE_TREE_WINT__", { -1, 5 });
		ImGui::PopStyleVar();

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("__SCENE_NODE_DRAG_TREE_WIN__")) {
				auto other = activeScene.findObjectByID(IKIGAI::ECS::Entity(IKIGAI::ECS::Entity::ID(*static_cast<const int*>(payload->Data))));
				if (other && node != other && node->getParent() && !PREFABS::Member(*node->getParent()) && !PREFABS::Locked(*other)) {
					auto parent = node->getParent();
					bool cycle = false;
					for (auto p = parent; p; p = p->getParent()) {
						if (p == other) {
							cycle = true;
						}
					}
					if (!cycle) {
						PREFABS::deferred = [other, parent, i] { PREFABS::Move(other->getIDInt(), parent->getIDInt(), i + 1); };
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (isParent && nodeIsOpen) {
			auto childClickedEntity = recursiveDraw(activeScene, node);
			if (!selectedNode) {
				selectedNode = childClickedEntity;
			}
			ImGui::TreePop();
		}
		i++;
		ImGui::PopID();
	}
	return selectedNode;
}
void TreeWindow::drawNodeTree() {

	auto& sceneManager = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>();
	if (!sceneManager.hasCurrentScene()) {
		return;
	}
	auto& scene = sceneManager.getCurrentScene();

	if (ImGui::Begin("Scene Hierarchy", &mIsOpen)) {
		ImGui::Text("Search: ");
		ImGui::SameLine();

		static int searchType = 0;
		const char* searchTypes[] = { "Name", "ID", "Tag", "Component" };
		ImGui::SetNextItemWidth(100.0f);
		ImGui::Combo("##searchType", &searchType, searchTypes, IM_ARRAYSIZE(searchTypes));
		ImGui::SameLine();

		static std::string searchInActors;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 65.0f);
		bool searchChanged = ImGui::InputText("##search_in_actors", &searchInActors);
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_SYNC)) {
			searchChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_TIMES)) {
			searchInActors.clear();
			searchChanged = true;
		}

		if (searchChanged) {
			searchedObjectsIds.clear();
			if (!searchInActors.empty()) {
				for (auto obj : scene.getObjects()) {
					ImGui::TreeNodeSetOpen(static_cast<int>(obj->getID().getUniqueId()), false);
				}
				const auto lowerSearch = IKIGAI::UTILS::ToLower(searchInActors);
				for (auto obj : scene.getObjects()) {
					bool found = false;
					if (searchType == 0) {
						found = IKIGAI::UTILS::ToLower(obj->getName()).find(lowerSearch) != std::string::npos;
					}
					else if (searchType == 1) {
						found = std::to_string(obj->getIDInt()).find(lowerSearch) != std::string::npos;
					}
					else if (searchType == 2) {
						found = IKIGAI::UTILS::ToLower(obj->getTag()).find(lowerSearch) != std::string::npos;
					}
					else if (searchType == 3) {
						for (const auto& [typeIndex, compWeak] : obj->getComponents()) {
							if (auto comp = compWeak.get()) {
								if (IKIGAI::UTILS::ToLower(comp->getName()).find(lowerSearch) != std::string::npos) {
									found = true;
									break;
								}
							}
						}
					}

					if (found) {
						searchedObjectsIds.insert(obj->getID().getUniqueId());
						auto expandAll = [](auto& self, std::shared_ptr<IKIGAI::ECS::Object> obj) -> void {
							ImGui::TreeNodeSetOpen(static_cast<int>(obj->getID().getUniqueId()), true);
							if (obj->getParent()) {
								self(self, obj->getParent());
							}
						};
						if (obj->getParent()) {
							expandAll(expandAll, obj->getParent());
						}
					}
				}
			}
		}

		// uniqueNodeId = 0;
		if (ImGui::Button("Instantiate Prefab...")) {
			PREFABS::Open(2);
		}
		auto _selectNode = recursiveDraw(scene, nullptr);
		ImGui::InvisibleButton("##prefab_root_drop", ImVec2(ImGui::GetContentRegionAvail().x, 36));
		if (ImGui::BeginDragDropTarget()) {
			if (const auto* payload = ImGui::AcceptDragDropPayload("DND_PREFAB_PATH")) {
				const std::string path(static_cast<const char*>(payload->Data));
				PREFABS::deferred = [path] { PREFABS::Instantiate(path); };
			}
			ImGui::EndDragDropTarget();
		}
		PREFABS::Draw();
		if (_selectNode && _selectNode->isAlive()) {
			EditorRender::GlobalState.mSelectObject = _selectNode;
		}
	}
	ImGui::End();
}

void TreeWindow::draw() {
	drawNodeTree();
}
#endif
