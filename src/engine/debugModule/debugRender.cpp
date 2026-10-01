#include "debugRender.h"
#ifdef USE_EDITOR_
#include <memory>


#include <renderModule/backends/interface/driverInterface.h>
#include "utilsModule/imguiHelper/imguiBackend/imguiBackend.h"

#ifdef VULKAN_BACKEND
#include <vulkan/vulkan_core.h>
#include <../../3rd/imgui/imgui/imgui_impl_vulkan.h>
#include <renderModule/backends/vk/driverVk.h>
#include <renderModule/backends/vk/textureVk.h>
#include <renderModule/backends/vk/frameBufferVk.h>
#endif

#include <renderModule/gameRenderer.h>
#include <../../3rd/imgui/imgui/imgui.h>
#include <../../3rd/imgui/imgui/imgui_impl_glfw.h>

#ifdef OPENGL_BACKEND
#include <../../3rd/imgui/imgui/imgui_impl_opengl3.h>
#include <renderModule/backends/gl/materialGl.h>
#endif

#ifdef DX12_BACKEND
#include <renderModule/backends/dx12/textureDx12.h>
#include <renderModule/backends/dx12/driverDx12.h>
#include "renderModule/gameRendererDx12.h"
#include <../../3rd/imgui/imgui/imgui_impl_dx12.h>
#include <../../3rd/imgui/imgui/imgui_impl_win32.h>
#endif
//#include <SomeLogger.h>
#include <stack>
#include <unordered_set>
//#include <rttr/enumeration.h>
//#include <rttr/type.h>

#include <windowModule/window/window.h>
#include <windowModule/inputManager/inputManager.h>
//#include <utilsModule/imguiWidgets/ImGuizmo.h>
#include <../../3rd/imgui/IconFont/IconsFontAwesome5.h>

//#include "coreModule/resourceManager/materialManager.h"
//#include "coreModule/resourceManager/textureManager.h"
#include "utilsModule/animation.h"
#include "utilsModule/assertion.h"
//#include "utilsModule/imguiWidgets/ImGuiFileBrowser.h"
//#include "utilsModule/imguiWidgets/imgui_widget_flamegraph.h"
//#include "utilsModule/imguiWidgets/TextEditor.h"
#include "utilsModule/profiler/profiler.h"
using namespace IKIGAI;
using namespace IKIGAI::DEBUG;

#include "../../3rd/imgui/imgui/imgui_internal.h"
#include "../../3rd/imgui/imgui/misc/cpp/imgui_stdlib.h"
#include <coreModule/ecs/object.h>
#include <coreModule/gui/guiObject.h>
#include <sceneModule/sceneManager.h>
#include <coreModule/core/core.h>
#include <utilsModule/time/time.h>
#include <coreModule/ecs/components/scriptComponent.h>

struct MovableChildData
{
	ImVec2 m_pressPosition = ImVec2(0, 0);
	ImVec2 m_position = ImVec2(0, 0);
	bool   m_isDragging = false;
	bool   m_initialPositionSet = false;
};

int uniqueNodeId = 0;
std::shared_ptr<IKIGAI::ECS::Object> selectObj;
std::shared_ptr<IKIGAI::GUI::GuiObject> selectObjGui;
std::map<std::string, MovableChildData>       m_movableChildData;
std::string dndStringPayload;

std::map<std::string, bool> popupStates;

template <typename Key, typename Value, std::size_t Size>
struct ConstexprMap {
	std::array<std::pair<Key, Value>, Size> data;
	[[nodiscard]] constexpr Value at(const Key& key) const {
		const auto itr = std::find_if(begin(data), end(data), [&key](const auto& v) { return v.first == key; });
		if (itr != end(data)) {
			return itr->second;
		}
		throw std::range_error("Not Found");
	}
};

static constexpr std::array<std::pair<const char*, int>, 1> nameToIdInVariant = {
	{{ "Transform", 0}}
};

int getComponentTypeIdInVariant(const char* key) {
	static constexpr auto map =
		ConstexprMap<const char*, int, nameToIdInVariant.size()>{ {nameToIdInVariant} };
	return map.at(key);
}

using ComponentType = std::variant<ECS::TransformComponent*, ECS::AmbientLight*, ECS::AmbientSphereLight*,
	ECS::DirectionalLight*, ECS::PointLight*, ECS::SpotLight*, ECS::CameraComponent*,
	ECS::AudioListenerComponent*, ECS::AudioComponent*>;
using ComponentName = std::string;
using PromName = std::string;
using PropValue = std::variant<float, int, bool, std::string, MATHGL::Vector2f, MATHGL::Vector2u, MATHGL::Vector2i, MATHGL::Vector3, MATHGL::Vector4>;

struct AnimationLineInfo {
	ObjectId<ECS::Object> id;
	std::string name;
	std::string componentName;
	std::string propName;

	PropValue value;

	ComponentType component;
};
std::vector<AnimationLineInfo> SequencerItemTypeNames{};

std::set<ECS::Object::Id_> searchedObjectsIds;

std::shared_ptr<RENDER::MaterialInterface> editMaterial;


std::function<void(std::string)> fileChooserCb;
std::function<std::string()> fileFormatsCb;

	std::unordered_map<std::string, std::shared_ptr<RENDER::TextureInterface>> textureCache;

