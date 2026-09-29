#include "treeWindow.h"
#ifdef USE_EDITOR
#include <string>

#include "editorRender.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "coreModule/core/core.h"
#include "coreModule/ecs/object.h"
#include "sceneModule/scene.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/stringUtils.h"
#include "misc/cpp/imgui_stdlib.h"

using namespace IKIGAI::EDITOR;

std::set<IKIGAI::ECS::Object::Id::ID> searchedObjectsIds;

std::shared_ptr<IKIGAI::ECS::Object> recursiveDraw(IKIGAI::SCENE_SYSTEM::Scene& activeScene, std::shared_ptr<IKIGAI::ECS::Object> parentEntity) {
	std::shared_ptr<IKIGAI::ECS::Object> selectedNode;
	static std::optional<IKIGAI::ECS::Object::Id::ID> editNodeId;
	static std::string editBuffer;

	std::span<std::shared_ptr<IKIGAI::ECS::Object>> nodeList;
	if (parentEntity) {
		nodeList = parentEntity->getChildren();
	} else {
		nodeList = activeScene.getObjects();
	}

	unsigned i = 0u;
	for (auto node : nodeList) {
		if (!parentEntity && node->getParent()) continue;
		ImGui::PushID(("node_" + std::to_string(i)).c_str());
		ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_OpenOnArrow;

		bool isParent = node->getChildren().size();

		if (!isParent) {
			nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		} else {
			nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
		}
		const auto name = node->getName();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});

		bool isActive = node->getIsActive();
		if (ImGui::Checkbox("##active", &isActive)) {
			node->setActive(isActive);
		}
		ImGui::SameLine();

		//Visible button
		if (ImGui::Button(node->getIsVisible() ? ICON_FA_EYE : ICON_FA_EYE_SLASH)) {
			node->setVisible(!node->getIsVisible());
		}
		ImGui::SameLine();
		
		bool inSearch = searchedObjectsIds.contains(node->getID().getUniqueId());
		if (inSearch) {
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
		}
		
		bool isEditing = editNodeId && *editNodeId == node->getID().getUniqueId();
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
		} else {
			nodeIsOpen = ImGui::TreeNodeBehavior(static_cast<int>(node->getID().getUniqueId()), nodeFlags, name.c_str());
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
				editNodeId = node->getID().getUniqueId();
				editBuffer = name;
			}
		}

		if (inSearch) {
			ImGui::PopStyleColor();
		}

		ImGui::PopStyleVar();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{5.0f, 5.0f});
		if (ImGui::BeginPopupContextItem("__SCENE_TREE_CONTEXTMENU__")) {
			if (ImGui::MenuItem("Create new child")) {
				if (IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>().hasCurrentScene()) {
					auto& scene = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>().getCurrentScene();
					const auto newObject = scene.createObject("new_object");
					newObject->setParent(node);
				}
			}
			if (ImGui::MenuItem("Create new before")) {
				if (IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>().hasCurrentScene()) {
					auto& scene = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>().getCurrentScene();
					const auto newObject = scene.createObjectBefore(node->getID(), "new_object");
					newObject->setParent(node->getParent());
				}
			}
			if (ImGui::MenuItem("Create new after")) {
				if (IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>().hasCurrentScene()) {
					auto& scene = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>().getCurrentScene();
					const auto newObject = scene.createObjectAfter(node->getID(), "new_object");
					newObject->setParent(node->getParent());
				}
			}
			if (ImGui::MenuItem("Delete")) {
				if (IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>().hasCurrentScene()) {
					auto& scene = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>().getCurrentScene();
					scene.destroyObject(node);
				}
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("__SCENE_NODE_DRAG_TREE_WIN__", &node, sizeof(IKIGAI::ECS::Object*));
			ImGui::TextUnformatted(name.c_str());
			ImGui::EndDragDropSource();
		}
		ImGui::PopStyleVar();

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("__SCENE_NODE_DRAG_TREE_WIN__")) {
				auto other = *static_cast<IKIGAI::ECS::Object**>(payload->Data);
				if (node.get() != other) {
					//change node parent
					other->setParent(node);
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (ImGui::IsItemClicked()) {
			selectedNode = node;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
		ImGui::InvisibleButton("__NODE_ORDER_SE_TREE_WINT__", {-1, 5});
		ImGui::PopStyleVar();

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("__SCENE_NODE_DRAG_TREE_WIN__")) {
				auto other = *static_cast<IKIGAI::ECS::Object**>(payload->Data);
				if (node.get() != other && node->getParent()) {
					other->setParentInPos(node->getParent(), i + 1);
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
					} else if (searchType == 1) {
						found = std::to_string(obj->getIDInt()).find(lowerSearch) != std::string::npos;
					} else if (searchType == 2) {
						found = IKIGAI::UTILS::ToLower(obj->getTag()).find(lowerSearch) != std::string::npos;
					} else if (searchType == 3) {
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


		//uniqueNodeId = 0;
		auto _selectNode = recursiveDraw(scene, nullptr);
		if (_selectNode) {
			EditorRender::GlobalState.mSelectObject = _selectNode;
		}
			//	selectObj = _selectNode;
		//	if (selectObj) {
		//		auto components = IKIGAI::ECS::ComponentManager::GetInstance().getComponents(selectObj->getID());
		//		for (auto& e : components) {
		//			if (!convertComp.count(e->getName())) continue; //TODO:: write info to log
		//			auto c = convertComp[e->getName()](*e.get());
		//			rttr::type t = std::visit(
		//				[](auto& arg) {
		//					return rttr::type::get<std::remove_reference_t<decltype(*arg)>>();
		//				},
		//				c);
		//			for (auto& prop : t.get_properties()) {
		//				auto flags = prop.get_metadata(IKIGAI::UTILS::MetaInfo::FLAGS).get_value<MetaInfo::Flags>();
		//				if (flags & (MetaInfo::USE_IN_EDITOR_ANIMATION)) {
		//					SequencerItemTypeNames.push_back(AnimationLineInfo{
		//						selectObj->getID(), selectObj->getName(),
		//						e->getName(), std::string(prop.get_name()), 0.0f, c});
		//				}
		//			}
		//		}
		//	}
		//}
	}
	ImGui::End();
	
}


void TreeWindow::draw() {
	drawNodeTree();
}
#endif
