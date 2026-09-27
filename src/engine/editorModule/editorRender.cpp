#include "editorRender.h"

#include "resourceEditorWindow.h"
#include "timelineAnimationWindow.h"

#include "utilsModule/log/loggerDefine.h"
#ifdef USE_EDITOR
#include "cameraControlWindow.h"
#include "componentManagerWindow.h"
#include "treeWindow.h"
#include <list>
#include "fileBrowser.h"
#include "statWindow.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "menuBar.h"
#include "coreModule/config.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/pathGetter.h"
#include "utilsModule/imguiHelper/ImGuiFileBrowser.h"
#include "renderModule/backends/interface/resourceStruct.h"

#ifdef DX12_BACKEND
#include "renderModule/gameRendererDx12.h"
#endif

#ifdef VULKAN_BACKEND
#include "renderModule/gameRendererVk.h"
#include "backends/imgui_impl_vulkan.h"
#endif


using namespace IKIGAI::EDITOR;

struct EditorRender::Internal {
	std::unique_ptr<MenuBar> mMenu;

	std::list<std::unique_ptr<EditorWindow>> mWindows;
	std::list<std::unique_ptr<EditorWindow>> mResWindows;

	Internal() {
		mMenu = std::make_unique<MenuBar>();

		mWindows.push_back(std::make_unique<CameraControlWindow>());
		mWindows.push_back(std::make_unique<ComponentManagerWindow>());
		mWindows.push_back(std::make_unique<ComponentInspectorWindow>());
		mWindows.push_back(std::make_unique<TreeWindow>());
		mWindows.push_back(std::make_unique<StatWindow>());
		mWindows.push_back(std::make_unique<TimelineAnimationWindow>());
		mWindows.push_back(std::make_unique<FileBrowserWindow>(Config::ROOT + Config::ASSETS_PATH));

		//RENDER::ShaderResource res;
		//mWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::ShaderResource>>(res));
	}
	
};

EditorRender::~EditorRender() = default;

EditorRender::EditorRender() {
	mData = std::make_unique<Internal>();

	//Init imgui style

	ImGuiIO& io = ImGui::GetIO(); (void)io;

	const float baseFontSize = 14.0f;
	ImFontConfig config;
	config.SizePixels = baseFontSize;
	io.Fonts->AddFontDefault(&config);

	const float iconFontSize = baseFontSize * 2.0f / 3.0f;

	// merge in icons from Font Awesome
	static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
	ImFontConfig icons_config;
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;
	icons_config.GlyphMinAdvanceX = iconFontSize;
	io.Fonts->AddFontFromFileTTF(UTILS::GetRealPath(std::string("fonts/") + FONT_ICON_FILE_NAME_FAS).c_str(), iconFontSize, &icons_config, icons_ranges);

	//ImGui_ImplVulkan_CreateFontsTexture();
	//#ifdef VULKAN_BACKEND
//	auto& render = reinterpret_cast<IKIGAI::RENDER::GameRendererVk&>(IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RENDER::GameRendererInterface>());
//	render.initForVk();
//#endif

//#ifdef DX12_BACKEND
//	auto& render = reinterpret_cast<IKIGAI::RENDER::GameRendererDx12&>(IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RENDER::GameRendererInterface>());
//#endif
}

void EditorRender::draw() {
	if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) {
		GlobalState.mCommandHistory.undo();
	}
	if ((ImGui::IsKeyPressed(ImGuiKey_Y, false) && ImGui::GetIO().KeyCtrl) ||
		(ImGui::IsKeyPressed(ImGuiKey_Z, false) && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift)) {
		GlobalState.mCommandHistory.redo();
	}

	mData->mMenu->draw();
	for (auto& win : mData->mWindows) {
		win->draw();
	}

	if (!GlobalState.mResPath.empty()) {
		switch (GlobalState.mResType) {
		case File::FileType::MATERIAL: 
		{
			auto res = UTILS::FromJson<RENDER::MaterialResource>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::MaterialResource>>(GlobalState.mResPath, data)); break;
		}
		case File::FileType::TEXTURE_RES:
		{
			auto res = UTILS::FromJson<RENDER::TextureResource>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::TextureResource>>(GlobalState.mResPath, data)); break;
		}
		case File::FileType::SHADER_RES:
		{
			auto res = UTILS::FromJson<RENDER::ShaderResource>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::ShaderResource>>(GlobalState.mResPath, data)); break;
		}
		case File::FileType::AUDIO_RES:
		{
			auto res = UTILS::FromJson<AUDIO::SoundConfig>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<AUDIO::SoundConfig>>(GlobalState.mResPath, data)); break;
		}
		case File::FileType::MODEL_RES:
		{
			auto res = UTILS::FromJson<RENDER::ModelResource>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::ModelResource>>(GlobalState.mResPath, data)); break;
		}
		default:;
		}
		GlobalState.mResPath = "";
	}
	std::erase_if(mData->mResWindows, [](auto& win) { return !win->isOpen(); });
	for (auto& win : mData->mResWindows) {
		win->draw();
	}

	drawPopup();
}

imgui_addons::ImGuiFileBrowser file_dialog;

void EditorRender::drawPopup() {
	//TODO: move to init
	//file_dialog.current_path = Config::ROOT + Config::ASSETS_PATH;


	bool open = true;
	if (GlobalState.mPopupStates.contains("create_new_scene") && GlobalState.mPopupStates["create_new_scene"]) {
		ImGui::OpenPopup("create_new_scene");
		GlobalState.mPopupStates["create_new_scene"] = false;
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

	if (GlobalState.mPopupStates.contains("create_new_material") && GlobalState.mPopupStates["create_new_material"]) {
		ImGui::OpenPopup("create_new_material");
		GlobalState.mPopupStates["create_new_material"] = false;
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


	if (GlobalState.mPopupStates.contains("file_chooser") && GlobalState.mPopupStates["file_chooser"]) {
		ImGui::OpenPopup("Open File");
		GlobalState.mPopupStates["file_chooser"] = false;
	}
	if (GlobalState.mFileFormatsCb && file_dialog.showFileDialog("Open File", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), GlobalState.mFileFormatsCb())) {
		GlobalState.mFileChooserCb(file_dialog.selected_path);
		std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
		std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file
		GlobalState.mFileFormatsCb = nullptr;
		GlobalState.mFileChooserCb = nullptr;
	}

}
#endif