std::shared_ptr<IKIGAI::ECS::Object> recursiveDraw(IKIGAI::SCENE_SYSTEM::Scene& activeScene, std::shared_ptr<IKIGAI::ECS::Object> parentEntity) {
	std::shared_ptr<IKIGAI::ECS::Object> selectedNode;

	std::span<std::shared_ptr<IKIGAI::ECS::Object>> nodeList;

	if (parentEntity) {
		nodeList = parentEntity->getChildren();
	}
	else {
		nodeList = activeScene.getObjects();
	}
	auto i = 0u;
	for (auto node : nodeList) {
		ImGui::PushID(("node_" + std::to_string(i)).c_str());
		ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_OpenOnArrow;

		bool isParent = node->getChildren().size();

		if (!isParent) {
			nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		else {
			nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
		}
		const auto name = node->getName();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f });

		//Visable button
		if (ImGui::Button(node->getIsActive() ? ICON_FA_EYE : ICON_FA_EYE_SLASH)) {
			node->setActive(!node->getIsActive());
		}
		ImGui::SameLine();
		bool inSearch = searchedObjectsIds.contains(node->getID());
		if (inSearch) {
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
		}
		const bool nodeIsOpen = ImGui::TreeNodeBehavior(static_cast<int>(node->getID()), nodeFlags, name.c_str());
		if (inSearch) {
			ImGui::PopStyleColor();
		}

		ImGui::PopStyleVar();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 5.0f, 5.0f });
		if (ImGui::BeginPopupContextItem("__SCENE_TREE_CONTEXTMENU__")) {
			if (ImGui::MenuItem("Create new child")) {
				if (RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().hasCurrentScene()) {
					auto& scene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
					const auto newObject = scene.createObject("new_object");
					newObject->setParent(node);
				}
			}
			if (ImGui::MenuItem("Create new before")) {
				if (RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().hasCurrentScene()) {
					auto& scene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
					const auto newObject = scene.createObjectBefore(node->getID(), "new_object");
					newObject->setParent(node->getParent());
				}
			}
			if (ImGui::MenuItem("Create new after")) {
				if (RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().hasCurrentScene()) {
					auto& scene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
					const auto newObject = scene.createObjectAfter(node->getID(), "new_object");
					newObject->setParent(node->getParent());
				}
			}
			if (ImGui::MenuItem("Delete")) {
				if (RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().hasCurrentScene()) {
					auto& scene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
					scene.destroyObject(node);
				}
			}

			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 10.0f, 10.0f });
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("__SCENE_NODE_DRAG__", &node, sizeof(IKIGAI::ECS::Object*));
			ImGui::TextUnformatted(name.c_str());
			ImGui::EndDragDropSource();
		}
		ImGui::PopStyleVar();

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("__SCENE_NODE_DRAG__")) {
				auto other = *static_cast<IKIGAI::ECS::Object**>(payload->Data);

				//change node parent
				auto parent = other->getParent();
				if (parent) {
					//parent->removeChild(other);
					//other->setParent(nullptr);
				}
				//node.second->addChild(other);
			}
			ImGui::EndDragDropTarget();
		}

		if (ImGui::IsItemClicked()) {
			selectedNode = node;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f });
		ImGui::InvisibleButton("__NODE_ORDER_SET__", { -1, 5 });
		ImGui::PopStyleVar();

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("__NODE_ORDER_SET__")) {
				auto other = *static_cast<IKIGAI::ECS::Object**>(payload->Data);
				if (node.get() != other && node->getParent()) {
					auto parent = other->getParent();
					if (parent) {
						//parent->removeChild(other);
						//other->setParent(nullptr);
					}
					//node.second->getParent()->addChild(other->getID(), other, i + 1);
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
void drawNodeTree(IKIGAI::CORE_SYSTEM::Core& core) {
	static bool isobjTreeOpen = true;
	static std::unordered_map<std::string, std::function<ComponentType(ECS::Component&)>> convertComp = {
		{ "Transform", [](ECS::Component& c) { return &static_cast<ECS::TransformComponent&>(c); } }
	};

	if (core.sceneManager->hasCurrentScene()) {
		auto& scene = core.sceneManager->getCurrentScene();

		if (ImGui::Begin("Scene Hierarchy", &isobjTreeOpen)) {
			ImGui::Text("Search: ");
			ImGui::SameLine();
			static std::string searchInActors;
			if (ImGui::InputText("##search_in_actors", &searchInActors)) {
				searchedObjectsIds.clear();
				if (!searchInActors.empty()) {
					for (auto obj : scene.getObjects()) {
						ImGui::TreeNodeSetOpen(static_cast<int>(obj->getID()), false);
					}
					for (auto obj : scene.getObjects()) {
						const auto foundInName = UTILS::toLower(obj->getName()).find(UTILS::toLower(searchInActors)) != std::string::npos;
						const auto foundInTag = UTILS::toLower(obj->getTag()).find(UTILS::toLower(searchInActors)) != std::string::npos;
						if (foundInName || foundInTag) {
							searchedObjectsIds.insert(obj->getID());
							std::function<void(std::shared_ptr<ECS::Object>)> expandAll;
							expandAll = [&expandAll](std::shared_ptr<ECS::Object> obj) {
								ImGui::TreeNodeSetOpen(static_cast<int>(obj->getID()), true);
								if (obj->getParent()) {
									expandAll(obj->getParent());
								}
							};
							if (obj->getParent()) {
								expandAll(obj->getParent());
							}
						}
					}
				}
			}


			uniqueNodeId = 0;
			auto _selectNode = recursiveDraw(scene, nullptr);
			if (_selectNode) {
				selectObj = _selectNode;
				if (selectObj) {
					auto components = ECS::ComponentManager::GetInstance().getComponents(selectObj->getID());
					for (auto& e : components) {
						if (!convertComp.count(e->getName())) continue; //TODO:: write info to log
						auto c = convertComp[e->getName()](*e.get());
						rttr::type t = std::visit(
							[](auto& arg) {
								return rttr::type::get<std::remove_reference_t<decltype(*arg)>>();
							},
							c);
						for (auto& prop : t.get_properties()) {
							auto flags = prop.get_metadata(MetaInfo::FLAGS).get_value<MetaInfo::Flags>();
							if (flags & (MetaInfo::USE_IN_EDITOR_ANIMATION)) {
								SequencerItemTypeNames.push_back(AnimationLineInfo{ 
									selectObj->getID(), selectObj->getName(),
									e->getName(), std::string(prop.get_name()), 0.0f, c});
							}
						}
					}
				}
			}
		}
		ImGui::End();
	}
}
#ifdef OPENGL_BACKEND
std::optional<unsigned> ImGuiLoadTextureFromFileGl(const std::string& filename) {
	// Load from file
	int image_width = 0;
	int image_height = 0;
	unsigned char* image_data = RESOURCES::stbiLoad(filename.c_str(), &image_width, &image_height, NULL, 4);
	if (image_data == NULL)
		return std::nullopt;

	// Create a OpenGL texture identifier
	GLuint image_texture;
	glGenTextures(1, &image_texture);
	glBindTexture(GL_TEXTURE_2D, image_texture);

	// Setup filtering parameters for display
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

	// Upload pixels into texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
	RESOURCES::stbiImageFree(image_data);

	return image_texture;
}
#endif

#ifdef VULKAN_BACKEND
std::shared_ptr<RENDER::TextureVk> ImGuiLoadTextureFromFileVk(const std::string& filename) {
	// Load from file
	auto texture = RENDER::TextureVk::create(filename);
	return texture;
}
#endif

#ifdef DX12_BACKEND
std::shared_ptr<RENDER::TextureDx12> ImGuiLoadTextureFromFileDx12(const std::string& filename) {
	// Load from file
	auto texture = RENDER::TextureDx12::Create(filename);
	return texture;
}
#endif
struct File {
	enum class FileType {
		DIR,
		IMAGE,
		MODEL,
		TEXT,
		FONT,
		MATERIAL
	};
	FileType type;
	std::filesystem::path path;
	std::filesystem::path file;
	std::string ext;
	std::list<File> files;

	int uid = 0;
	File(std::filesystem::path path): path(path) {
		type = File::getFileType(path);
		ext = getExtension(path);
		file = path.filename();

		static int newid= 1;
		uid = newid++;
	}
private:
	inline static std::set<std::string> imageExt = { ".png", ".jpg", ".jpeg", ".tga", ".dds"};
	inline static std::set<std::string> objExt = { ".obj", ".fbx", ".dae" };
	inline static std::set<std::string> fontExt = { ".ttf" };
	inline static std::set<std::string> materialExt = { ".mat" };
public:
	static FileType getFileType(const std::filesystem::path& path) {
		auto ext = std::filesystem::is_directory(path) ? "dir":
			path.has_extension() ? path.extension().string() : "";
		ext = UTILS::toLower(ext);
		if (ext == "dir") {
			return FileType::DIR;
		}
		if (imageExt.contains(ext)) {
			return FileType::IMAGE;
		}
		if (fontExt.contains(ext)) {
			return FileType::TEXT;
		}
		if (materialExt.contains(ext)) {
			return FileType::MATERIAL;
		}
		return FileType::TEXT;
	}
	static std::string getExtension(const std::filesystem::path& path) {
		auto ext = std::filesystem::is_directory(path) ? "dir" :
			path.has_extension() ? path.extension().string() : "";
		ext = UTILS::toLower(ext);
		return ext;
	}
};


struct FileBrowser {
	std::string mPath;
	std::string mSelectedFolderPath;

	std::stack<std::string> history;
	int elementSize = 100;
	FileBrowser(const std::string& path): mPath(path), mSelectedFolderPath(path), root(path) {
#ifdef OPENGL_BACKEND
		textureCache["__image__"] = ImGuiLoadTextureFromFileGl(UTILS::getRealPath("Textures/Debug/Editor/image-white.png")).value();
		textureCache["__dir__"] = ImGuiLoadTextureFromFileGl(UTILS::getRealPath("Textures/Debug/Editor/folder-white.png")).value();
		textureCache["__file__"] = ImGuiLoadTextureFromFileGl(UTILS::getRealPath("Textures/Debug/Editor/file-white.png")).value();
		textureCache["__font__"] = ImGuiLoadTextureFromFileGl(UTILS::getRealPath("Textures/Debug/Editor/font-white.png")).value();
		textureCache["__object__"] = ImGuiLoadTextureFromFileGl(UTILS::getRealPath("Textures/Debug/Editor/cube-white.png")).value();
#endif
#ifdef VULKAN_BACKEND
		textureCache["__image__"] = ImGuiLoadTextureFromFileVk(UTILS::getRealPath("Textures/Debug/Editor/image-white.png"));
		textureCache["__dir__"] = ImGuiLoadTextureFromFileVk(UTILS::getRealPath("Textures/Debug/Editor/folder-white.png"));
		textureCache["__file__"] = ImGuiLoadTextureFromFileVk(UTILS::getRealPath("Textures/Debug/Editor/file-white.png"));
		textureCache["__font__"] = ImGuiLoadTextureFromFileVk(UTILS::getRealPath("Textures/Debug/Editor/font-white.png"));
		textureCache["__object__"] = ImGuiLoadTextureFromFileVk(UTILS::getRealPath("Textures/Debug/Editor/cube-white.png"));
#endif
#ifdef DX12_BACKEND
		textureCache["__image__"] = ImGuiLoadTextureFromFileDx12(UTILS::getRealPath("Textures/Debug/Editor/image-white.png"));
		textureCache["__dir__"] = ImGuiLoadTextureFromFileDx12(UTILS::getRealPath("Textures/Debug/Editor/folder-white.png"));
		textureCache["__file__"] = ImGuiLoadTextureFromFileDx12(UTILS::getRealPath("Textures/Debug/Editor/file-white.png"));
		textureCache["__font__"] = ImGuiLoadTextureFromFileDx12(UTILS::getRealPath("Textures/Debug/Editor/font-white.png"));
		textureCache["__object__"] = ImGuiLoadTextureFromFileDx12(UTILS::getRealPath("Textures/Debug/Editor/cube-white.png"));
#endif
		initFileTree(root);
	}

	File root;
	void initFileTree(File& fileTree) {
		fileTree.files.clear();
		if (std::filesystem::is_directory(fileTree.path)) {
			for (const auto& entry : std::filesystem::directory_iterator(fileTree.path)) {
				fileTree.files.emplace_back(entry);
				initFileTree(fileTree.files.back());

			}
		}
	}

	std::set<int> searchedFileTreeIds;
	void draw() {
		//ImGui::Columns(2);
		ImGui::Begin("File Browser");
		if (ImGui::BeginTable("File Browser Table", 2, ImGuiTableFlags_Reorderable | ImGuiTableFlags_Resizable)) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			if (ImGui::Button(ICON_FA_RETWEET)) {
				initFileTree(root);
			}
			ImGui::SameLine();
			ImGui::Text("Search: ");
			ImGui::SameLine();
			static std::string searchInActors;
			if (ImGui::InputText("##search_in_file_tree", &searchInActors)) {
				searchedFileTreeIds.clear();
				if (!searchInActors.empty()) {
					std::function<void(File&)> closeAll;
					closeAll = [&closeAll, this](File& root) {
						for (auto& obj : root.files) {
							closeAll(obj);
							ImGui::TreeNodeSetOpen(obj.uid, false);
						}
						ImGui::TreeNodeSetOpen(root.uid, false);
					};
					closeAll(root);

					std::function<bool(File&)> expandAll;
					expandAll = [&expandAll, this](File& root) -> bool {
						auto _res = false;
						for (auto& obj : root.files) {
							const auto foundInName = UTILS::toLower(obj.file.string()).find(UTILS::toLower(searchInActors)) != std::string::npos;
							const auto res = expandAll(obj);
							if (foundInName) {
								ImGui::TreeNodeSetOpen(obj.uid, true);
								searchedFileTreeIds.insert(obj.uid);
								_res = true;
							}
							if (res) {
								ImGui::TreeNodeSetOpen(obj.uid, true);
								_res =  true;
							}
						}
						return _res;
					};
					if (expandAll(root)) {
						ImGui::TreeNodeSetOpen(root.uid, true);
					}

				}
			}

			ImGui::BeginChild("##fileTree_child_win");
			drawItem(root);
			ImGui::EndChild();
			
			ImGui::TableNextColumn();
			drawFolder(mSelectedFolderPath);

			ImGui::EndTable();
		}
		ImGui::End();
		//ImGui::Begin("File Browser");
		//globalNodeId = 0;
		//drawItem(globalNodeId, mPath);
		//ImGui::End();
		//ImGui::Begin("Folder");
		//drawFolder(mSelectedFolderPath);
		//ImGui::End();
	}
private:
#ifdef OPENGL_BACKEND
	std::unordered_map<std::string, unsigned> textureCache;
#endif
#ifdef VULKAN_BACKEND
	std::unordered_map<std::string, std::shared_ptr<RENDER::TextureVk>> textureCache;
#endif
	void drawFolder(std::string_view path) {
		const int sliderSize = 200;

		std::optional<std::string> newPath;

		ImGui::BeginChild("##FolderBar", ImVec2(0, 30));
		ImGui::Text(std::string(path).c_str());
		ImGui::SameLine();
		ImGui::Text("Search: ");
		ImGui::SameLine();
		static std::string searchInFolder;
		ImGui::PushItemWidth(sliderSize);
		if (ImGui::InputText("##search_in_file_tree", &searchInFolder)) {

		}
		ImGui::PopItemWidth();

		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_ARROW_LEFT)) {//back
			if (!history.empty()) {
				newPath = history.top();
				history.pop();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_ARROW_UP)) { //level up
			std::filesystem::path p = mSelectedFolderPath;
			if (mSelectedFolderPath != mPath) {
				history.push(mSelectedFolderPath);
				newPath = p.parent_path().string();
			}
		}
		
		ImGui::SameLine(std::max(1.0f, ImGui::GetWindowWidth() - sliderSize));
		ImGui::PushItemWidth(sliderSize);
		ImGui::SliderInt("##FSize", &elementSize, 20, 150);
		ImGui::PopItemWidth();
		ImGui::EndChild();

		ImGui::BeginChild("##FolderFiles", ImVec2(0, 0));
		const auto winSize = ImGui::GetWindowSize();
		//const float col0Size = ImGui::GetColumnWidth(0);
		//const float col1Size = winSize.x - col0Size;
		//ImGui::SetColumnWidth(1, col1Size);
		auto cols = static_cast<int>(winSize.x / (elementSize + 15));
		if (cols <= 0) {
			cols = 1;
		}
		//ImGui::Columns(static_cast<int>(col1Size / elementSize));
		int i = 0;
		int uid = 0;
		for (const auto& entry : std::filesystem::directory_iterator(path)) {
			uid++;
			if (!searchInFolder.empty()) {
				if (UTILS::toLower(entry.path().filename().string())
					.find(UTILS::toLower(searchInFolder)) == std::string::npos) {
					continue;
				}
			}
			ImGui::PushID(("folder_item_" + std::to_string(i)).c_str());
			bool isDirectory = std::filesystem::is_directory(entry);
			auto extType = File::getFileType(entry);
			ImGui::BeginGroup();
			std::string imPath;
			switch (extType) {
			case File::FileType::DIR: imPath = "__dir__";  break;
			case File::FileType::IMAGE: {
				imPath = entry.path().string();
				if (!textureCache.contains(imPath)) {
#ifdef OPENGL_BACKEND
					textureCache[imPath] = ImGuiLoadTextureFromFileGl(imPath).value();
#endif
#ifdef VULKAN_BACKEND
					textureCache[imPath] = ImGuiLoadTextureFromFileVk(imPath);
#endif
#ifdef DX12_BACKEND
					textureCache[imPath] = ImGuiLoadTextureFromFileDx12(imPath);
#endif
				}
				//imPath = "__image__";
				break;
			};
			case File::FileType::MODEL: imPath = "__object__"; break;
			case File::FileType::TEXT: imPath = "__file__"; break;
			case File::FileType::FONT: imPath = "__font__"; break;
			default: imPath = "__file__"; break;;
			}
			
			//TODO: create withget for it
			static int selected = -1;
			if (ImGui::Selectable(("##" + std::to_string(uid)).c_str(), uid == selected, 0, ImVec2(elementSize, elementSize))) {
				selected = uid;
			}

			ImGuiDragDropFlags src_flags = 0;
			src_flags |= ImGuiDragDropFlags_SourceNoDisableHover;     // Keep the source displayed as hovered
			//src_flags |= ImGuiDragDropFlags_SourceNoHoldToOpenOthers; // Because our dragging is local, we disable the feature of opening foreign treenodes/tabs while dragging
			//src_flags |= ImGuiDragDropFlags_SourceNoPreviewTooltip; // Hide the tooltip
			if (ImGui::BeginDragDropSource(src_flags)) {
				//if (!(src_flags))
				//	ImGui::Text("Moving \"%s\"", names[n]);
				int a = 0;
				if (extType == File::FileType::IMAGE) {
					ImGui::SetDragDropPayload("DND_IMAGE_DATA", &a, sizeof(int));
					dndStringPayload = imPath;
				}
				else if (extType == File::FileType::MATERIAL) {
					ImGui::SetDragDropPayload("DND_MATERIAL_DATA", &a, sizeof(int));
					dndStringPayload = entry.path().string();
				}
				ImGui::EndDragDropSource();
			}

			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
				if (isDirectory) {
					history.push(mSelectedFolderPath);
					newPath = entry.path().string();
				}
				else if (extType == File::FileType::MATERIAL) {
					editMaterial = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(entry.path().string());
					//editMaterial = RESOURCES::MaterialLoader::Create(entry.path().string());
				}
			}
			auto pos = ImGui::GetCursorPos();
			ImGui::SetCursorPos(ImVec2(pos.x, pos.y - elementSize));
			auto imguiId = textureCache.at(imPath)->getImguiId();
#ifdef OPENGL_BACKEND
			ImGui::Image(reinterpret_cast<ImTextureID>(imguiId), { static_cast<float>(elementSize), static_cast<float>(elementSize) }, ImVec2(0, 1), ImVec2(1, 0));
#else
			ImGui::Image(reinterpret_cast<ImTextureID>(imguiId), { static_cast<float>(elementSize), static_cast<float>(elementSize) });
#endif
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 100);
			ImGui::Text(entry.path().filename().string().c_str());
			ImGui::PopTextWrapPos();

			ImGui::EndGroup();
			i = (i + 1) % cols;
			if (i) {
				ImGui::SameLine();
			}
			//ImGui::NextColumn();
			ImGui::PopID();
		}
		ImGui::EndChild();
		//ImGui::Columns(2);
		if (newPath) {
			mSelectedFolderPath = newPath.value();
		}
	}

	void drawItem(File& file) {
		ImGui::PushID(("fileTree_" + std::to_string(file.uid)).c_str());
		ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_Selected;
		auto path = file.path.string();
		bool isDirectory = std::filesystem::is_directory(path);
		if (!isDirectory) {
			nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		else {
			nodeFlags |= ImGuiTreeNodeFlags_None;
		}
		std::string _path = std::string(path.begin(), path.end());
		if (path.back() == '\\' || path.back() == '/') {
			_path = std::filesystem::path(path).parent_path().string();
		}
		const auto name = std::filesystem::path(_path).filename().string();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f });
		bool inSearch = searchedFileTreeIds.contains(file.uid);
		if (inSearch) {
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
		}
		bool nodeIsOpen = ImGui::TreeNodeBehavior(file.uid, nodeFlags, name.c_str());
		if (inSearch) {
			ImGui::PopStyleColor();
		}
		ImGui::PopStyleVar();
		

		//ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 5.0f, 5.0f });
		//if (ImGui::BeginPopupContextItem("__SCENE_TREE_CONTEXTMENU__")) {
		//	if (ImGui::MenuItem("Create new")) {
		//
		//	}
		//	if (ImGui::MenuItem("Delete")) {
		//
		//	}
		//
		//	ImGui::EndPopup();
		//}
		//ImGui::PopStyleVar();

		//ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 10.0f, 10.0f });
		//if (ImGui::BeginDragDropSource()) {
		//	ImGui::SetDragDropPayload("__SCENE_NODE_DRAG__", &node, sizeof(IKIGAI::ECS::Object*));
		//	ImGui::TextUnformatted(name.c_str());
		//	ImGui::EndDragDropSource();
		//}
		//ImGui::PopStyleVar();

		//if (ImGui::BeginDragDropTarget()) {
		//	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("__SCENE_NODE_DRAG__")) {
		//		auto other = *static_cast<IKIGAI::ECS::Object**>(payload->Data);
		//
		//		//change node parent
		//		auto parent = other->getParent();
		//		if (parent) {
		//			//parent->removeChild(other);
		//			//other->setParent(nullptr);
		//		}
		//		//node.second->addChild(other);
		//	}
		//	ImGui::EndDragDropTarget();
		//}

		if (isDirectory && ImGui::IsItemClicked()) {
			history.push(mSelectedFolderPath);
			mSelectedFolderPath = _path;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f });
		ImGui::InvisibleButton("__NODE_ORDER_SET__", { -1, 5 });
		ImGui::PopStyleVar();

		//if (ImGui::BeginDragDropTarget()) {
		//	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("__SCENE_NODE_DRAG__")) {
		//		auto other = *static_cast<IKIGAI::ECS::Object**>(payload->Data);
		//		if (node.get() != other && node->getParent()) {
		//			auto parent = other->getParent();
		//			if (parent) {
		//				//parent->removeChild(other);
		//				//other->setParent(nullptr);
		//			}
		//			//node.second->getParent()->addChild(other->getID(), other, i + 1);
		//		}
		//	}
		//	ImGui::EndDragDropTarget();
		//}

		if (isDirectory && nodeIsOpen) {
			for (auto& entry : file.files) {
				drawItem(entry);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
};

std::unique_ptr<FileBrowser> fileBrowser;

/*
std::shared_ptr<IKIGAI::GUI::GuiObject> recursiveDrawGui(IKIGAI::SCENE_SYSTEM::Scene& activeScene, std::shared_ptr<IKIGAI::GUI::GuiObject> parentEntity) {
	std::shared_ptr<IKIGAI::GUI::GuiObject> selectedNode;

	std::vector<std::shared_ptr<IKIGAI::GUI::GuiObject>> nodeList;

	if (parentEntity) {
		nodeList = parentEntity->childs;
	}
	else {
		nodeList = activeScene.guiObjs;
	}
	auto i = 0u;
	for (auto node : nodeList) {
		ImGui::PushID(("node_" + std::to_string(i)).c_str());
		ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_Selected;

		bool isParent = node->childs.size();

		if (!isParent) {
			nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		else {
			nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
		}
		const auto name = node->name;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f });
		bool nodeIsOpen = ImGui::TreeNodeBehavior(ImGui::GetCurrentWindow()->GetID(node->name.c_str()), nodeFlags, name.c_str());
		ImGui::PopStyleVar();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 5.0f, 5.0f });
		if (ImGui::BeginPopupContextItem("__SCENE_TREE_CONTEXTMENU__")) {
			if (ImGui::MenuItem("Create new")) {

			}
			if (ImGui::MenuItem("Delete")) {

			}

			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 10.0f, 10.0f });
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("__SCENE_NODE_DRAG__", &node, sizeof(IKIGAI::ECS::Object*));
			ImGui::TextUnformatted(name.c_str());
			ImGui::EndDragDropSource();
		}
		ImGui::PopStyleVar();

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("__SCENE_NODE_DRAG__")) {
				auto other = *static_cast<IKIGAI::GUI::GuiObject**>(payload->Data);

				//change node parent
				auto parent = other->parent;
				if (parent) {
					//parent->removeChild(other);
					//other->setParent(nullptr);
				}
				//node.second->addChild(other);
			}
			ImGui::EndDragDropTarget();
		}

		if (ImGui::IsItemClicked()) {
			selectedNode = node;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f });
		ImGui::InvisibleButton("__NODE_ORDER_SET__", { -1, 5 });
		ImGui::PopStyleVar();

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("__SCENE_NODE_DRAG__")) {
				auto other = *static_cast<IKIGAI::GUI::GuiObject**>(payload->Data);
				if (node.get() != other && node->parent) {
					auto parent = other->parent;
					if (parent) {
						//parent->removeChild(other);
						//other->setParent(nullptr);
					}
					//node.second->getParent()->addChild(other->getID(), other, i + 1);
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (isParent && nodeIsOpen) {
			auto childClickedEntity = recursiveDrawGui(activeScene, node);
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
*/
//void drawNodeTreeGui(IKIGAI::CORE_SYSTEM::Core& core) {
//	static bool isobjTreeOpen = true;
//	if (core.sceneManager->hasCurrentScene()) {
//		auto& scene = core.sceneManager->getCurrentScene();
//		if (ImGui::Begin("Scene Hierarchy", &isobjTreeOpen)) {
//			uniqueNodeId = 0;
//			auto _selectNode = recursiveDrawGui(scene, nullptr);
//			if (_selectNode) {
//				selectObjGui = _selectNode;
//			}
//		}
//		ImGui::End();
//	}
//}


#include <coreModule/resourceManager/serviceManager.h>


bool SliderFloatWithSteps(const char* label, float* v, float v_min, float v_max, float v_step, const char* display_format = "%.3f") {
	if (!display_format)
		display_format = "%.3f";

	char text_buf[64] = {};
	ImFormatString(text_buf, IM_ARRAYSIZE(text_buf), display_format, *v);

	// Map from [v_min,v_max] to [0,N]
	const int countValues = int((v_max - v_min) / v_step);
	int v_i = int((*v - v_min) / v_step);
	const bool value_changed = ImGui::SliderInt(label, &v_i, 0, countValues, text_buf);

	// Remap from [0,N] to [v_min,v_max]
	*v = v_min + float(v_i) * v_step;
	return value_changed;
}


void PushIconFontSmall()
{
	//ImGui::PushFont(GUILayer::Get()->GetIconFontSmall());
}

void IconSmall(const char* icon)
{
	//PushIconFontSmall();
	ImGui::Text(icon);
	//ImGui::PopFont();
}
ImGuiWindowFlags gizmoWindowFlags = 0;
void BeginMovableChild(const char* childID, ImVec2 size, const ImVec2& defaultPosition, const ImRect& confineRect, bool isHorizontal, ImVec2 iconCursorOffset)
{
	const ImVec2 confineSize = ImVec2(confineRect.Max.x - confineRect.Min.x, confineRect.Max.y - confineRect.Min.y);
	const float  iconOffset = 12.0f;
	if (isHorizontal)
		size.x += iconOffset;
	else
		size.y += iconOffset;

	// Set the position only if first launch.
	const std::string childIDStr = std::string(childID);
	ImVec2            targetPosition = ImVec2(confineRect.Min.x + m_movableChildData[childIDStr].m_position.x, confineRect.Min.y + m_movableChildData[childIDStr].m_position.y);

	if (targetPosition.x > confineRect.Max.x - size.x)
		targetPosition.x = confineRect.Max.x - size.x;
	if (targetPosition.y > confineRect.Max.y - size.y)
		targetPosition.y = confineRect.Max.y - size.y;

	ImGui::SetNextWindowPos(targetPosition);

	ImGui::BeginChild(childID, size, true, ImGuiWindowFlags_NoMove);
	ImGui::SetCursorPos(iconCursorOffset);

	IconSmall(isHorizontal ? ICON_FA_ELLIPSIS_V : ICON_FA_ELLIPSIS_H);

	if (ImGui::IsItemClicked())
	{
		if (!m_movableChildData[childIDStr].m_isDragging)
		{
			m_movableChildData[childIDStr].m_isDragging = true;
			m_movableChildData[childIDStr].m_pressPosition = m_movableChildData[childIDStr].m_position;
		}
	}
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		m_movableChildData[childIDStr].m_isDragging = false;
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && m_movableChildData[childIDStr].m_isDragging)
	{

		// Calc new window position.
		const ImVec2 pressPos = m_movableChildData[childIDStr].m_pressPosition;
		const ImVec2 drag = ImGui::GetMouseDragDelta();
		ImVec2       desiredPosition = ImVec2(pressPos.x + drag.x, pressPos.y + drag.y);

		// Confine window position to confine rect.
		const float positionLimitPadding = 2.0f;
		if (desiredPosition.x < positionLimitPadding)
			desiredPosition.x = positionLimitPadding;
		else if (desiredPosition.x > confineSize.x - positionLimitPadding - size.x)
			desiredPosition.x = confineSize.x - positionLimitPadding - size.x;
		if (desiredPosition.y < positionLimitPadding)
			desiredPosition.y = positionLimitPadding;
		else if (desiredPosition.y > confineSize.y - positionLimitPadding - size.y)
			desiredPosition.y = confineSize.y - positionLimitPadding - size.y;

		m_movableChildData[childIDStr].m_position = desiredPosition;
	}
	else
	{


		if (!m_movableChildData[childIDStr].m_initialPositionSet)
		{
			m_movableChildData[childIDStr].m_initialPositionSet = true;
			m_movableChildData[childIDStr].m_position = defaultPosition;
		}
	}
}



static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
float camDistance = 8.f;
int gizmoCount = 1;
bool useWindow = false;
bool isPerspective = true;
void drawGuizmo(int w, int h) {
	if (!selectObj) {
		return;
	}

	ImGuizmo::SetOrthographic(!isPerspective);
	ImGuizmo::BeginFrame();

	ImGuizmo::SetID(0);

	bool editTransformDecomposition = false;

	static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);
	static bool useSnap = false;
	static float snap[3] = { 1.f, 1.f, 1.f };
	static float bounds[] = { -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f };
	static float boundsSnap[] = { 0.1f, 0.1f, 0.1f };
	static bool boundSizing = false;
	static bool boundSizingSnap = false;

	auto& sceneManager = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>();
	auto cameraComponent = sceneManager.getCurrentScene().findMainCamera().value();
	auto cameraProjection = MATHGL::Matrix4::Transpose(cameraComponent->getCamera().getProjectionMatrix());
	auto cameraView = MATHGL::Matrix4::Transpose(cameraComponent->getCamera().getViewMatrix());
	auto matrix = MATHGL::Matrix4::Transpose(selectObj->getTransform()->getWorldMatrix());


	if (editTransformDecomposition)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_T))
			mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
		if (ImGui::IsKeyPressed(ImGuiKey_E))
			mCurrentGizmoOperation = ImGuizmo::ROTATE;
		if (ImGui::IsKeyPressed(ImGuiKey_R)) // r Key
			mCurrentGizmoOperation = ImGuizmo::SCALE;
		if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
			mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
		ImGui::SameLine();
		if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE))
			mCurrentGizmoOperation = ImGuizmo::ROTATE;
		ImGui::SameLine();
		if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE))
			mCurrentGizmoOperation = ImGuizmo::SCALE;
		if (ImGui::RadioButton("Universal", mCurrentGizmoOperation == ImGuizmo::UNIVERSAL))
			mCurrentGizmoOperation = ImGuizmo::UNIVERSAL;
		float matrixTranslation[3], matrixRotation[3], matrixScale[3];
		ImGuizmo::DecomposeMatrixToComponents(matrix.getData(), matrixTranslation, matrixRotation, matrixScale);
		ImGui::InputFloat3("Tr", matrixTranslation);
		ImGui::InputFloat3("Rt", matrixRotation);
		ImGui::InputFloat3("Sc", matrixScale);
		//ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, matrix.getData());

		if (mCurrentGizmoOperation != ImGuizmo::SCALE)
		{
			if (ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
				mCurrentGizmoMode = ImGuizmo::LOCAL;
			ImGui::SameLine();
			if (ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD))
				mCurrentGizmoMode = ImGuizmo::WORLD;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_S))
			useSnap = !useSnap;
		ImGui::Checkbox("##UseSnap", &useSnap);
		ImGui::SameLine();

		switch (mCurrentGizmoOperation)
		{
		case ImGuizmo::TRANSLATE:
			ImGui::InputFloat3("Snap", &snap[0]);
			break;
		case ImGuizmo::ROTATE:
			ImGui::InputFloat("Angle Snap", &snap[0]);
			break;
		case ImGuizmo::SCALE:
			ImGui::InputFloat("Scale Snap", &snap[0]);
			break;
		}
		ImGui::Checkbox("Bound Sizing", &boundSizing);
		if (boundSizing)
		{
			ImGui::PushID(3);
			ImGui::Checkbox("##BoundSizing", &boundSizingSnap);
			ImGui::SameLine();
			ImGui::InputFloat3("Snap", boundsSnap);
			ImGui::PopID();
		}
	}

	ImGuiIO& io = ImGui::GetIO();
	float viewManipulateRight = io.DisplaySize.x;
	float viewManipulateTop = 0;
	
	//if (useWindow)
	//{
	//	ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_Appearing);
	//	ImGui::SetNextWindowPos(ImVec2(400, 20), ImGuiCond_Appearing);
	//	ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(0.35f, 0.3f, 0.3f));
	//	ImGui::Begin("Gizmo", 0, gizmoWindowFlags);
	//	ImGuizmo::SetDrawlist();
	//	float windowWidth = (float)ImGui::GetWindowWidth();
	//	float windowHeight = (float)ImGui::GetWindowHeight();
	//	ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);
	//	viewManipulateRight = ImGui::GetWindowPos().x + windowWidth;
	//	viewManipulateTop = ImGui::GetWindowPos().y;
	//	ImGuiWindow* window = ImGui::GetCurrentWindow();
	//	gizmoWindowFlags = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(window->InnerRect.Min, window->InnerRect.Max) ? ImGuiWindowFlags_NoMove : 0;
	//}
	//else
	//{
	//	ImGuizmo::SetDrawlist();
	//	float windowWidth = (float)ImGui::GetWindowWidth();
	//	float windowHeight = (float)ImGui::GetWindowHeight();
	//	ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);
	//	viewManipulateRight = ImGui::GetWindowPos().x + windowWidth;
	//	viewManipulateTop = ImGui::GetWindowPos().y;
	//	
	//}
	std::string childID = "##levelpanel_playops";
	const ImVec2 windowPos = ImGui::GetWindowPos();
	const ImVec2 windowSize = ImGui::GetWindowSize();
	const ImRect confineRect = ImRect(windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y));

	const float   rounding = 4.0f;
	const float   itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
	const float   itemSpacingY = ImGui::GetStyle().ItemSpacing.y;
	const float   childRounding = 6.0f;
	const ImVec2  buttonSize = ImVec2(28, 28);
	const ImVec2  currentWindowPos = ImGui::GetWindowPos();
	const ImVec2  contentOffset = ImVec2(0.5f, -2.0f);
	static ImVec2 childSize = ImVec2(buttonSize.x * 6 + itemSpacingX * 7, buttonSize.y + itemSpacingY * 3);
	ImGui::SetNextWindowBgAlpha(0.5f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, childRounding);
	const ImVec2 defaultPos = ImVec2((confineRect.Max.x - confineRect.Min.x) - childSize.x - 12 - 10, 12);

	BeginMovableChild(childID.c_str(), childSize, defaultPos, confineRect, true, ImVec2(5, 10));
	ImGui::SameLine();
	PushIconFontSmall();
	ImGui::SetCursorPos(ImVec2(17, 6));

	const bool isInPlayMode = false;
	const bool isPaused = false;

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
	ImGui::Button(ICON_FA_PLAY, buttonSize);
	ImGui::PopStyleColor();
	
	ImGui::SameLine();
	ImGui::SetCursorPosY(6.0f);
	ImGui::Button(ICON_FA_PAUSE, buttonSize);

	ImGui::SameLine();
	ImGui::SetCursorPosY(6.0f);
	ImGui::Button(ICON_FA_FAST_FORWARD, buttonSize);

	ImGui::SameLine();
	ImGui::SetCursorPosY(6.0f);
	ImGui::Button(ICON_FA_CAMERA, buttonSize);

	ImGui::SameLine();
	ImGui::SetCursorPosY(6.0f);
	ImGui::Button(ICON_FA_EYE, buttonSize);
	
	ImGui::SameLine();
	ImGui::SetCursorPosY(6.0f);
	ImGui::Button(ICON_FA_VECTOR_SQUARE, buttonSize);
	
	ImGui::EndChild();
	ImGui::PopStyleVar();


	////////////////////

	ImVec2 imageRectMin = ImGui::GetWindowPos();
	ImVec2 imageRectMax = ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight());


	//ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::Enable(true);
	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(imageRectMin.x, imageRectMin.y, imageRectMax.x - imageRectMin.x, imageRectMax.y - imageRectMin.y);
	ImGui::PushClipRect(imageRectMin, imageRectMax, false);


	ImGuizmo::SetDrawlist();
	float windowWidth = (float)w;// ImGui::GetWindowWidth();
	float windowHeight = (float)h;// ImGui::GetWindowHeight();
	ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);
	viewManipulateRight = ImGui::GetWindowPos().x + windowWidth;
	viewManipulateTop = ImGui::GetWindowPos().y;

	
	ImGuizmo::MODE gizmoMode = ImGuizmo::MODE::LOCAL;
	ImGuizmo::Manipulate(cameraView.getData(), cameraProjection.getData(), mCurrentGizmoOperation, gizmoMode, matrix.getData());

	auto rot = cameraComponent->obj->getTransform()->getLocalRotationDeg();
	float _rot[] = { rot.x, rot.y, rot.z };
	auto viewData = ImGuizmo::ViewManipulate(cameraView.getData(), _rot, camDistance, ImVec2(viewManipulateRight - 128, viewManipulateTop), ImVec2(128, 128), 0x10101010);
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	gizmoWindowFlags = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(window->InnerRect.Min, window->InnerRect.Max) ? ImGuiWindowFlags_NoMove : 0;
	
	if (viewData[0] == 1) {
		//auto vm = MATHGL::Matrix4::Transpose(cameraView);
		//vm(0, 3) = vm(1, 3) = vm(2,3) =
		//	vm(3,0) = vm(3,1) = vm(3,2) = 0.;
		//vm(3,3) = 1;
		//vm = MATHGL::Matrix4::Inverse(vm);
		//MATHGL::Quaternion q(vm);
		//auto rot = MATHGL::Quaternion::ToEulerAngles(q);
	
	
		//cameraComponent->obj->getTransform()->setLocalPosition(MATHGL::Vector3(viewData[1], viewData[2], viewData[3]));
		cameraComponent->obj->getTransform()->setLocalRotationDeg({ viewData[4], viewData[5], viewData[6] });
		//cameraComponent->getCamera().setView(MATHGL::Matrix4::Transpose(cameraView));
	}
	if (ImGuizmo::IsUsing())
	{
		float matrixTranslation[3], matrixRotation[3], matrixScale[3];
		ImGuizmo::DecomposeMatrixToComponents(matrix.getData(), matrixTranslation, matrixRotation, matrixScale);
		selectObj->getTransform()->setLocalPosition(MATHGL::Vector3(matrixTranslation[0], matrixTranslation[1], matrixTranslation[2]));
		selectObj->getTransform()->setLocalScale(MATHGL::Vector3(matrixScale[0], matrixScale[1], matrixScale[2]));
		selectObj->getTransform()->setLocalRotationDeg(MATHGL::Vector3(matrixRotation[0], matrixRotation[1], matrixRotation[2]));
	}


}


template<class T>
bool drawWidget(std::string name, T& elem) {
	using type = std::remove_reference_t<decltype(elem)>;
	int a=1;
	return false;
}

template<>
bool drawWidget(std::string name, float& elem) {
	return ImGui::DragFloat(name.c_str(), &elem, 0.1f, 0.1f, 500.0f);
}
template<>
bool drawWidget(std::string name, int& elem) {
	return ImGui::DragInt(name.c_str(), &elem, 1.0f, 0, 100);
}
template<>
bool drawWidget(std::string name, bool& elem) {
	return ImGui::Checkbox(name.c_str(), &elem);
}
template<>
bool drawWidget(std::string name, std::string& elem) {
	return false;
}
template<>
bool drawWidget(std::string name, MATHGL::Vector2u& elem) {
	int arr[2];
	arr[0] = elem.x;
	arr[1] = elem.y;
	auto b = ImGui::DragInt2(name.c_str(), arr, 1, 0, 4000);
	elem.x = arr[0];
	elem.y = arr[1];
	return b;
}

struct DebugConfig {
	enum class WidgetType {
		WINDOW = 0
	};

	static bool check(WidgetType t) { return conf[static_cast<int>(t)]; }
	inline static std::bitset<10> conf;
};

void ThemeNord(ImGuiStyle* dst = nullptr) {
	// Nord/Nordic GTK [Improvised]
	// https://github.com/EliverLara/Nordic
	ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
	ImVec4* colors = style->Colors;
	ImGui::StyleColorsDark(style);//Reset to base/dark theme
	colors[ImGuiCol_Text] = ImVec4(0.85f, 0.87f, 0.91f, 0.88f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.49f, 0.50f, 0.53f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
	colors[ImGuiCol_Border] = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.09f, 0.09f, 0.09f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.56f, 0.74f, 0.73f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.23f, 0.26f, 0.32f, 0.60f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.63f, 0.76f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.51f, 0.63f, 0.76f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.51f, 0.63f, 0.76f, 1.00f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.56f, 0.74f, 0.73f, 1.00f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.53f, 0.75f, 0.82f, 0.86f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.61f, 0.74f, 0.87f, 1.00f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
	colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.24f, 0.31f, 1.00f);
	colors[ImGuiCol_TabActive] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.13f, 0.15f, 0.18f, 1.00f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.17f, 0.19f, 0.23f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.56f, 0.74f, 0.73f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.53f, 0.75f, 0.82f, 0.86f);
	style->WindowBorderSize = 1.00f;
	style->ChildBorderSize = 1.00f;
	style->PopupBorderSize = 1.00f;
	style->FrameBorderSize = 1.00f;
}

void DarkTheme(ImGuiStyle* dst = nullptr) {
	ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
	style->WindowMinSize = ImVec2(160, 20);
	style->FramePadding = ImVec2(4, 2);
	style->ItemSpacing = ImVec2(6, 2);
	style->ItemInnerSpacing = ImVec2(6, 4);
	style->Alpha = 0.95f;
	style->WindowRounding = 4.0f;
	style->FrameRounding = 2.0f;
	style->IndentSpacing = 6.0f;
	style->ItemInnerSpacing = ImVec2(2, 4);
	style->ColumnsMinSpacing = 50.0f;
	style->GrabMinSize = 14.0f;
	style->GrabRounding = 16.0f;
	style->ScrollbarSize = 12.0f;
	style->ScrollbarRounding = 16.0f;
	
	style->Colors[ImGuiCol_Text] = ImVec4(0.86f, 0.93f, 0.89f, 0.78f);
	style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.86f, 0.93f, 0.89f, 0.28f);
	style->Colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
	style->Colors[ImGuiCol_Border] = ImVec4(0.31f, 0.31f, 1.00f, 0.00f);
	style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	style->Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
	style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
	style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
	style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.20f, 0.22f, 0.27f, 0.75f);
	style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.20f, 0.22f, 0.27f, 0.47f);
	style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.09f, 0.15f, 0.16f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
	style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_CheckMark] = ImVec4(0.71f, 0.22f, 0.27f, 1.00f);
	style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.47f, 0.77f, 0.83f, 0.14f);
	style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_Button] = ImVec4(0.47f, 0.77f, 0.83f, 0.14f);
	style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.86f);
	style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_Header] = ImVec4(0.92f, 0.18f, 0.29f, 0.76f);
	style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.86f);
	style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_Separator] = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);
	style->Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
	style->Colors[ImGuiCol_SeparatorActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.47f, 0.77f, 0.83f, 0.04f);
	style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
	style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_PlotLines] = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
	style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
	style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.92f, 0.18f, 0.29f, 0.43f);
	style->Colors[ImGuiCol_PopupBg] = ImVec4(0.20f, 0.22f, 0.27f, 0.9f);
	style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.22f, 0.27f, 0.73f);
}

void LightTheme(ImGuiStyle* dst = nullptr) {
	ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
	//ImGuiIO& io = ImGui::GetIO();
	//io.Fonts->Clear();
	//io.Fonts->AddFontFromFileTTF(ofToDataPath("fonts/OpenSans-Light.ttf", true).c_str(), 16);
	//io.Fonts->AddFontFromFileTTF(ofToDataPath("fonts/OpenSans-Regular.ttf", true).c_str(), 16);
	//io.Fonts->AddFontFromFileTTF(ofToDataPath("fonts/OpenSans-Light.ttf", true).c_str(), 32);
	//io.Fonts->AddFontFromFileTTF(ofToDataPath("fonts/OpenSans-Regular.ttf", true).c_str(), 11);
	//io.Fonts->AddFontFromFileTTF(ofToDataPath("fonts/OpenSans-Bold.ttf", true).c_str(), 11);
	//io.Fonts->Build();
	

	style->WindowPadding = ImVec2(15, 15);
	style->WindowRounding = 5.0f;
	style->FramePadding = ImVec2(5, 5);
	style->FrameRounding = 4.0f;
	style->ItemSpacing = ImVec2(12, 8);
	style->ItemInnerSpacing = ImVec2(8, 6);
	style->IndentSpacing = 25.0f;
	style->ScrollbarSize = 15.0f;
	style->ScrollbarRounding = 9.0f;
	style->GrabMinSize = 5.0f;
	style->GrabRounding = 3.0f;

	style->Colors[ImGuiCol_Text] = ImVec4(0.40f, 0.39f, 0.38f, 1.00f);
	style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.39f, 0.38f, 0.77f);
	style->Colors[ImGuiCol_WindowBg] = ImVec4(0.92f, 0.91f, 0.88f, 0.70f);
	style->Colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 0.98f, 0.95f, 0.58f);
	style->Colors[ImGuiCol_PopupBg] = ImVec4(0.92f, 0.91f, 0.88f, 0.92f);
	style->Colors[ImGuiCol_Border] = ImVec4(0.84f, 0.83f, 0.80f, 0.65f);
	style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.92f, 0.91f, 0.88f, 0.00f);
	style->Colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 0.98f, 0.95f, 1.00f);
	style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.99f, 1.00f, 0.40f, 0.78f);
	style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_TitleBg] = ImVec4(1.00f, 0.98f, 0.95f, 1.00f);
	style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 0.98f, 0.95f, 0.75f);
	style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_MenuBarBg] = ImVec4(1.00f, 0.98f, 0.95f, 0.47f);
	style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(1.00f, 0.98f, 0.95f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.00f, 0.00f, 0.00f, 0.21f);
	style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.90f, 0.91f, 0.00f, 0.78f);
	style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 0.98f, 0.95f, 1.00f);
	style->Colors[ImGuiCol_CheckMark] = ImVec4(0.25f, 1.00f, 0.00f, 0.80f);
	style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 0.00f, 0.00f, 0.14f);
	style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_Button] = ImVec4(0.00f, 0.00f, 0.00f, 0.14f);
	style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.99f, 1.00f, 0.22f, 0.86f);
	style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_Header] = ImVec4(0.25f, 1.00f, 0.00f, 0.76f);
	style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 1.00f, 0.00f, 0.86f);
	style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_Separator] = ImVec4(0.00f, 0.00f, 0.00f, 0.32f);
	style->Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.25f, 1.00f, 0.00f, 0.78f);
	style->Colors[ImGuiCol_SeparatorActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.04f);
	style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.25f, 1.00f, 0.00f, 0.78f);
	style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	//style->Colors[ImGuiCol_CloseButton] = ImVec4(0.40f, 0.39f, 0.38f, 0.16f);
	//style->Colors[ImGuiCol_CloseButtonHovered] = ImVec4(0.40f, 0.39f, 0.38f, 0.39f);
	//style->Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.40f, 0.39f, 0.38f, 1.00f);
	style->Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
	style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.98f, 0.95f, 0.73f);
}


void ArchTheme(ImGuiStyle* dst = nullptr) {

	auto m_defaultWindowPadding = MATHGL::Vector2i(8, 8);
	auto m_defaultFramePadding = MATHGL::Vector2i(8, 2);

	ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
	ImVec4* colors = ImGui::GetStyle().Colors;
	style->FrameBorderSize = 1.0f;
	style->PopupBorderSize = 1.0f;
	// style.AntiAliasedFill = false;
	// style.WindowRounding = 0.0f;
	style->TabRounding = 3.0f;
	// style.ChildRounding = 0.0f;
	style->PopupRounding = 3.0f;
	// style.FrameRounding = 0.0f;
	// style.ScrollbarRounding = 5.0f;
	style->FramePadding = ImVec2(m_defaultFramePadding.x, m_defaultFramePadding.y);
	style->WindowPadding = ImVec2(m_defaultWindowPadding.x, m_defaultWindowPadding.y);
	style->CellPadding = ImVec2(9, 2);
	// style.ItemInnerSpacing = ImVec2(8, 4);
	// style.ItemInnerSpacing = ImVec2(5, 4);
	// style.GrabRounding = 6.0f;
	// style.GrabMinSize     = 6.0f;
	style->ChildBorderSize = 0.0f;
	// style.TabBorderSize = 0.0f;
	style->WindowBorderSize = 1.0f;
	style->WindowMenuButtonPosition = ImGuiDir_None;
	colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
	colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	///colors[ImGuiCol_PopupBorder] = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.04f, 0.04f, 0.04f, 0.54f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.44f, 0.26f, 0.26f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.47f, 0.19f, 0.19f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.47f, 0.19f, 0.19f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.47f, 0.19f, 0.19f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.74f, 0.74f, 0.74f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.49f, 0.62f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.24f, 0.37f, 0.53f, 1.00f);
	///colors[ImGuiCol_ButtonLocked] = ImVec4(0.183f, 0.273f, 0.364f, 1.000f);
	///colors[ImGuiCol_ButtonSecondary] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	///colors[ImGuiCol_ButtonSecondaryHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	///colors[ImGuiCol_ButtonSecondaryActive] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	///colors[ImGuiCol_ButtonSecondaryLocked] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	///colors[ImGuiCol_Folder] = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
	///colors[ImGuiCol_FolderHovered] = ImVec4(0.35f, 0.49f, 0.62f, 1.00f);
	///colors[ImGuiCol_FolderActive] = ImVec4(0.24f, 0.37f, 0.53f, 1.00f);
	///colors[ImGuiCol_Toolbar] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
	///colors[ImGuiCol_Icon] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	///colors[ImGuiCol_TitleHeader] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
	///colors[ImGuiCol_TitleHeaderHover] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
	///colors[ImGuiCol_TitleHeaderPressed] = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
	///colors[ImGuiCol_TitleHeaderBorder] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	///colors[ImGuiCol_TitleHeaderDisabled] = ImVec4(0.17f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.47f, 0.19f, 0.19f, 1.00f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.43f, 0.24f, 0.24f, 1.00f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.49f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.44f, 0.44f, 0.44f, 1.00f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.58f, 0.58f, 0.58f, 1.00f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.73f, 0.73f, 0.73f, 1.00f);
	colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.24f, 0.25f, 0.26f, 1.00f);
	colors[ImGuiCol_TabActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
	colors[ImGuiCol_DockingPreview] = ImVec4(0.47f, 0.19f, 0.19f, 1.00f);
	colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.69f, 0.15f, 0.29f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
	colors[ImGuiCol_TableBorderStrong] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
	colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.47f, 0.20f, 0.20f, 0.71f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(0.58f, 0.23f, 0.23f, 0.71f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.61f);

	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		style->WindowRounding = 0.0f;
		style->Colors[ImGuiCol_WindowBg].w = 1.0f;
	}
}


DebugRender::DebugRender() {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
#if defined  OPENGL_BACKEND || defined  VULKAN_BACKEND
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
#endif
	//io.ConfigViewportsNoAutoMerge = true;
	//io.ConfigViewportsNoTaskBarIcon = true;
	float baseFontSize = 14.0f;
	ImFontConfig config;
	config.SizePixels = baseFontSize;
	io.Fonts->AddFontDefault(&config);
	
	float iconFontSize = baseFontSize * 2.0f / 3.0f;

	// merge in icons from Font Awesome
	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
	ImFontConfig icons_config;
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;
	icons_config.GlyphMinAdvanceX = iconFontSize;
	io.Fonts->AddFontFromFileTTF((Config::ROOT + "../3rd/imgui/IconFont/" + FONT_ICON_FILE_NAME_FAS).c_str(), iconFontSize, &icons_config, icons_ranges);

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

#ifdef VULKAN_BACKEND
	if (RENDER::DriverInterface::settings.backend == RENDER::RenderSettings::Backend::VULKAN) {
		auto win = RESOURCES::ServiceManager::Get<WINDOW_SYSTEM::Window>().getGLFWWin();
		ImGui_ImplGlfw_InitForVulkan(win, false);
		initForVk();
	}
#endif
	ArchTheme();

	// Setup Platform/Renderer backends


	//DEBUG CAMERA
	debugCamera = std::make_unique<ECS::Object>(ECS::Object::Id_(-123100), "DEBUG_CAMERA", "EDITOR");
	auto cam = debugCamera->addComponent<IKIGAI::ECS::CameraComponent>();
	cam->setFov(45.0f);
	cam->setSize(5.0f);
	cam->setNear(0.1f);
	cam->setFar(1000.0f);
	cam->setFrustumGeometryCulling(false);
	cam->setFrustumLightCulling(false);
	cam->setProjectionMode(IKIGAI::RENDER::Camera::ProjectionMode::PERSPECTIVE);
	debugCamera->getTransform()->setLocalPosition({ 0.f, 140.0f, 0.0f });
	debugCamera->getTransform()->setLocalRotation({ 0.0f, 0.98480773f, -0.17364819f, 0.0f });
	debugCamera->getTransform()->setLocalScale({ 1.0f, 1.0f, 1.0f });
	//END DEBUG CAMERA



	fileBrowser = std::make_unique<FileBrowser>(Config::ROOT + Config::ASSETS_PATH);


	m_movableChildData["GizmoTools"] = MovableChildData{ ImVec2(0, 0) , ImVec2(0, 0) , true, false };
	textureCache["default_texture"] = RESOURCES::ServiceManager::Get<RENDER::Renderer>().createTexture(UTILS::getRealPath("Textures/default.png"));
}


DebugRender::~DebugRender() {
}

void DebugRender::drawWindowWidget(CORE_SYSTEM::Core& core) {
	if (DebugConfig::check(DebugConfig::WidgetType::WINDOW)) {
		ImGui::Begin("Window Config");
		//auto b = buildWidget(core.window->getSetting());
		//if (b) {
		//	core.window->updateWindow();
		//}
		ImGui::End();
	}
}

bool drawFloatN(std::string& name, int size, float* vec, float step = 0.1f) {
	switch (size) {
	case 2:
		return ImGui::DragFloat2(name.c_str(), vec, step);
	case 3:
		return ImGui::DragFloat3(name.c_str(), vec, step);
	case 4:
		return ImGui::DragFloat4(name.c_str(), vec, step);
	default: ASSERT("Error");
	}
	return false;
}

bool drawColorN(std::string& name, int size, float* vec) {
	switch (size) {
	case 3:
		return ImGui::ColorEdit3(name.c_str(), vec);
	case 4:
		return ImGui::ColorEdit4(name.c_str(), vec);
	default: ASSERT("Error");
	}
	return false;
}

class CombineVecEdit {
	size_t mVecSize = 3;
	std::string mName;
	bool mIsColorMode = false;
	std::string mButtonLblColor;
	std::string mButtonLblVec;
	float mWidth = 200.0f;
public:
	enum class MODE {
		COMBINE, COLOR, POS
	};
	MODE mMode = MODE::COMBINE;
	CombineVecEdit(std::string_view name, size_t vecSize, MODE mode = MODE::COMBINE): mVecSize(vecSize), mName(name), mMode(mode) {
		switch (mVecSize) {
		case 2: mButtonLblVec = "XY"; mButtonLblColor = "RG"; mMode = MODE::POS; break;
		case 3: mButtonLblVec = "XYZ"; mButtonLblColor = "RGB"; mWidth = 150.0f; break;
		case 4: mButtonLblVec = "XYZW"; mButtonLblColor = "RGBA"; mWidth = 150.0f; break;
		default: ASSERT("Support only 2, 3, 4 vec size");
		}
		if (mode == MODE::POS) {
			mIsColorMode = false;
		}
		if (mode == MODE::COLOR) {
			mIsColorMode = true;
		}
	}
	inline static std::unordered_map<std::string, CombineVecEdit> Data;

	//add type check
	template<class T>
	bool draw(T& vec) {
		return draw(reinterpret_cast<float*>(&vec));
	}
private:
	bool draw(float* vec) {
		ImGui::Columns(2);
		ImGui::SetColumnWidth(1, 200.0f);

		ImGui::Text(mName.c_str());
		ImGui::NextColumn();

		ImGui::SetNextItemWidth(mWidth);

		bool res = false;
		auto _nameId = "##" + mName;
		if (mIsColorMode) {
			if (drawColorN(_nameId, mVecSize, vec)) {
				res = true;
			}
		}
		else {
			if (drawFloatN(_nameId, mVecSize, vec)) {
				res = true;
			}
		}
		if (mMode == MODE::COMBINE) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(50);
			if (ImGui::SmallButton(mIsColorMode ? mButtonLblColor.c_str() : mButtonLblVec.c_str())) {
				mIsColorMode = !mIsColorMode;
			}
		}

		ImGui::NextColumn();
		ImGui::Columns(1);
		return res;
	}
};

class ScalarEdit {
	std::string mName;
public:

	inline static std::unordered_map<std::string, ScalarEdit> Data;
	enum class TYPE {
		INT, FLOAT, BOOL
	};
	TYPE mType;
	ScalarEdit(std::string_view name, TYPE type): mType(type), mName(name) {
		
	}
	template<class T>
	bool draw(T& val) {
		bool res = false;
		ImGui::Columns(2);
		ImGui::SetColumnWidth(1, 200.0f);

		ImGui::Text(mName.c_str());
		ImGui::NextColumn();

		ImGui::SetNextItemWidth(200.0f);

		const auto _nameId = "##" + mName;

		res = draw(_nameId, val);

		ImGui::NextColumn();
		ImGui::Columns(1);
		return res;
	}
private:
	template<class T>
	bool draw(const std::string& _nameId, T& val) {
		return false;
	}
	template<>
	bool draw<int>(const std::string& _nameId, int& val) {
		return ImGui::DragInt(_nameId.c_str(), &val);
	}
	template<>
	bool draw<float>(const std::string& _nameId, float& val) {
		return ImGui::DragFloat(_nameId.c_str(), &val, 0.1f);
	}
	template<>
	bool draw<bool>(const std::string& _nameId, bool& val) {
		return ImGui::Checkbox(_nameId.c_str(), &val);
	}
};

bool isEngineUniform(const std::string& uniformName) {
	return uniformName.rfind("EngineUBO", 0) == 0 || uniformName.rfind("engine_", 0) == 0
		|| uniformName.rfind("Engine", 0) == 0;
}
#ifdef OPENGL_BACKEND
void DebugRender::drawMaterialWidget(RENDER::MaterialGl* material) {
	ImGui::Begin("Material Editor");

	if (ImGui::Button("Save")) {
		nlohmann::json data;
		material->onSerialize(data);
		std::ofstream f(UTILS::getRealPath(material->mPath));
		f << data.dump(4) << std::endl;
		f.close();
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload")) {
		auto path = material->mPath;
		//editMaterial = RESOURCES::MaterialLoader::Create(path);
		editMaterial = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(path);
	}
	ImGui::SameLine();
	if (ImGui::Button("Create new")) {
		popupStates["create_new_material"] = true;
	}

	ImGui::Text("Name:", material->mPath);

	static bool isMatSettingsOpen = true;
	if (ImGui::CollapsingHeader("Material settings", &isMatSettingsOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Checkbox("Is Blendable", &material->mBlendable)) {
			//material->generateStateMask();
		}
		if (ImGui::Checkbox("Back face culling", &material->mBackfaceCulling)) {
			//material->generateStateMask();
		}
		if (ImGui::Checkbox("Front face culling", &material->mFrontfaceCulling)) {
			//material->generateStateMask();
		}
		if (ImGui::Checkbox("Depth test", &material->mDepthTest)) {
			//material->generateStateMask();
		}
		if (ImGui::Checkbox("Color writing", &material->mColorWriting)) {
			//material->generateStateMask();
		}
		if (ImGui::DragInt("GPU Instance", &material->mGpuInstances)) {
			//material->generateStateMask();
		}
	}

	static std::string shaderVPath = material->mShader->vertexPath.value();
	static std::string shaderFPath = material->mShader->fragmentPath.value();
	if (ImGui::InputText("Shader Fragment:", &shaderVPath, ImGuiInputTextFlags_EnterReturnsTrue)) {
		
	}
	if (ImGui::InputText("Shader Vertex:", &shaderFPath, ImGuiInputTextFlags_EnterReturnsTrue)) {

	}

	auto shader = material->getShader();
	if (!shader) {
		ImGui::End();
		return;
	}
	const auto& refl = shader->getUniformsInfo();

	for (auto& [name, data]: refl) {
		if (isEngineUniform(name)) continue;
		if (data.type == RENDER::UniformInform::TYPE::UNIFORM) {
			switch (data.members[0].type) {
			case RENDER::UNIFORM_TYPE::MAT4: break;
			case RENDER::UNIFORM_TYPE::MAT3: break;
			case RENDER::UNIFORM_TYPE::VEC4: {
				if (!CombineVecEdit::Data.count(name)) {
					CombineVecEdit::Data.insert({ name, CombineVecEdit(name, 4) });
				}
				auto val = std::get<MATHGL::Vector4>(material->get(name));
				if (CombineVecEdit::Data.at(name).draw(val)) {
					material->set(name, val);
				}
				break;
			}
			case RENDER::UNIFORM_TYPE::VEC3: {
				if (!CombineVecEdit::Data.count(name)) {
					CombineVecEdit::Data.insert({ name, CombineVecEdit(name, 3) });
				}
				auto val = std::get<MATHGL::Vector3>(material->get(name));
				if (CombineVecEdit::Data.at(name).draw(val)) {
					material->set(name, val);
				}
				break;
			}
			case RENDER::UNIFORM_TYPE::VEC2: {
				if (!CombineVecEdit::Data.count(name)) {
					CombineVecEdit::Data.insert({ name, CombineVecEdit(name, 2) });
				}
				auto val = std::get<MATHGL::Vector2f>(material->get(name));
				if (CombineVecEdit::Data.at(name).draw(val)) {
					material->set(name, val);
				}
				break;
			}
			case RENDER::UNIFORM_TYPE::INT: {
				if (!ScalarEdit::Data.count(name)) {
					ScalarEdit::Data.insert({ name, ScalarEdit(name, ScalarEdit::TYPE::INT) });
				}
				auto val = std::get<int>(material->mUniformData.at(name));
				if (ScalarEdit::Data.at(name).draw(val)) {
					material->set(name, val);
				}
				break;
			}
			case RENDER::UNIFORM_TYPE::FLOAT: {
				if (!ScalarEdit::Data.count(name)) {
					ScalarEdit::Data.insert({ name, ScalarEdit(name, ScalarEdit::TYPE::FLOAT) });
				}
				auto val = std::get<float>(material->mUniformData.at(name));
				if (ScalarEdit::Data.at(name).draw(val)) {
					material->set(name, val);
				}
				break;
			}
			case RENDER::UNIFORM_TYPE::BOOL: {
				if (!ScalarEdit::Data.count(name)) {
					ScalarEdit::Data.insert({ name, ScalarEdit(name, ScalarEdit::TYPE::BOOL) });
				}
				auto val = std::get<bool>(material->mUniformData.at(name));
				if (ScalarEdit::Data.at(name).draw(val)) {
					material->set(name, val);
				}
				break;
			}
			case RENDER::UNIFORM_TYPE::SAMPLER_2D: {
				auto val = std::get<std::shared_ptr<RENDER::TextureGl>>(material->mUniformData.at(name));
				ImGui::PushID(("##" + name).c_str());
				
				//auto size = RESOURCES::ServiceManager::Get<WINDOW_SYSTEM::Window>().getSize();
				if (ImGui::Selectable(("##" + name).c_str(), false, 0, ImVec2(100, 100))) {
					
				}
				auto pos = ImGui::GetCursorPos();
				ImGui::SetCursorPos(ImVec2(pos.x, pos.y - 100));

				if (ImGui::BeginDragDropTarget())
				{
					ImGuiDragDropFlags target_flags = 0;
					//target_flags |= ImGuiDragDropFlags_AcceptBeforeDelivery;    // Don't wait until the delivery (release mouse button on a target) to do something
					//target_flags |= ImGuiDragDropFlags_AcceptNoDrawDefaultRect; // Don't display the yellow rectangle
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_IMAGE_DATA", target_flags)) {
						auto path = dndStringPayload;
						material->set(name, std::static_pointer_cast<RENDER::TextureGl>(RESOURCES::ServiceManager::Get<RESOURCES::TextureLoader>().createFromFile(path, true)));
						dndStringPayload.clear();
					}
					ImGui::EndDragDropTarget();
				}
				if (val) {
					ImGui::Image(reinterpret_cast<ImTextureID>((uintptr_t)val->id), ImVec2(100, 100), ImVec2(0, 1), ImVec2(1, 0));
				}
				else {
					ImGui::Image(reinterpret_cast<ImTextureID>((uintptr_t)textureCache["default_texture"]), ImVec2(100, 100), ImVec2(0, 1), ImVec2(1, 0));
				}
				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_FILE)) {
					popupStates["file_chooser"] = true;
					fileFormatsCb = [] {
						return ".png,.jpeg,.jpg";
					};
					fileChooserCb = [material, name](std::string path) mutable {
						material->set(name, std::static_pointer_cast<RENDER::TextureGl>(RESOURCES::ServiceManager::Get<RESOURCES::TextureLoader>().createFromFile(path, true)));
					};
				}
				ImGui::PopID();
				//ImGui::GetWindowDrawList()->AddImage(
				//	(void*)val->getId(),
				//	ImVec2(ImGui::GetCursorScreenPos()),
				//	ImVec2(ImGui::GetCursorScreenPos().x + size.x / 2,
				//		ImGui::GetCursorScreenPos().y + size.y / 2), ImVec2(0, 1), ImVec2(1, 0));
				break;
			}
			case RENDER::UNIFORM_TYPE::SAMPLER_3D: break;
			case RENDER::UNIFORM_TYPE::SAMPLER_CUBE: break;
			default: break;
			}
		}
		else if (data.type == RENDER::UniformInform::TYPE::UNIFORM_BUFFER) {
			for (const auto& mamber : data.members) {
				auto _name = mamber.name;
				switch (mamber.type) {
				case RENDER::UNIFORM_TYPE::MAT4: break;
				case RENDER::UNIFORM_TYPE::MAT3: break;
				case RENDER::UNIFORM_TYPE::VEC4: {
					if (!CombineVecEdit::Data.count(_name)) {
						CombineVecEdit::Data.insert({ _name, CombineVecEdit(_name, 4) });
					}
					auto val = std::get<MATHGL::Vector4>(material->get(name, mamber.name));
					if (CombineVecEdit::Data.at(_name).draw(val)) {
						material->set(name, mamber.name, val);
					}
					break;
				}
				case RENDER::UNIFORM_TYPE::VEC3: {
					if (!CombineVecEdit::Data.count(_name)) {
						CombineVecEdit::Data.insert({ _name, CombineVecEdit(_name, 3) });
					}
					auto val = std::get<MATHGL::Vector3>(material->get(name, mamber.name));
					if (CombineVecEdit::Data.at(_name).draw(val)) {
						material->set(name, mamber.name, val);
					}
					break;
				}
				case RENDER::UNIFORM_TYPE::VEC2: {
					if (!CombineVecEdit::Data.count(_name)) {
						CombineVecEdit::Data.insert({ _name, CombineVecEdit(_name, 2) });
					}
					auto val = std::get<MATHGL::Vector2f>(material->get(name, mamber.name));
					if (CombineVecEdit::Data.at(_name).draw(val)) {
						material->set(name, mamber.name, val);
					}
					break;
				}
				case RENDER::UNIFORM_TYPE::INT: {
					if (!ScalarEdit::Data.count(name)) {
						ScalarEdit::Data.insert({ name, ScalarEdit(name, ScalarEdit::TYPE::INT) });
					}
					auto val = std::get<int>(material->mUniformData.at(name));
					if (ScalarEdit::Data.at(name).draw(val)) {
						material->set(name, val);
					}
					break;
				}
				case RENDER::UNIFORM_TYPE::FLOAT: {
					if (!ScalarEdit::Data.count(name)) {
						ScalarEdit::Data.insert({ name, ScalarEdit(name, ScalarEdit::TYPE::FLOAT) });
					}
					auto val = std::get<float>(material->mUniformData.at(name));
					if (ScalarEdit::Data.at(name).draw(val)) {
						material->set(name, val);
					}
					break;
				}
				case RENDER::UNIFORM_TYPE::BOOL: {
					if (!ScalarEdit::Data.count(name)) {
						ScalarEdit::Data.insert({ name, ScalarEdit(name, ScalarEdit::TYPE::BOOL) });
					}
					auto val = std::get<bool>(material->mUniformData.at(name));
					if (ScalarEdit::Data.at(name).draw(val)) {
						material->set(name, val);
					}
					break;
				}
				case RENDER::UNIFORM_TYPE::SAMPLER_2D: break;
				case RENDER::UNIFORM_TYPE::SAMPLER_3D: break;
				case RENDER::UNIFORM_TYPE::SAMPLER_CUBE: break;
				default: break;
				}
			}
		}
	}
	ImGui::End();
}
#endif

void drawComponent(ECS::SpotLight* component) {
	if (ImGui::CollapsingHeader(component->getName().c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {

	}
}

void drawComponent(ECS::DirectionalLight* component) {

}

void drawComponent(ECS::PointLight* component) {

}

void drawComponent(ECS::AmbientLight* component) {

}

void drawComponent(ECS::AmbientSphereLight* component) {

}

void drawComponent(ECS::TransformComponent* component) {
	if (ImGui::CollapsingHeader(component->getName().c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		{
			auto val = component->getTransform().getLocalPosition();
			std::string _name = "Position";
			if (!CombineVecEdit::Data.count(_name)) {
				CombineVecEdit::Data.insert({ _name, CombineVecEdit(_name, 3, CombineVecEdit::MODE::POS) });
			}
			if (CombineVecEdit::Data.at(_name).draw(val)) {
				component->getTransform().setLocalPosition(val);
			}
		}
		{
			auto val = component->getTransform().getLocalScale();
			std::string _name = "Scale";
			if (!CombineVecEdit::Data.count(_name)) {
				CombineVecEdit::Data.insert({ _name, CombineVecEdit(_name, 3, CombineVecEdit::MODE::POS) });
			}
			if (CombineVecEdit::Data.at(_name).draw(val)) {
				component->getTransform().setLocalScale(val);
			}
		}
		{
			auto val = MATHGL::Quaternion::ToEulerAngles(component->getTransform().getLocalRotation());
			val = { TO_DEGREES(val.x), TO_DEGREES(val.y), TO_DEGREES(val.z) };
			std::string _name = "Rotate";
			if (!CombineVecEdit::Data.count(_name)) {
				CombineVecEdit::Data.insert({ _name, CombineVecEdit(_name, 3, CombineVecEdit::MODE::POS) });
			}
			if (CombineVecEdit::Data.at(_name).draw(val)) {
				//val = { TO_RADIANS(val.x), TO_RADIANS(val.y), TO_RADIANS(val.z) };
				component->getTransform().setLocalRotation(MATHGL::Quaternion(val));
			}
		}

		if (component->getTransform().isAnchorPivotMode()) {
			{
				auto val = component->getTransform().getLocalAnchor();
				std::string _name = "Anchor";
				if (!CombineVecEdit::Data.count(_name)) {
					CombineVecEdit::Data.insert({ _name, CombineVecEdit(_name, 2) });
				}
				if (CombineVecEdit::Data.at(_name).draw(val)) {
					component->getTransform().setLocalAnchor(val);
				}
			}
			{
				auto val = component->getTransform().getLocalPivot();
				std::string _name = "Pivot";
				if (!CombineVecEdit::Data.count(_name)) {
					CombineVecEdit::Data.insert({ _name, CombineVecEdit(_name, 2) });
				}
				if (CombineVecEdit::Data.at(_name).draw(val)) {
					component->getTransform().setLocalPivot(val);
				}
			}
			{
				auto val = component->getTransform().getLocalSize();
				std::string _name = "Size";
				if (!CombineVecEdit::Data.count(_name)) {
					CombineVecEdit::Data.insert({ _name, CombineVecEdit(_name, 2) });
				}
				if (CombineVecEdit::Data.at(_name).draw(val)) {
					component->getTransform().setLocalSize(val);
				}
			}
			
		}
	}
}

template<class T>
T getValueFromProp(ComponentType& c, rttr::property& p) {
	return std::visit(
		[&](auto& arg) {
			return p.get_value(c);
		},c);
}

//----------------------------------------------
const std::string& drawSearchBox(const std::vector<std::string>& autocomplete) {
	static std::string input;
	ImGui::InputText("##input", &input);
	ImGui::SameLine();
	static bool isOpen = false;
	bool isFocused = ImGui::IsItemFocused();
	isOpen |= ImGui::IsItemActive();
	if (isOpen)
	{
		ImGui::SetNextWindowPos({ ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y });
		ImGui::SetNextWindowSize({ ImGui::GetItemRectSize().x, 0 });
		if (ImGui::Begin("##popup", &isOpen, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_Tooltip)) {
			ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
			isFocused |= ImGui::IsWindowFocused();
			for (int i = 0; i < autocomplete.size(); i++) {
				if (strstr(autocomplete[i].c_str(), input.c_str()) == NULL)
					continue;
				if (ImGui::Selectable(autocomplete[i].c_str()) || (ImGui::IsItemFocused() && ImGui::IsKeyPressedMap(ImGuiKey_Enter))) {
					input = autocomplete[i];
					isOpen = false;
				}
			}
		}
		ImGui::End();
		isOpen &= isFocused;
	}
	return input;
}

//ArCameraComponent,\

//using ComponentsTypeProviderType = ECS::ComponentsTypeProvider<
//	COMPONENTS_DEF
//>;

template<typename T>
void removeComponentFromObjectImpl(std::shared_ptr<IKIGAI::ECS::Object> obj, std::string_view compName) {
	if (compName == typeid(T).name()) {
		obj->removeComponent<T>();
	}
}

template<>
void removeComponentFromObjectImpl<ECS::TransformComponent>(std::shared_ptr<IKIGAI::ECS::Object> obj, std::string_view compName) {}

template<template<typename...> class Container, typename...ComponentType>
void removeComponentFromObject(std::shared_ptr<IKIGAI::ECS::Object> obj, std::string_view compName, Container<ComponentType...> opt) {
	(removeComponentFromObjectImpl<ComponentType>(obj, compName), ...);
}

void removeComponentFromObject(std::shared_ptr<IKIGAI::ECS::Object> obj, std::string_view compName) {
	removeComponentFromObject(obj, compName, ECS::ComponentsTypeProviderType{});
}
//----------------------------------


template<typename T>
void genComponentsStringArrayImpl(std::vector<std::string>& arr) {
	auto t = rttr::type::get<T>();
	arr.push_back(std::string(t.get_name().begin(), t.get_name().end()));
	//arr.push_back(typeid(T).name());
}

template<template<typename...> class Container, typename...ComponentType>
void genComponentsStringArray(std::vector<std::string>& arr, Container<ComponentType...> opt) {
	(genComponentsStringArrayImpl<ComponentType>(arr), ...);
}

std::vector<std::string> genComponentsStringArray() {
	std::vector<std::string> res;
	genComponentsStringArray(res, ECS::ComponentsTypeProviderType{});
	return res;
}
//----------------------------------


template<typename T>
void addComponentForObjectImpl(std::shared_ptr<IKIGAI::ECS::Object> obj, std::string_view compName) {
	auto t = rttr::type::get<T>();
	if (compName == t.get_name().to_string()) {
		obj->addComponent<T>();
	}
}

template<template<typename...> class Container, typename...ComponentType>
void addComponentForObject(std::shared_ptr<IKIGAI::ECS::Object> obj, std::string_view compName, Container<ComponentType...> opt) {
	(addComponentForObjectImpl<ComponentType>(obj, compName), ...);
}

void addComponentForObject(std::shared_ptr<IKIGAI::ECS::Object> obj, std::string_view compName) {
	addComponentForObject(obj, compName, ECS::ComponentsTypeProviderType{});
}

//----------------------------------


template<typename T>
void getInstanceImpl(UTILS::WeakPtr<ECS::Component> c, std::string_view compName, std::unique_ptr<rttr::instance>& inst) {
	auto t = rttr::type::get<T>();
	if (compName == t.get_name().to_string()) {
		inst = std::make_unique<rttr::instance>(*static_cast<T*>(c.get()));
	}
}

template<template<typename...> class Container, typename...ComponentType>
void getInstance(UTILS::WeakPtr<ECS::Component> c, std::string_view compName, std::unique_ptr<rttr::instance>& inst, Container<ComponentType...> opt) {
	(getInstanceImpl<ComponentType>(c, compName, inst), ...);
}

void getInstance(UTILS::WeakPtr<ECS::Component> c, std::unique_ptr<rttr::instance>& inst) {
	getInstance(c, c->getName(), inst, ECS::ComponentsTypeProviderType{});
}

//-------------------------------------------

void widgetFloat3(UTILS::WeakPtr<ECS::Component> component, const rttr::property& prop) {
	const std::string propName = prop.get_name().to_string();
	if (!CombineVecEdit::Data.contains(propName)) {
		CombineVecEdit::Data.insert({ propName, CombineVecEdit(propName, 3, CombineVecEdit::MODE::POS) });
	}
	std::unique_ptr<rttr::instance> inst;
	getInstance(component, inst);
	auto val = prop.get_value(*inst).get_value<MATHGL::Vector3>();
	if (CombineVecEdit::Data.at(propName).draw(val)) {
		std::ignore = prop.set_value(*inst, val);
	}
}

void widgetColor3(UTILS::WeakPtr<ECS::Component> component, const rttr::property& prop) {
	const std::string propName = prop.get_name().to_string();
	if (!CombineVecEdit::Data.contains(propName)) {
		CombineVecEdit::Data.insert({ propName, CombineVecEdit(propName, 3, CombineVecEdit::MODE::COLOR) });
	}
	std::unique_ptr<rttr::instance> inst;
	getInstance(component, inst);
	auto val = prop.get_value(*inst).get_value<MATHGL::Vector3>();
	if (CombineVecEdit::Data.at(propName).draw(val)) {
		std::ignore = prop.set_value(*inst, val);
	}
}

/*
 * TODO:
 * - view port for window (not screen size)
 */

void widgetFloat(UTILS::WeakPtr<ECS::Component> component, const rttr::property& prop) {
	const std::string propName = prop.get_name().to_string();
	std::unique_ptr<rttr::instance> inst;
	getInstance(component, inst);
	auto val = prop.get_value(*inst).get_value<float>();

	const auto flagsRageData = prop.get_metadata(EditorMetaInfo::EDIT_RANGE);
	const auto flagsStepData = prop.get_metadata(EditorMetaInfo::EDIT_STEP);
	auto _speed = 1;
	auto _min = 0.0f;
	auto _max = 0.0f;
	if (flagsRageData) {
		_min = flagsRageData.get_value<Pair>().first;
		_max = flagsRageData.get_value<Pair>().second;
	}
	if (flagsRageData) {
		_speed = flagsStepData.get_value<float>();
	}
	if (ImGui::DragFloat(propName.c_str(), &val, _speed, _min, _max)) {
		std::ignore = prop.set_value(*inst, val);
	}
}

void widgetBool(UTILS::WeakPtr<ECS::Component> component, const rttr::property& prop) {
	const std::string propName = prop.get_name().to_string();
	std::unique_ptr<rttr::instance> inst;
	getInstance(component, inst);
	auto val = prop.get_value(*inst).get_value<bool>();
	if (ImGui::Checkbox(propName.c_str(), &val)) {
		std::ignore = prop.set_value(*inst, val);
	}
}

//TODO: add dnd
void widgetStringFixFileChoose(UTILS::WeakPtr<ECS::Component> component, const rttr::property& prop) {
	const std::string propName = prop.get_name().to_string();
	std::unique_ptr<rttr::instance> inst;
	getInstance(component, inst);

	auto fileExt = prop.get_metadata(EditorMetaInfo::FILE_EXTENSION).to_string();
	auto val = prop.get_value(*inst).get_value<std::string>();
	if (ImGui::InputText(propName.c_str(), &val)) {
		std::ignore = prop.set_value(*inst, val);
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_FILE)) {
		popupStates["file_chooser"] = true;
		fileFormatsCb = [fileExt] {
			return fileExt;
		};
		fileChooserCb = [prop, component](std::string path) mutable {
			std::unique_ptr<rttr::instance> inst;
			getInstance(component, inst);
			std::ignore = prop.set_value(*inst, path);
		};
	}
}

void widgetString(UTILS::WeakPtr<ECS::Component> component, const rttr::property& prop) {
	const std::string propName = prop.get_name().to_string();
	std::unique_ptr<rttr::instance> inst;
	getInstance(component, inst);
	auto val = prop.get_value(*inst).get_value<std::string>();
	if (ImGui::InputText(propName.c_str(), &val)) {
		std::ignore = prop.set_value(*inst, val);
	}
}

void widgetStringArray(UTILS::WeakPtr<ECS::Component> component, const rttr::property& prop) {
	const std::string propName = prop.get_name().to_string();
	std::unique_ptr<rttr::instance> inst;
	getInstance(component, inst);
	ImGui::Text(propName.c_str());
	auto val = prop.get_value(*inst).get_value<std::vector<std::string>>();
	for (auto& e : val) {
		if (ImGui::InputText(propName.c_str(), &e)) {
			std::ignore = prop.set_value(*inst, val);
		}
	}
}

void widgetModelLod(UTILS::WeakPtr<ECS::Component> component, const rttr::property& prop) {
	const std::string propName = prop.get_name().to_string();
	std::unique_ptr<rttr::instance> inst;
	getInstance(component, inst);
	ImGui::Text(propName.c_str());
	auto val = prop.get_value(*inst).get_value<std::vector<ECS::ModelLODRenderer::ModelLodRefl>>();
	int i = 0;
	for (auto& e : val) {
		ImGui::PushID(i);
		if (ImGui::DragFloat("Distance", &e.m_distance)) {
			std::ignore = prop.set_value(*inst, val);
		}
		ImGui::SameLine();
		if (ImGui::InputText("Model", &e.m_path, ImGuiInputTextFlags_EnterReturnsTrue)) {
			std::ignore = prop.set_value(*inst, val);
		}
		ImGui::SameLine();
		if (ImGui::Button("X")) {
			val.erase(val.begin() + i);
			std::ignore = prop.set_value(*inst, val);
		}
		ImGui::PopID();
		i++;
	}
	if (ImGui::Button("Add")) {
		val.push_back({});
		std::ignore = prop.set_value(*inst, val);
	}
}


void widgetMaterial(UTILS::WeakPtr<ECS::Component> component, const rttr::property& prop) {
	const std::string propName = prop.get_name().to_string();
	std::unique_ptr<rttr::instance> inst;
	getInstance(component, inst);
	ImGui::Text(propName.c_str());
	auto val = prop.get_value(*inst).get_value<ECS::MaterialRenderer*>();
	int i = 0;
	const auto& names = val->getMaterialNames();
	for (auto& e : val->getMaterials()) {
		if (!e) {
			break;
		}
		ImGui::PushID(i);
		if (ImGui::InputText(names[i].c_str(), &e->mPath, ImGuiInputTextFlags_EnterReturnsTrue)) {
			//TODO: check file exist
			val->setMaterial(i, RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(e->mPath));
			//val->setMaterial(i, RESOURCES::MaterialLoader::Create(e->mPath));
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_FILE)) {
			popupStates["file_chooser"] = true;
			fileFormatsCb = [] {
				return ".mat";
			};
			fileChooserCb = [val, i](std::string path) mutable {
				if (val) {
					auto mat = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(path);
					//RESOURCES::MaterialLoader::Create(path);
					val->setMaterial(i, mat);
				}
			};
		}
		++i;
		ImGui::PopID();
	}
}

void widgetCombo(UTILS::WeakPtr<ECS::Component> component, const rttr::property& prop) {
	const std::string propName = prop.get_name().to_string();
	std::unique_ptr<rttr::instance> inst;
	getInstance(component, inst);
	auto val = prop.get_value(*inst);

	if (val.get_type().is_enumeration()) {
		const auto names = val.get_type().get_enumeration().get_names();
		const auto values = val.get_type().get_enumeration().get_values();
		const auto enumNames = std::vector(names.begin(), names.end());
		const auto enumValues = std::vector(values.begin(), values.end());
	
		int itemCurrentIndex = [&enumValues, &val]() {
			int i = 0;
			for (auto& e : enumValues) {
				if (e == val) {
					break;
				}
				i++;
			}
			return i;
		}();
		const auto comboLabel = enumNames[itemCurrentIndex].to_string();
		if (ImGui::BeginCombo(propName.c_str(), comboLabel.c_str())) {
			for (int n = 0; n < enumNames.size(); n++) {
				const bool isSelected = (itemCurrentIndex == n);
				if (ImGui::Selectable(enumNames[n].to_string().c_str(), isSelected)) {
					itemCurrentIndex = n;
					std::ignore = prop.set_value(*inst, enumValues[itemCurrentIndex]);
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}
}

void DebugRender::drawComponentInspector() {
	ImGui::Begin((ICON_FA_DATABASE + std::string(" Component Inspector")).c_str());
	if (!selectObj) {
		ImGui::Text("Object not selected");
		ImGui::End();
		return;
	}

	static std::unordered_map<std::string, std::string> iconComp = {
		{ "TransformComponent", ICON_FA_GLOBE },
		{ "SpotLight", ICON_FA_LIGHTBULB },
		{ "DirectionalLight", ICON_FA_SUN },
		{ "PointLight", ICON_FA_LIGHTBULB },
		{ "AmbientSphereLight", ICON_FA_CIRCLE },
		{ "AmbientLight", ICON_FA_SQUARE },
		{ "CameraComponent", ICON_FA_CAMERA },
		{ "AudioListenerComponent", ICON_FA_MUSIC },
		{ "AudioComponent", ICON_FA_MUSIC },
		{ "InputComponent", ICON_FA_HAND_POINT_UP },
		{ "LogicComponent", ICON_FA_CODE },
		{ "MaterialRenderer", ICON_FA_IMAGE },
		{ "ModelRenderer", ICON_FA_CUBE },
		{ "PhysicsComponent", ICON_FA_WEIGHT },
		{ "ScriptComponent", ICON_FA_CODE },
		{ "Skeletal", ICON_FA_SKULL },
	};

	static std::map<EditorMetaInfo::WidgetType, std::function<void(UTILS::WeakPtr<ECS::Component>, const rttr::property&)>> widgetDrawer = {
		{ EditorMetaInfo::DRAG_FLOAT_3, &widgetFloat3 },
		{ EditorMetaInfo::DRAG_COLOR_3, &widgetColor3 },
		{ EditorMetaInfo::DRAG_FLOAT, &widgetFloat },
		{ EditorMetaInfo::BOOL, &widgetBool },
		{ EditorMetaInfo::STRING, &widgetString },
		{ EditorMetaInfo::STRING_WITH_FILE_CHOOSE, &widgetStringFixFileChoose },
		{ EditorMetaInfo::COMBO, &widgetCombo },
		{ EditorMetaInfo::STRINGS_ARRAY, &widgetStringArray },
		{ EditorMetaInfo::MATERIAL, &widgetMaterial },
		{ EditorMetaInfo::MODEL_LOD, &widgetModelLod },
	};


	ImGui::Text(("Id: " + std::to_string(static_cast<int>(selectObj->getID()))).c_str());
	ImGui::Text("Name:");
	ImGui::SameLine();
	auto name = selectObj->getName();
	if (ImGui::InputText("##object_name", &name)) {
		selectObj->setName(name);
	}
	ImGui::Text("Tag:");
	ImGui::SameLine();
	auto tag = selectObj->getTag();
	if (ImGui::InputText("##object_tag", &tag)) {
		selectObj->setTag(tag);
	}


	//Add new component to object
	static auto componentNames = genComponentsStringArray();
	const auto& componentToAdd = drawSearchBox(componentNames);
	if (ImGui::Button("Add")) {
		if (!componentToAdd.empty()) {
			addComponentForObject(selectObj, componentToAdd);
		}
	}

	//Object components
	auto components = ECS::ComponentManager::GetInstance().getComponents(selectObj->getID());
	for (auto& component : components) {
		auto title = iconComp[component->getName()] + std::string(" ") + component->getName();
		bool needDelComponent = true;
		if (ImGui::CollapsingHeader(title.c_str(), &needDelComponent, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow)) {
			if (!needDelComponent) {
				removeComponentFromObject(selectObj, component->getTypeidName());
				break;
			}
			
			rttr::type t = rttr::type::get_by_name(component->getName());
			for (auto& prop : t.get_properties()) {
				const auto flagsData = prop.get_metadata(MetaInfo::FLAGS);
				if (!flagsData) {
					LOG_INFO("drawComponentInspector: '" + prop.get_name().to_string() + "' does not have " + MetaInfo::FLAGS);
					continue;
				}
				const auto flags = flagsData.get_value<MetaInfo::Flags>();

				if (flags & (MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)) {
					const std::string propName = prop.get_name().to_string();
					const auto wType = prop.get_metadata(EditorMetaInfo::EDIT_WIDGET).get_value<EditorMetaInfo::WidgetType>();
					widgetDrawer.at(wType)(component, prop);
				}
			}
		}
	}
	ImGui::End();
}

void DebugRender::drawTextureWatcher() {
#if 0
	// Obsolete
#endif
}

static MATHGL::Vector3 ProjectPointOntoRay(const MATHGL::Vector3& rayOrigin, const MATHGL::Vector3& rayDirection, const MATHGL::Vector3& point)
{
	MATHGL::Vector3 originToPoint = point - rayOrigin;

	// assume the direction is not normalized
	float dist = (rayDirection.dot(originToPoint)) / MATHGL::Vector3::Length(rayDirection);

	MATHGL::Vector3 result = rayDirection * dist;
	result = rayOrigin + result;

	return result;
}

static bool RaySphereIntersection(const MATHGL::Vector3& rayOrigin, const MATHGL::Vector3& rayDirection, const MATHGL::Vector3& sphereCenter, float radius, std::vector<MATHGL::Vector3>& hits)
{
	// make sure the direction is a unit vector
	MATHGL::Vector3 direction = MATHGL::Vector3::Normalize(rayDirection);

	MATHGL::Vector3 originToCenter = sphereCenter - rayOrigin;

	// check whether the center of the sphere is behind the ray origin
	if (MATHGL::Vector3::Dot(originToCenter, direction) < 0.0f)
	{
		// the sphere center is behind the ray -> intersection is only possible if the ray is within the sphere

		float distance = MATHGL::Vector3::Length(originToCenter);
		if (distance > radius)
		{
			// ray origin is outside the sphere
			return false;
		}
		else if (distance > (radius - 0.000001f) && distance < (radius + 0.000001f))
		{
			// ray origin is on the sphere
			hits.push_back(rayOrigin);
			return true;
		}
		else
		{
			// get the projection point from the sphere center onto the ray
			MATHGL::Vector3 projected = ProjectPointOntoRay(rayOrigin, direction, sphereCenter);

			// get the intersection point
			float lengthProjCenter = MATHGL::Vector3::Length(projected - sphereCenter);
			float dist = sqrtf((radius * radius) + (lengthProjCenter * lengthProjCenter));

			float lengthOriginIntersection = dist - MATHGL::Vector3::Length(projected - rayOrigin);

			MATHGL::Vector3 hit = rayOrigin + (direction * lengthOriginIntersection);
			hits.push_back(hit);

			return true;
		}

	}
	else
	{
		// the sphere center is in front of the ray

		MATHGL::Vector3 projected = ProjectPointOntoRay(rayOrigin, direction, sphereCenter);

		float lengthProjCenter = MATHGL::Vector3::Length(sphereCenter - projected);
		if (lengthProjCenter > radius)
		{
			// the projection point is outside the sphere -> no intersection
			return false;
		}
		else if (lengthProjCenter > (radius - 0.000001f) && lengthProjCenter < (radius + 0.000001f))
		{
			// the projection point is on the sphere
			hits.push_back(projected);
			return true;
		}

		float lengthProjIntersection1 = sqrtf((radius * radius) + (lengthProjCenter * lengthProjCenter));

		// check whether the ray origin is within the sphere
		if (MATHGL::Vector3::Length(originToCenter) < radius)
		{
			// there is only one intersection
			float lengthOriginIntersection = MATHGL::Vector3::Length(projected - rayOrigin) + lengthProjIntersection1;

			MATHGL::Vector3 hit = rayOrigin + (direction * lengthOriginIntersection);
			hits.push_back(hit);

			return true;
		}
		else
		{
			// there are two intersections
			// get the first intersection
			float lengthProjOrigin = MATHGL::Vector3::Length(projected - rayOrigin);
			float lengthOriginIntersection = lengthProjOrigin - lengthProjIntersection1;
			MATHGL::Vector3 hit = rayOrigin + (direction * lengthOriginIntersection);
			hits.push_back(hit);

			// get the second intersection point
			lengthOriginIntersection = lengthProjOrigin + lengthProjIntersection1;
			hit = rayOrigin + (direction * lengthOriginIntersection);
			hits.push_back(hit);

			return true;
		}

	}
}


void DebugRender::drawScene() {
#ifdef OPENGL_BACKEND
	ImGui::Begin("Scene Watcher", nullptr, gizmoWindowFlags);

	auto& renderer = RESOURCES::ServiceManager::Get<RENDER::GameRendererInterface>();
	auto _renderer = reinterpret_cast<RENDER::GameRendererGl*>(&renderer);

	//botton panel

	ImGui::Columns(2, nullptr, false);
	ImGui::SetColumnWidth(0, 40.0f);
	ImGui::Button(ICON_FA_ARROWS_ALT, ImVec2(36, 36));
	ImGui::Button(ICON_FA_SYNC_ALT, ImVec2(36, 36));
	ImGui::Button(ICON_FA_COMPRESS_ALT, ImVec2(36, 36));
	ImGui::NextColumn();

	float w = 0.0f;
	float h = 0.0f;
	auto val = _renderer->pingPongTex[!_renderer->pingPong];
	if (val) {
		auto winSize = RESOURCES::ServiceManager::Get<WINDOW_SYSTEM::Window>().getSize();
		ImVec2 imWinSize = ImGui::GetWindowSize();
		if (imWinSize.x < imWinSize.y) {
			w = imWinSize.x;
			h = (winSize.y * imWinSize.x) / winSize.x;
		}
		else
		{
			w = (imWinSize.y * winSize.x) / winSize.y;
			h = imWinSize.y;
		}

		if (h > 100.0f) {
			h -= 30.0f;
		} 
		ImGui::Image(reinterpret_cast<ImTextureID>((uintptr_t)val->id), ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
		if (ImGui::IsItemHovered()) 
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				ImVec2 screen_pos = ImGui::GetMousePos();

				ImGuiWindow* window = ImGui::GetCurrentWindowRead();
				screen_pos.x = screen_pos.x - window->Pos.x;
				screen_pos.y = screen_pos.y - window->Pos.y;


				auto startXY = ImGui::GetCursorPos();
				//startXY.x -= w;
				startXY.y -= h;

				auto x = (screen_pos.x - startXY.x);
				auto y = (screen_pos.y -	 startXY.y);

				auto screenRes = RESOURCES::ServiceManager::Get<WINDOW_SYSTEM::Window>().getSize();
				auto camera = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene().findMainCamera().value();
				auto scale = w / screenRes.x;

				x *= scale;
				y *= scale;

				MATHGL::Vector4 lRayStart_NDC(
					(x / screenRes.x - 0.5f) * 2.0f, // [0,1024] -> [-1,1]
					(y / screenRes.y - 0.5f) * 2.0f, // [0, 768] -> [-1,1]
					-1.0, // The near plane maps to Z=-1 in Normalized Device Coordinates
					1.0f
				);
				MATHGL::Vector4 lRayEnd_NDC(
					(x / screenRes.x - 0.5f) * 2.0f,
					(y / screenRes.y - 0.5f) * 2.0f,
					0.0,
					1.0f
				);

				auto InverseProjectionMatrix = MATHGL::Matrix4::Inverse(camera->getCamera().getProjectionMatrix());
				
				auto InverseViewMatrix = MATHGL::Matrix4::Inverse(camera->getCamera().getViewMatrix());


				MATHGL::Vector4 lRayStart_camera = InverseProjectionMatrix * lRayStart_NDC;
				lRayStart_camera = lRayStart_camera / lRayStart_camera.w;

				MATHGL::Vector4 lRayStart_world = InverseViewMatrix * lRayStart_camera;
				lRayStart_world = lRayStart_world / lRayStart_world.w;

				MATHGL::Vector4 lRayEnd_camera = InverseProjectionMatrix * lRayEnd_NDC;
				lRayEnd_camera = lRayEnd_camera / lRayEnd_camera.w;
				MATHGL::Vector4 lRayEnd_world = InverseViewMatrix * lRayEnd_camera;
				lRayEnd_world = lRayEnd_world / lRayEnd_world.w;

				auto v = lRayEnd_world - lRayStart_world;
				MATHGL::Vector3 lRayDir_world(v.x, v.y, v.z);
				lRayDir_world = (lRayDir_world);
				//lRayDir_world = MATHGL::Vector3::Normalize(lRayDir_world);

				auto origin = camera->obj->mTransform->getWorldPosition();
				//MATHGL::Vector3 out_end = origin + lRayDir_world * 1000.0f;

				std::vector<UTILS::Ref<ECS::Object>> objs;

				for (const auto& modelRenderer : ECS::ComponentManager::GetInstance().getComponentArrayRef<ECS::ModelRenderer>()) {
					if (modelRenderer.obj.get().getIsActive()) {
						auto bs = modelRenderer.getModel()->getBoundingSphere();
						std::vector<MATHGL::Vector3> hits;
						auto _pos = bs.position + modelRenderer.obj.get().transform->getWorldPosition();
						auto scale = modelRenderer.obj.get().transform->getWorldScale();
						auto _r = bs.radius * std::max(scale.x, std::max(scale.y, scale.z));
						if (RaySphereIntersection(origin, lRayDir_world, _pos, _r, hits)) {
							objs.push_back(modelRenderer.obj);
						}
					}
				}

				std::sort(objs.begin(), objs.end(), [origin](UTILS::Ref<ECS::Object> a, UTILS::Ref<ECS::Object> b) {
					auto _a = std::abs(MATHGL::Vector3::Distance(a->getTransform()->getWorldPosition(), origin));
					auto _b = std::abs(MATHGL::Vector3::Distance(b->getTransform()->getWorldPosition(), origin));

					return _a > _b;
				});
				std::cout << objs.size() << std::endl;
			}
		}
	}

	drawGuizmo(w, h);


	//ImGuiWindowFlags     flags = 0 | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
	//const ImGuiViewport* viewport = ImGui::GetMainViewport();
	//const ImVec2         pos = ImVec2(0.0f, 20.0f);
	//const ImVec2         size = ImVec2(viewport->WorkSize.x, 0.0f);
	//ImGui::SetNextWindowPos(pos);
	////ImGui::SetNextWindowSize(size);
	//ImGui::Begin("Dock", NULL, flags);
	////ImGui::SetWindowFocus("Dock");
	//ImGui::BeginGroup();
	//const float itemSpacing = 0.2f;
	//ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacing, ImGui::GetStyle().ItemSpacing.y));
	//if (ImGui::Button("!!!!!"))
	//{
	//
	//}
	//ImGui::PopStyleVar();
	//ImGui::EndGroup();
	//ImGui::End();



	ImGui::Columns(1);
	ImGui::End();

	


#endif
}


void DebugRender::drawEditorScene() {
#ifdef OPENGL_BACKEND
	ImGui::Begin("Scene Editor Watcher", nullptr, gizmoWindowFlags);

	auto& renderer = RESOURCES::ServiceManager::Get<RENDER::GameRendererInterface>();
	auto _renderer = reinterpret_cast<RENDER::GameRendererGl*>(&renderer);

	//botton panel

	ImGui::Columns(2, nullptr, false);
	ImGui::SetColumnWidth(0, 40.0f);
	ImGui::Button(ICON_FA_ARROWS_ALT, ImVec2(36, 36));
	ImGui::Button(ICON_FA_SYNC_ALT, ImVec2(36, 36));
	ImGui::Button(ICON_FA_COMPRESS_ALT, ImVec2(36, 36));
	ImGui::NextColumn();

	float w = 0.0f;
	float h = 0.0f;
	auto val = _renderer->mEditorTexture;
	if (val) {
		auto winSize = RESOURCES::ServiceManager::Get<WINDOW_SYSTEM::Window>().getSize();
		ImVec2 imWinSize = ImGui::GetWindowSize();
		if (imWinSize.x < imWinSize.y) {
			w = imWinSize.x;
			h = (winSize.y * imWinSize.x) / winSize.x;
		}
		else
		{
			w = (imWinSize.y * winSize.x) / winSize.y;
			h = imWinSize.y;
		}

		if (h > 100.0f) {
			h -= 30.0f;
		}
		ImGui::Image(reinterpret_cast<ImTextureID>((uintptr_t)val->id), ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
	}

	drawGuizmo(w, h);




	ImGui::Columns(1);
	ImGui::End();




#endif
}


void DebugRender::drawStats() {
	static std::vector<float> values(100, 0.0f);
	static int valuesOffset = 0;
	static float updateTime = 0;
	static float timeToUpdate = 0;
	static double fps = 0.0;
	static double dt;

	ImGui::Begin("Stats");
	auto& timer = TIME::Timer::GetInstance();
	if (timeToUpdate <= 0.0) {
		fps = timer.getFPS();
		dt = timer.getDeltaTimeUnscaled().count();
		values[valuesOffset] = fps;
		valuesOffset = (valuesOffset + 1) % values.size();
		timeToUpdate = updateTime;
	}
	else {
		timeToUpdate -= timer.getDeltaTimeUnscaled().count();
	}
	if (SliderFloatWithSteps("Update time", &updateTime, 0.0f, 1.0f, 0.10f)) {
		timeToUpdate = updateTime;
	}
	ImGui::Text("FPS: %f", fps);
	ImGui::Text("Delta: %f", dt);
	ImGui::PlotLines("##FPSGraph", values.data(), values.size(), valuesOffset, nullptr, -10.0f, 500.0f, ImVec2(0, 80.0f));
	ImGui::End();
}

imgui_addons::ImGuiFileBrowser file_dialog;
void DebugRender::drawPopup() {
	//TODO: move to init
	//file_dialog.current_path = Config::ROOT + Config::ASSETS_PATH;


	bool open = true;
	if (popupStates.contains("create_new_scene") && popupStates["create_new_scene"]) {
		ImGui::OpenPopup("create_new_scene");
		popupStates["create_new_scene"] = false;
	}
	if (ImGui::BeginPopupModal("create_new_scene", &open)) {
		ImGui::TextWrapped("Write name for scene");
		static std::string name;
		if (ImGui::InputText("Scene name", &name)) {
			
		}

		if (ImGui::Button("OK", ImVec2(120, 0))) {
			RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().loadEmptyScene();
			RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().setCurrentSceneSourcePath(name);
			name = "";
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (popupStates.contains("create_new_material") && popupStates["create_new_material"]) {
		ImGui::OpenPopup("create_new_material");
		popupStates["create_new_material"] = false;
	}
	if (ImGui::BeginPopupModal("create_new_material", &open)) {
		ImGui::TextWrapped("Write name for material");
		static std::string name;
		if (ImGui::InputText("Material name", &name, ImGuiInputTextFlags_EnterReturnsTrue)) {
			auto path = Config::ROOT + Config::USER_ASSETS_PATH + "Materials/" + name + ".mat";
			std::ofstream outfile(path);
			const static std::string materialConf = R"(
{
	"shaderFragment": "Shaders/gl/deferredGBuffer.fs.glsl",
	"shaderVertex": "Shaders/gl/deferredGBuffer.vs.glsl",
	"blendable": false,
	"backfaceCulling": true,
	"frontfaceCulling": false,
	"depthTest": true,
	"depthWriting": true,
	"colorWriting": true,
	"gpuInstances": 1,
	"isDeferred": true,
	"uniforms": {
		"u_AlbedoMap": "textures\\brick_albedo.jpg",
		"u_NormalMap": "textures\\brick_normal.jpg",
		"u_TextureTiling": [ 1.0, 1.0 ],
		"u_TextureOffset": [ 0.0, 0.0 ],
		"u_Albedo": [ 1.0, 0.0, 0.0, 1.0 ],
		"u_Specular": [ 1.0, 1.0, 1.0 ],
		"u_Shininess": 100.0,
		"u_HeightScale": 0.0,
		"u_EnableNormalMapping": true
	}
}
)";
			outfile << materialConf << std::endl;
			outfile.close();
		}

		if (ImGui::Button("OK", ImVec2(120, 0))) {
			RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().loadEmptyScene();
			RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().setCurrentSceneSourcePath(name);
			name = "";
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}


	if (popupStates.contains("file_chooser") && popupStates["file_chooser"]) {
		ImGui::OpenPopup("Open File");
		popupStates["file_chooser"] = false;
	}
	if (fileFormatsCb && file_dialog.showFileDialog("Open File", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), fileFormatsCb())) {
		fileChooserCb(file_dialog.selected_path);
		std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
		std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file
		fileFormatsCb = nullptr;
		fileChooserCb = nullptr;
	}

}


void DebugRender::drawMainWindow()
{
	return;
	static bool isOpen = true;
	//static const ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;
	//ImGuiID dockSpace = ImGui::GetID("MainWindowDockspace");
	//ImGui::DockSpace(dockSpace, ImVec2(0.0f, 0.0f), dockspaceFlags);
	{//menu
		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("Menu"))
			{
				if (ImGui::MenuItem("Save Current Scene", "CTRL+S")) {
					if (RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().hasCurrentScene()) {
						RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().saveToFile();
					}
				}
				if (ImGui::MenuItem("Create new scene")) {
					popupStates["create_new_scene"] = true;
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Examples"))
			{
				//ImGui::MenuItem("Main menu bar", NULL, &[](){});
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}

		drawPopup();
	}

#ifdef IMGUI_HAS_VIEWPORT
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	auto h = ImGui::GetFrameHeight();
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + h), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - h), ImGuiCond_Always);
	ImGui::SetNextWindowViewport(viewport->ID);
#else 
	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
#endif



	ImGui::Begin("##MainWin", &isOpen,
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar);
	
	ImGui::End();
}


struct ImGuiLogger {
	std::list<TextEditor::Record> mBuffer;
	int mBufferSize = 10;
	std::string mSearch;

	TextEditor editor;

	ImGuiLogger() {
		editor.SetReadOnly(true);\
		auto lang = TextEditor::LanguageDefinition::Logger();
		editor.SetLanguageDefinition(lang);
		editor.SetPalette(TextEditor::GetLoggerPalette());
	}
	
	bool ScrollToBottom = true;

	void addLog(const TextEditor::Record& record) {
		mBuffer.push_back(record);
		while (mBuffer.size() > mBufferSize) {
			mBuffer.pop_front();
		}
		editor.SetTextLogLines(mBuffer, mLogLevels, mSearch);
	}

	void addLog(SomeLogger::LoggerLevel level, const std::string& word) {
		addLog({
			std::format("{:<8} {:<7.3f} - {}", mLogLevelsToString.at(level), ImGui::GetTime(), word),
			level
		});
	}

	void clear() {
		mBuffer.clear();
	}

	std::map<SomeLogger::LoggerLevel, bool> mLogLevels = {
		{SomeLogger::LoggerLevel::DEBUG, true},
		{SomeLogger::LoggerLevel::ERR, true},
		{SomeLogger::LoggerLevel::INFO, true},
		{SomeLogger::LoggerLevel::WARNING, true},
	};
	const std::map<SomeLogger::LoggerLevel, std::string> mLogLevelsToString = {
		{SomeLogger::LoggerLevel::DEBUG, "DEBUG:"},
		{SomeLogger::LoggerLevel::ERR, "ERROR:"},
		{SomeLogger::LoggerLevel::INFO, "INFO:"},
		{SomeLogger::LoggerLevel::WARNING, "WARNING:"},
	};

	void draw() {
		ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
		static bool isOpen = true;
		ImGui::Begin("Logger", &isOpen);

		if (ImGui::Button("Clear")) {
			clear();
		}
		ImGui::SameLine();
		if (ImGui::Checkbox("ERROR", &mLogLevels[SomeLogger::LoggerLevel::ERR])) { editor.SetTextLogLines(mBuffer, mLogLevels, mSearch); }
		ImGui::SameLine();
		if (ImGui::Checkbox("DEBUG", &mLogLevels[SomeLogger::LoggerLevel::DEBUG])) { editor.SetTextLogLines(mBuffer, mLogLevels, mSearch); }
		ImGui::SameLine();
		if (ImGui::Checkbox("INFO", &mLogLevels[SomeLogger::LoggerLevel::INFO])) { editor.SetTextLogLines(mBuffer, mLogLevels, mSearch); }
		ImGui::SameLine();
		if (ImGui::Checkbox("WARNING", &mLogLevels[SomeLogger::LoggerLevel::WARNING])) { editor.SetTextLogLines(mBuffer, mLogLevels, mSearch); }
		ImGui::SameLine();
		if (ImGui::InputText("Search", &mSearch)) { editor.SetTextLogLines(mBuffer, mLogLevels, mSearch); }

		ImGui::Separator();

		if (ImGui::InputText("Run", &mSearch, ImGuiInputTextFlags_EnterReturnsTrue)) {
			//TODO: run script
		}
		ImGui::Separator();

		ImGui::BeginChild("##logscrolling");
		editor.Render("TextEditor");
		ImGui::EndChild();

		ImGui::End();
	}
};

void DebugRender::drawConsole() {
	static ImGuiLogger log;

	ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
	ImGui::Begin("Example: Log");
	if (ImGui::SmallButton("[Debug] Add 5 entries"))
	{
		static int counter = 0;
		const SomeLogger::LoggerLevel categories[4] = { SomeLogger::LoggerLevel::DEBUG, SomeLogger::LoggerLevel::INFO, SomeLogger::LoggerLevel::ERR, SomeLogger::LoggerLevel::WARNING };
		const std::string words[] = { "Bumfuzzled", "Cattywampus", "Snickersnee", "Abibliophobia", "Absquatulate", "Nincompoop", "Paucaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaailoquent" };
		for (int n = 0; n < 5; n++) {
			auto category = categories[counter % IM_ARRAYSIZE(categories)];
			const auto& word = words[counter % IM_ARRAYSIZE(words)];
			log.addLog(category, word);
			counter++;
		}
	}
	ImGui::End();
	log.draw();
}

int frameId = 0;
std::thread::id frameThreadId;


static void ProfilerValueGetter(float* startTimestamp, float* endTimestamp, ImU8* level, const char** caption, const void* _data, int idx) {
	auto& reports = PROFILER::Profiler::getReportHistory();

	auto& frame = reports[frameId];
	auto& data =  frame.mCallsTree.at(frameThreadId);

	if (caption) *caption = data[idx].mName.c_str();
	if (level) *level = data[idx].level;
	if (startTimestamp) *startTimestamp = data[idx].mStart;
	if (endTimestamp) *endTimestamp = data[idx].mStart + data[idx].mDuration;
}



void DebugRender::drawProfiler() {
	static bool isOpen = true;
	auto& reports = PROFILER::Profiler::getReportHistory();

	ImGui::Begin("Profiler Window", &isOpen);
	static bool isProfileOn = false;
	if (ImGui::Checkbox("Is Enabled", &isProfileOn)) {
		PROFILER::Profiler::ToggleEnable();
		//PROFILER::Profiler::ClearHistory();
	}
	if (/*!isProfileOn ||*/ reports.size() < frameId + 1) {
		ImGui::Text("Profiler is off");
		ImGui::End();
		return;
	}

	auto fid = ImGuiWidgetFlameGraph::PlotHistogramPressed("Histogram", PROFILER::Profiler::getReportHistoryDurations().data(),
		PROFILER::Profiler::getReportHistoryDurations().size(), 0, NULL, 0.001f, 1.0f, ImVec2(400, 80.0f));
	if (fid >= 0) {
		frameId = fid;
	}

	auto& frame = reports[frameId];
	for (auto& e : frame.mCallsTree) {
		frameThreadId = e.first;
		ImGuiWidgetFlameGraph::PlotFlame("CPU", &ProfilerValueGetter, nullptr, e.second.size(), 0, "Main Thread", FLT_MAX, FLT_MAX, ImVec2(400, 0));
		break;
	}
	ImGui::End();
}

#include <../../3rd/imgui/imgui/ImCurveEdit.h>
#include <utilsModule/imguiWidgets/ImSequencer.h>

//#include "../utils/refl.hpp"

template<class T>
void addWrappersForComponent(ANIMATION::Animation& anim, const AnimationLineInfo& data, T& component) {
	auto id = component.getObject().getID();
	std::string prefix = std::to_string(static_cast<int>(id));

	rttr::type t = rttr::type::get<typename std::remove_reference<T>::type>();
	const auto prop = t.get_property(data.propName);
	
	anim.addProperty(ANIMATION::AnimationProperty(
	prefix + data.propName,
	[prop, id](ANIMATION::PropType val) {
			auto component = ECS::ComponentManager::GetInstance().getComponent<T>(id);
			prop.set_value(*component.get(), std::get<float>(val));
		},
		[prop, id]() {
			auto component = ECS::ComponentManager::GetInstance().getComponent<T>(id);
			//TODO::
			return prop.get_value(*component.get()).get_value<float>();
		}
	));
}


//std::unordered_map<ComponentName, std::map<PromName, PropValue>> componentsProperties;


struct MySequence : public ImSequencer::SequenceInterface {
	std::unique_ptr<IKIGAI::ANIMATION::Animation> animation;

	virtual int GetFrameMin() const {
		return mFrameMin;
	}
	virtual int GetFrameMax() const {
		return mFrameMax;
	}
	virtual int GetItemCount() const { return (int)myItems.size(); }

	virtual int GetItemTypeCount() const {
		return SequencerItemTypeNames.size();
	}

	virtual const std::string GetItemTypeName(int typeIndex) const {
		return SequencerItemTypeNames[typeIndex].name + ":" + 
			SequencerItemTypeNames[typeIndex].componentName + ":" + 
			SequencerItemTypeNames[typeIndex].propName;
	}

	virtual const char* GetItemLabel(int index) const
	{
		static char tmps[512];
		snprintf(tmps, 512, "[%02d]", index);
		return tmps;
	}

	virtual void Get(int index, std::vector<std::pair<int, int>>& intervals, int* type, unsigned int* color) override
	{
		MySequenceItem& item = myItems[index];
		if (color)
			*color = 0xFFAA8080; // same color for everyone, return color based on type
		intervals = item.intervals;
		if (type)
			*type = item.mType;
	}

	virtual void RemoveInterval(int eId, int iId) override {
		if (eId < 0 || iId < 0) {
			return;
		}

		std::map<std::string, ANIMATION::PropType> prop;
		//for (auto& e : myItems[eId].intervalsValues[iId]) {
		auto& e = myItems[eId].intervalsValues[iId];
		prop[std::to_string(static_cast<int>(myItems[eId].info.id)) + e.first] = e.second;
		//}
		animation->delKeyFrameProp(myItems[eId].intervals[iId].first, prop);

		myItems[eId].intervals.erase(myItems[eId].intervals.begin() + iId);
		myItems[eId].intervalsValues.erase(myItems[eId].intervalsValues.begin() + iId);
	}

	virtual void AddInterval(int eId, int frame) override {
		if (eId < 0 || frame < 0) {
			return;
		}
		myItems[eId].intervals.push_back(std::make_pair(frame, frame));
		myItems[eId].intervalsValues.push_back(std::make_pair(myItems[eId].info.propName, myItems[eId].info.value));
		

		std::map<std::string, ANIMATION::PropType> prop;
		auto& e = myItems[eId].intervalsValues.back();
		//for (auto& e : myItems[eId].intervalsValues.back()) {

			prop[std::to_string(static_cast<int>(myItems[eId].info.id)) + e.first] = e.second;
		//}
		animation->delKeyFrameProp(myItems[eId].intervals.back().first, prop);
	}

	virtual void UpdateFPS(unsigned int fps) override {
		animation->setFPS(fps);
	}
	virtual void UpdateMaxFrame(unsigned int mframe) override {
		animation->setFrameCount(mframe);
	}

	virtual void SetIsLooped(bool b) override {
		animation->setLooped(b);
	}


	virtual void Set(int index, std::vector<std::pair<int, int>>& intervals) override
	{
		MySequenceItem& item = myItems[index];
		item.intervals = intervals;
	}

	virtual void Add(int type) {
		auto& data = SequencerItemTypeNames[type];

		static int lineId = 0;
		myItems.push_back(MySequenceItem{ data, lineId++, {}, false, {} });

		//if (data.componentName == "Transform") {
			//addWrappersForComponent(*animation, selectObj->getComponent<ECS::TransformComponent>().value().get());
		//}
		std::visit(
			[&data, this](auto& arg) {
				addWrappersForComponent(*animation, data, *selectObj->getComponent<std::remove_reference_t<decltype(*arg)>>().get());
			}, 
			data.component);
	};

	//TODO: del from anim
	virtual void Del(int index) { myItems.erase(myItems.begin() + index); }

	virtual void Duplicate(int index) { myItems.push_back(myItems[index]); }

	virtual size_t GetCustomHeight(int index) { return myItems[index].mExpanded ? 300 : 0; }

	MySequence() : mFrameMin(0), mFrameMax(0)
	{
		animation = std::make_unique<ANIMATION::Animation>(mFrameMax, FPS, false);

		//componentsProperties["Transform"] = {
		//	{"posX", 0.0f},
		//	{"posY", 0.0f},
		//	{"posZ", 0.0f},
		//	{"rotX", 0.0f},
		//	{"rotY", 0.0f},
		//	{"rotZ", 0.0f},
		//	{"scaleX", 0.0f},
		//	{"scaleY", 0.0f},
		//	{"scaleZ", 0.0f}
		//};
	}
	int FPS = 30;
	int mFrameMin, mFrameMax;
	struct MySequenceItem
	{
		AnimationLineInfo info;
		int mType;
		std::vector<std::pair<int, int>> intervals;
		bool mExpanded;
		std::vector<std::pair<PromName, PropValue>> intervalsValues;
	};
	std::vector<MySequenceItem> myItems;
	//RampEdit rampEdit;

	virtual void DoubleClick(int index) {
		//if (myItems[index].mExpanded)
		//{
		//	myItems[index].mExpanded = false;
		//	return;
		//}
		//for (auto& item : myItems)
		//	item.mExpanded = false;
		//myItems[index].mExpanded = !myItems[index].mExpanded;
	}

	void applyFrame(MySequenceItem& item, int intervalId) {
		std::map<std::string, ANIMATION::PropType> prop;
		//for (auto& e : item.intervalsValues[intervalId]) {
		auto& e = item.intervalsValues[intervalId];
		prop[std::to_string(static_cast<int>(item.info.id)) + e.first] = e.second;
		//}
		animation->addKeyFrameMerge(item.intervals[intervalId].first, prop);
	}

	virtual void UpdateFrame(int id, int frameId, int o, int n) override
	{
		auto& item = myItems[id];
		updateFrame(item, frameId, o, n);
	};

	void updateFrame(MySequenceItem& item, int frameId, int oldId, int newId) {
		std::map<std::string, ANIMATION::PropType> prop;
		//for (auto& e : item.intervalsValues[frameId]) {
		auto& e = item.intervalsValues[frameId];
		prop[std::to_string(static_cast<int>(item.info.id)) + e.first] = e.second;
		//}

		animation->delKeyFrameProp(oldId, prop);
		animation->addKeyFrameMerge(newId, prop);
	}

	virtual void Update()
	{
		animation->update(TIME::Timer::GetInstance().getDeltaTime().count() * 1000.0f);
	}

	virtual void FrameDraw(int index, int intervalId, ImDrawList* draw_list)
	{
		if (index < 0 || intervalId < 0) {
			return;
		}
		//static const char* labels[] = { "Translation", "Rotation" , "Scale" };
		//ImGui::SameLine();
		//ImGui::SetCursorScreenPos(rc.Min);
		ImGui::Begin("##EditFrame");
		//static float[3];
		auto& item = myItems[index];
		auto& e = item.intervalsValues[intervalId];

		//for (auto& e : itemInterval) {
			std::string name = e.first;
			std::visit([this, name, &e, &item, intervalId](auto&& arg) {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, int>) {
					if (ImGui::DragInt(name.c_str(), &arg)) {
						e.second = arg;
						applyFrame(item, intervalId);
					}
				}
				else if constexpr (std::is_same_v<T, float>) {
					if (ImGui::DragFloat(name.c_str(), &arg)) {
						e.second = arg;
						applyFrame(item, intervalId);
					}
				}
				else if constexpr (std::is_same_v<T, bool>) {
					if (ImGui::Checkbox(name.c_str(), &arg)) {
						e.second = arg;
						applyFrame(item, intervalId);
					}
				}
				else if constexpr (std::is_same_v<T, std::string>) {
					if (ImGui::InputText(name.c_str(), &arg)) {
						e.second = arg;
						applyFrame(item, intervalId);
					}
				}
			}, e.second);
		//}

		//for (auto& e : itemInterval.second) {
		//	std::string name = "End " + e.first;
		//	std::visit([name, &e](auto&& arg) {
		//		using T = std::decay_t<decltype(arg)>;
		//		if constexpr (std::is_same_v<T, int>) {
		//			if (ImGui::DragInt(name.c_str(), &arg)) {
		//				e.second = arg;
		//			}
		//		}
		//		else if constexpr (std::is_same_v<T, float>) {
		//			if (ImGui::DragFloat(name.c_str(), &arg)) {
		//				e.second = arg;
		//			}
		//		}
		//		else if constexpr (std::is_same_v<T, bool>) {
		//			if (ImGui::Checkbox(name.c_str(), &arg)) {
		//				e.second = arg;
		//			}
		//		}
		//		else if constexpr (std::is_same_v<T, std::string>) {
		//			if (ImGui::InputText(name.c_str(), &arg)) {
		//				e.second = arg;
		//			}
		//		}
		//	}, e.second);
		//}
		//ImGui::DragFloat3("Transform", e);
		//ImGui::DragFloat3("Rotation", e);
		//ImGui::DragFloat3("Scale", e);
		ImGui::End();
		//rampEdit.mMax = ImVec2(float(mFrameMax), 1.f);
		//rampEdit.mMin = ImVec2(float(mFrameMin), 0.f);
		//draw_list->PushClipRect(legendClippingRect.Min, legendClippingRect.Max, true);
		//for (int i = 0; i < 3; i++)
		//{
		//	ImVec2 pta(legendRect.Min.x + 30, legendRect.Min.y + i * 14.f);
		//	ImVec2 ptb(legendRect.Max.x, legendRect.Min.y + (i + 1) * 14.f);
		//	draw_list->AddText(pta, rampEdit.mbVisible[i] ? 0xFFFFFFFF : 0x80FFFFFF, labels[i]);
		//	if (ImRect(pta, ptb).Contains(ImGui::GetMousePos()) && ImGui::IsMouseClicked(0))
		//		rampEdit.mbVisible[i] = !rampEdit.mbVisible[i];
		//}
		//draw_list->PopClipRect();
		//
		//ImGui::SetCursorScreenPos(rc.Min);
		//
		//ImCurveEdit::Edit(rampEdit, { rc.Max.x - rc.Min.x, rc.Max.y - rc.Min.y }, 137 + index, &clippingRect);
	}

	virtual void CustomDrawCompact(int index, ImDrawList* draw_list, const ImRect& rc, const ImRect& clippingRect)
	{
		//rampEdit.mMax = ImVec2(float(mFrameMax), 1.f);
		//rampEdit.mMin = ImVec2(float(mFrameMin), 0.f);
		//draw_list->PushClipRect(clippingRect.Min, clippingRect.Max, true);
		//for (int i = 0; i < 3; i++)
		//{
		//	for (int j = 0; j < rampEdit.mPointCount[i]; j++)
		//	{
		//		float p = rampEdit.mPts[i][j].x;
		//		if (p < myItems[index].mFrameStart || p > myItems[index].mFrameEnd)
		//			continue;
		//		float r = (p - mFrameMin) / float(mFrameMax - mFrameMin);
		//		float x = ImLerp(rc.Min.x, rc.Max.x, r);
		//		draw_list->AddLine(ImVec2(x, rc.Min.y + 6), ImVec2(x, rc.Max.y - 4), 0xAA000000, 4.f);
		//	}
		//}
		//draw_list->PopClipRect();
	}
};

MySequence mySequence;

void DebugRender::draw(CORE_SYSTEM::Core& core) {

#ifdef OPENGL_BACKEND
	if (RENDER::DriverInterface::settings.backend == RENDER::RenderSettings::Backend::OPENGL) {
		if (auto* imgui = IKIGAI::IMGUI::Get()) {
			imgui->newFrame();
		}
	}
#endif
#ifdef VULKAN_BACKEND
	if (RENDER::DriverInterface::settings.backend == RENDER::RenderSettings::Backend::VULKAN) {
		if (auto* imgui = IKIGAI::IMGUI::Get()) {
			imgui->newFrame();
		}
	}
#endif

#ifdef DX12_BACKEND
	if (RENDER::DriverInterface::settings.backend == RENDER::RenderSettings::Backend::DIRECTX12) {
		if (auto* imgui = IKIGAI::IMGUI::Get()) {
			imgui->newFrame();
		}
	}
#endif

#if defined(OPENGL_BACKEND) || defined(VULKAN_BACKEND)
	drawMainWindow();
#endif
	{//input
		std::array keys = {
			INPUT_SYSTEM::EKey::KEY_TAB,
			INPUT_SYSTEM::EKey::KEY_LEFT,
			INPUT_SYSTEM::EKey::KEY_RIGHT,
			INPUT_SYSTEM::EKey::KEY_UP,
			INPUT_SYSTEM::EKey::KEY_DOWN,
			INPUT_SYSTEM::EKey::KEY_PAGE_UP,
			INPUT_SYSTEM::EKey::KEY_PAGE_DOWN,
			INPUT_SYSTEM::EKey::KEY_HOME,
			INPUT_SYSTEM::EKey::KEY_END,
			INPUT_SYSTEM::EKey::KEY_INSERT,
			INPUT_SYSTEM::EKey::KEY_DELETE,
			INPUT_SYSTEM::EKey::KEY_BACKSPACE,
			INPUT_SYSTEM::EKey::KEY_SPACE,
			INPUT_SYSTEM::EKey::KEY_ENTER,
			INPUT_SYSTEM::EKey::KEY_ESCAPE,
			INPUT_SYSTEM::EKey::KEY_LEFT_CONTROL,
			INPUT_SYSTEM::EKey::KEY_LEFT_SHIFT,
			INPUT_SYSTEM::EKey::KEY_LEFT_ALT,
			INPUT_SYSTEM::EKey::KEY_LEFT_SUPER,
			INPUT_SYSTEM::EKey::KEY_RIGHT_CONTROL,
			INPUT_SYSTEM::EKey::KEY_RIGHT_SHIFT,
			INPUT_SYSTEM::EKey::KEY_RIGHT_ALT,
			INPUT_SYSTEM::EKey::KEY_RIGHT_SUPER,
			INPUT_SYSTEM::EKey::KEY_MENU,
			INPUT_SYSTEM::EKey::KEY_0,
			INPUT_SYSTEM::EKey::KEY_1,
			INPUT_SYSTEM::EKey::KEY_2,
			INPUT_SYSTEM::EKey::KEY_3,
			INPUT_SYSTEM::EKey::KEY_4,
			INPUT_SYSTEM::EKey::KEY_5,
			INPUT_SYSTEM::EKey::KEY_6,
			INPUT_SYSTEM::EKey::KEY_7,
			INPUT_SYSTEM::EKey::KEY_8,
			INPUT_SYSTEM::EKey::KEY_9,
			INPUT_SYSTEM::EKey::KEY_A,
			INPUT_SYSTEM::EKey::KEY_B,
			INPUT_SYSTEM::EKey::KEY_C,
			INPUT_SYSTEM::EKey::KEY_D,
			INPUT_SYSTEM::EKey::KEY_E,
			INPUT_SYSTEM::EKey::KEY_F,
			INPUT_SYSTEM::EKey::KEY_G,
			INPUT_SYSTEM::EKey::KEY_H,
			INPUT_SYSTEM::EKey::KEY_I,
			INPUT_SYSTEM::EKey::KEY_J,
			INPUT_SYSTEM::EKey::KEY_K,
			INPUT_SYSTEM::EKey::KEY_L,
			INPUT_SYSTEM::EKey::KEY_M,
			INPUT_SYSTEM::EKey::KEY_N,
			INPUT_SYSTEM::EKey::KEY_O,
			INPUT_SYSTEM::EKey::KEY_P,
			INPUT_SYSTEM::EKey::KEY_Q,
			INPUT_SYSTEM::EKey::KEY_R,
			INPUT_SYSTEM::EKey::KEY_S,
			INPUT_SYSTEM::EKey::KEY_T,
			INPUT_SYSTEM::EKey::KEY_U,
			INPUT_SYSTEM::EKey::KEY_V,
			INPUT_SYSTEM::EKey::KEY_W,
			INPUT_SYSTEM::EKey::KEY_X,
			INPUT_SYSTEM::EKey::KEY_Y,
			INPUT_SYSTEM::EKey::KEY_Z,
			INPUT_SYSTEM::EKey::KEY_F1,
			INPUT_SYSTEM::EKey::KEY_F2,
			INPUT_SYSTEM::EKey::KEY_F3,
			INPUT_SYSTEM::EKey::KEY_F4,
			INPUT_SYSTEM::EKey::KEY_F5,
			INPUT_SYSTEM::EKey::KEY_F6,
			INPUT_SYSTEM::EKey::KEY_F7,
			INPUT_SYSTEM::EKey::KEY_F8,
			INPUT_SYSTEM::EKey::KEY_F9,
			INPUT_SYSTEM::EKey::KEY_F10,
			INPUT_SYSTEM::EKey::KEY_F11,
			INPUT_SYSTEM::EKey::KEY_F12,
		};
		
		int i = 512;
		for (auto e : keys) {
			if (ImGui::IsKeyPressed((ImGuiKey)i, false)) {
				RESOURCES::ServiceManager::Get<WINDOW_SYSTEM::Window>().keyPressedEvent.run(static_cast<int>(e));
			}
			else if (ImGui::IsKeyReleased((ImGuiKey)i)) {
				RESOURCES::ServiceManager::Get<WINDOW_SYSTEM::Window>().keyReleasedEvent.run(static_cast<int>(e));
			}
			++i;
		}
	}

	{//Debug
#ifdef OPENGL_BACKEND
		ImGui::Begin("Render pipeline");

		auto& renderer = RESOURCES::ServiceManager::Get<RENDER::GameRendererInterface>();
		auto _renderer = reinterpret_cast<RENDER::GameRendererGl*>(&renderer);

		static std::map<RENDER::RenderStates, std::function<void()>> stagesSettingsDraw = {
			{ RENDER::RenderStates::HDR, [_renderer]() {
				ImGui::DragFloat("Exposure", &_renderer->mPipeline.hdr.exposure);
				ImGui::DragFloat("Gamma", &_renderer->mPipeline.hdr.gamma);
			}},

			{ RENDER::RenderStates::VOLUMETRIC_LIGHT, [_renderer]() {
				ImGui::DragFloat("Asymmetry", &_renderer->mPipeline.vl.godRayAsymmetry);
				ImGui::DragFloat("MaxSteps", &_renderer->mPipeline.vl.godRayMaxSteps);
				ImGui::DragFloat("SampleStep", &_renderer->mPipeline.vl.godRaySampleStep);
				ImGui::DragFloat("Increment", &_renderer->mPipeline.vl.godRayStepIncrement);
				ImGui::DragFloat("Max dist", &_renderer->mPipeline.vl.maxDist);
				ImGui::Checkbox("Tex", &_renderer->mPipeline.vl.tex);
				ImGui::Checkbox("Dir", &_renderer->mPipeline.vl.dir);
				ImGui::Checkbox("Print Map", &_renderer->mPipeline.vl.map); 

			}},
		};

		static std::map<RENDER::RenderStates, bool> pp;
		int i = 0;
		for (auto& e : UTILS::Impl::Tokens<RENDER::RenderStates>) {
			if (!pp.contains(e.second)) {
				pp[e.second] = ((_renderer->renderStateMask & e.second) == e.second);
			}
			ImGui::PushID(i);
			if (ImGui::CollapsingHeader(std::string(e.first.begin(), e.first.end()).c_str())) {
				if (ImGui::Checkbox("Is enabled", &pp[e.second])) {
					_renderer->renderStateMask = pp[e.second] ? (_renderer->renderStateMask | e.second) : (_renderer->renderStateMask & ~e.second);
					_renderer->preparePipeline();
				}
				if (stagesSettingsDraw.contains(e.second)) {
					stagesSettingsDraw[e.second]();
				}
			}
			i++;
			ImGui::PopID();
		}

		if (!_renderer->customPostProcessing.empty()) {
			ImGui::SeparatorText("Custom PP");
			for (auto& e : _renderer->activeCustomPP) {
				if (ImGui::Checkbox(e.first.c_str(), &e.second)) {
					_renderer->preparePipeline();
				}
			}
		}

		ImGui::DragFloat("Dir shadow Near", &_renderer->mPipeline.mDirShadowMap.mPointNearPlane);
		ImGui::DragFloat("Dir shadow Far", &_renderer->mPipeline.mDirShadowMap.mDirFarPlane);
		_renderer->mPipeline.mDirShadowMap.mShadowCascadeLevels = {
			_renderer->mPipeline.mDirShadowMap.mDirFarPlane / 50.0f,
			_renderer->mPipeline.mDirShadowMap.mDirFarPlane / 25.0f,
			_renderer->mPipeline.mDirShadowMap.mDirFarPlane / 10.0f,
			_renderer->mPipeline.mDirShadowMap.mDirFarPlane / 2.0f
		};


		if (ImGui::CollapsingHeader("Skybox")) {
			if (!_renderer->skyBoxMaterial) {
				ImGui::Text(_renderer->mHDRSkyBoxTexture->mPath.c_str());
				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_FILE)) {
					popupStates["file_chooser"] = true;
					fileFormatsCb = [] {
						return ".hdr";
					};
					fileChooserCb = [_renderer](std::string path) {
						std::string _path = UTILS::ReplaceSubstrings(path, "\\", "/");
						auto pos = _path.find("Assets/Engine/");
						if (pos != std::string::npos) {
							_path = _path.substr(pos + 14);
						}
						pos = _path.find("Assets/Game/");
						if (pos != std::string::npos) {
							_path = _path.substr(pos + 12);
						}
						_renderer->setSkyBoxTexture(_path);
						_renderer->prepareIBL();
					};
				}
			}
			ImGui::PushID(11);
			ImGui::Text("Material:");
			ImGui::SameLine();
			ImGui::Text(_renderer->skyBoxMaterial ? _renderer->skyBoxMaterial->mPath.c_str() : "");
			if (ImGui::Button(ICON_FA_FILE)) {
				popupStates["file_chooser"] = true;
				fileFormatsCb = [] {
					return ".mat";
				};
				fileChooserCb = [_renderer](std::string path) {
					_renderer->setSkyBoxMaterial(RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(path));
				};
			}
			ImGui::SameLine();
			if (ImGui::Button("X")) {
				_renderer->setSkyBoxMaterial(nullptr);
			}
			ImGui::PopID();
		}
		ImGui::End();
#endif


		ImGui::Begin("Input Manager");
		ImGuiIO& io = ImGui::GetIO();
		ImGui::SeparatorText("Mouse");
		if (ImGui::IsMousePosValid())
			ImGui::Text("Mouse pos: (%g, %g)", io.MousePos.x, io.MousePos.y);
		else
			ImGui::Text("Mouse pos: <INVALID>");
		ImGui::Text("Mouse delta: (%g, %g)", io.MouseDelta.x, io.MouseDelta.y);
		ImGui::Text("Mouse down:");
		for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++) {
			if (ImGui::IsMouseDown(i)) {
				ImGui::SameLine();
				ImGui::Button(std::format("BTN {}", i).c_str(), ImVec2(0, 40));
				//ImGui::Text("b%d (%.02f secs)", i, io.MouseDownDuration[i]);
			}
		}
		ImGui::Text("Mouse wheel: %.1f", io.MouseWheel);
		ImGui::SeparatorText("Keys");
		struct funcs { static bool IsLegacyNativeDupe(ImGuiKey key) { return key < 512 && ImGui::GetIO().KeyMap[key] != -1; } }; // Hide Native<>ImGuiKey duplicates when both exists in the array
		ImGuiKey start_key = (ImGuiKey)0;
		ImGui::Text("Keys down:");
		for (ImGuiKey key = start_key; key < ImGuiKey_NamedKey_END; key = (ImGuiKey)(key + 1)) {
			if (funcs::IsLegacyNativeDupe(key) || !ImGui::IsKeyDown(key)) {
				continue;
			}
			ImGui::SameLine();
			ImGui::Text((key < ImGuiKey_NamedKey_BEGIN) ? "\"%s\"" : "\"%s\" %d", ImGui::GetKeyName(key), key);
		}
		ImGui::Text("Keys mods: %s%s%s%s", io.KeyCtrl ? "CTRL " : "", io.KeyShift ? "SHIFT " : "", io.KeyAlt ? "ALT " : "", io.KeySuper ? "SUPER " : "");
		ImGui::Text("Chars queue:");
		for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
			ImWchar c = io.InputQueueCharacters[i];
			ImGui::SameLine();
			ImGui::Text("\'%c\' (0x%04X)", (c > ' ' && c <= 255) ? (char)c : '?', c);
		}


		auto& im = RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputManager>();
		if (im.isGamepadExist(0)) {
			for (auto e : im.getGamepad(0).buttons) {
				ImGui::LabelText(("Btn" + std::to_string(static_cast<int>(e.first))).c_str(), std::to_string(e.second).c_str());
			}
			ImGui::LabelText("LStick", (std::to_string(im.getGamepad(0).leftSticX) + " " + std::to_string(im.getGamepad(0).leftSticY)).c_str());
			ImGui::LabelText("RStick", (std::to_string(im.getGamepad(0).rightSticX) + " " + std::to_string(im.getGamepad(0).rightSticX)).c_str());
			ImGui::LabelText("Triggers", (std::to_string(im.getGamepad(0).leftTrigger) + " " + std::to_string(im.getGamepad(0).rightTrigger)).c_str());
		}
		ImGui::End();

#ifdef OPENGL_BACKEND
		if (RENDER::DriverInterface::settings.backend == RENDER::RenderSettings::Backend::OPENGL) {
			if (editMaterial) {
				drawMaterialWidget(reinterpret_cast<RENDER::MaterialGl*>(editMaterial.get()));
			}
		}
#endif
		drawComponentInspector();
		drawTextureWatcher();
		drawScene();
		drawEditorScene();
	}

	drawStats();
	drawConsole();
	drawProfiler();

	drawWindowWidget(core);
	{
		drawNodeTree(core);
		//drawNodeTreeGui(core);
	}
	{
		//auto size = RESOURCES::ServiceManager::Get<WINDOW_SYSTEM::Window>().getSize();
		//ImGui::Begin("Scene Window");
		//ImVec2 pos = ImGui::GetCursorScreenPos();
		//ImGui::GetWindowDrawList()->AddImage(
		//	(void*)core.renderer->getResultTexture().getId(),
		//	ImVec2(ImGui::GetCursorScreenPos()),
		//	ImVec2(ImGui::GetCursorScreenPos().x + size.x / 2,
		//		ImGui::GetCursorScreenPos().y + size.y / 2), ImVec2(0, 1), ImVec2(1, 0));
		//ImGui::End();
	}
	{
		//auto size = RESOURCES::ServiceManager::Get<WINDOW_SYSTEM::Window>().getSize();
		//ImGui::Begin("Scene Window Light");
		//ImVec2 pos = ImGui::GetCursorScreenPos();
		//ImGui::GetWindowDrawList()->AddImage(
		//	(void*)core.renderer->getResultTexture2().getId(),
		//	ImVec2(ImGui::GetCursorScreenPos()),
		//	ImVec2(ImGui::GetCursorScreenPos().x + size.x / 2,
		//		ImGui::GetCursorScreenPos().y + size.y / 2), ImVec2(0, 1), ImVec2(1, 0));

		{
			//static float x = -20.0f;
			//static float y = 40.0f;
			//static float z = 10.0f;
			//if (ImGui::DragFloat("LightX", &x, 1.0f, -100.0f, 100.0f)) {
			//	core.renderer->setLightPos(x, y, z);
			//}
			//if (ImGui::DragFloat("LightY", &y, 1.0f, -100.0f, 100.0f)) {
			//	core.renderer->setLightPos(x, y, z);
			//}
			//if (ImGui::DragFloat("LightZ", &z, 1.0f, -100.0f, 100.0f)) {
			//	core.renderer->setLightPos(x, y, z);
			//}
		}

		//ImGui::End();
	}
	//ImGui::SFML::Render(*window);

	{
		fileBrowser->draw();
	}

	{
		ImGui::Begin("Seq");
		static int selectedEntry = -1;
		static int firstFrame = 0;
		static bool expanded = true;
		static int currentFrame = 100;

		static bool isInit = false;
		if (!isInit)
		{
			isInit = true;
			mySequence.mFrameMin = 0;
			mySequence.mFrameMax = 1000;
			//mySequence.myItems.push_back(MySequence::MySequenceItem{ 0, 10, 30, false });
			//mySequence.myItems.push_back(MySequence::MySequenceItem{ 1, 20, 30, true });
			//mySequence.myItems.push_back(MySequence::MySequenceItem{ 3, 12, 60, false });
			//mySequence.myItems.push_back(MySequence::MySequenceItem{ 2, 61, 90, false });
			//mySequence.myItems.push_back(MySequence::MySequenceItem{ 4, 90, 99, false });
		}

		ImGui::PushItemWidth(130);
		ImGui::InputInt("Frame Min", &mySequence.mFrameMin);
		ImGui::SameLine();
		ImGui::InputInt("Frame ", &currentFrame);
		ImGui::SameLine();
		if (ImGui::InputInt("Frame Max", &mySequence.mFrameMax)) {
			mySequence.UpdateMaxFrame(mySequence.mFrameMax);
		};
		static bool isLooped = false;
		if (ImGui::Checkbox("Is Looped", &isLooped)) {
			mySequence.SetIsLooped(isLooped);
		};
		if (ImGui::Button("Play")) {
			mySequence.animation->stop();
			mySequence.animation->play();
		};
		ImGui::PopItemWidth();
		ImSequencer::Sequencer(&mySequence, &currentFrame, &expanded, &selectedEntry, &firstFrame, ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD | ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_COPYPASTE | ImSequencer::SEQUENCER_CHANGE_FRAME);
		// add a UI to edit that particular item
		if (selectedEntry != -1)
		{
			const MySequence::MySequenceItem& item = mySequence.myItems[selectedEntry];
			ImGui::Text("I am a %s, please edit me", SequencerItemTypeNames[item.mType]);
			// switch (type) ....
		}
		ImGui::End();
	}

	ImGui::Render();

	if (auto* imgui = IKIGAI::IMGUI::Get()) {
		imgui->renderDrawData();
	}
#if defined(OPENGL_BACKEND) || defined(VULKAN_BACKEND)
	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		GLFWwindow* backup_current_context = glfwGetCurrentContext();
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		glfwMakeContextCurrent(backup_current_context);
	}
#endif
#if defined(DX12_BACKEND)
	//if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
	//	auto& gr = reinterpret_cast<RENDER::GameRendererDx12&>(RESOURCES::ServiceManager::Get<RENDER::GameRendererInterface>());
	//	auto render = reinterpret_cast<RENDER::GameRendererDx12&>(RESOURCES::ServiceManager::Get<RENDER::GameRendererInterface>()).mDriver;
	//
	//	ImGui::UpdatePlatformWindows();
	//	ImGui::RenderPlatformWindowsDefault(nullptr, (void*)render->mCommandList.Get());
	//}
#endif
	//window->popGLStates();
}

void DebugRender::postDraw() {

}
#endif
