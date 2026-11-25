#include "editorRender.h"
#include "coreModule/ecs/components/environmentComponent.h"
#include "coreModule/ecs/components/waterComponent.h"
#include "coreModule/ecs/systems/environmentSystem.h"
#include "environmentWindow.h"
#include "prefabEditor.h"
#include "resourceModule/jsonLoader.h"
#include "surfaceBrushWindow.h"

#include "resourceEditorWindow.h"
#include "timelineAnimationWindow.h"

#include "utilsModule/log/loggerDefine.h"
#ifdef USE_EDITOR
#include "IconsFontAwesome5.h"
#include "cameraControlWindow.h"
#include "componentManagerWindow.h"
#include "coreModule/config.h"
#include "coreModule/ecs/components/facialAnimationComponent.h"
#include "coreModule/ecs/components/modelRenderer.h"
#include "destructionEditor.h"
#include "editorModule/imguiHelper/ImGuiFileBrowser.h"
#include "fileBrowser.h"
#include "guiAnimationGraphEditor.h"
#include "imgui.h"
#include "menuBar.h"
#include "misc/cpp/imgui_stdlib.h"
#include "pbrMaterialEditor.h"
#include "physicsDebugWindow.h"
#include "profillerWindow.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include "renderingDemos.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"
#include "sceneTransformTool.h"
#include "statWindow.h"
#include "textureWatcher.h"
#include "treeWindow.h"
#include "windowModule/inputManager/inputActions.h"
#include "windowModule/inputManager/inputManager.h"
#include <cstring>
#include <list>

using namespace IKIGAI::EDITOR;

namespace {
void drawWaterWindow(IKIGAI::SCENE_SYSTEM::Scene& scene, bool& open) {
	using namespace IKIGAI;
	const auto* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos({ viewport->WorkPos.x + 10, viewport->WorkPos.y + 10 }, ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Water and Wind", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
		bool found = false;
		for (auto& water : scene.getWorld().getComponentManager()->getComponentsArray<ECS::WaterComponent>()) {
			if (!scene.getObjectIndex(water.obj.get())) {
				continue;
			}
			found = true;
			ImGui::PushItemWidth(138.f);
			ImGui::PushID(&water);
			ImGui::TextUnformatted(water.obj->getName().c_str());
			auto settings = water.getDescriptor();
			auto preset = ENVIRONMENT::Preset::Custom;
			bool changed = false;
			if (ImGui::Button("Calm")) {
				settings.Amplitude = .012f;
				settings.Wavelength = 8;
				settings.WindSpeed = .3f;
				settings.Current = 0;
				settings.Foam = 0;
				settings.Roughness = .06f;
				preset = ENVIRONMENT::Preset::Clear;
			}
			ImGui::SameLine();
			if (ImGui::Button("Breeze")) {
				settings.Amplitude = .16f;
				settings.Wavelength = 7;
				settings.WindSpeed = 4;
				settings.Current = .12f;
				settings.Foam = .35f;
				settings.Roughness = .14f;
				preset = ENVIRONMENT::Preset::WhiteClouds;
			}
			ImGui::SameLine();
			if (ImGui::Button("Storm")) {
				settings.Amplitude = 1.05f;
				settings.Wavelength = 11;
				settings.WindSpeed = 19;
				settings.Current = .35f;
				settings.Foam = .9f;
				settings.Roughness = .25f;
				preset = ENVIRONMENT::Preset::Storm;
			}
			changed |= ImGui::SliderFloat("Amplitude (m)", &settings.Amplitude, 0, 2);
			changed |= ImGui::SliderFloat("Wavelength (m)", &settings.Wavelength, 2, 40);
			bool windChanged = ImGui::SliderFloat("Wind (m/s)", &settings.WindSpeed, 0, 40);
			windChanged |= ImGui::SliderFloat("Heading (deg)", &settings.Heading, -180, 180);
			changed |= ImGui::SliderFloat("Current (m/s)", &settings.Current, -3, 3);
			changed |= ImGui::ColorEdit3("Water color", &settings.Color.x);
			changed |= ImGui::SliderFloat("Roughness", &settings.Roughness, .04f, 1);
			changed |= ImGui::SliderFloat("Whitecaps", &settings.Foam, 0, 1);
			changed |= ImGui::SliderFloat("Hull foam", &settings.HullFoam, 0, 1);
			if (changed || windChanged || preset != ENVIRONMENT::Preset::Custom) {
				water.configure(settings);
			}
			if (windChanged || preset != ENVIRONMENT::Preset::Custom) {
				if (auto* env = ECS::EnvironmentSystem::active(scene)) {
					auto weather = env->settings();
					if (preset != ENVIRONMENT::Preset::Custom) {
						weather = ENVIRONMENT::presetSettings(preset, weather);
					}
					const float angle = settings.Heading * .01745329252f;
					weather.WindDirection = { std::cos(angle), std::sin(angle) };
					weather.WindSpeed = settings.WindSpeed;
					env->apply(weather);
				}
			}
			ImGui::PopItemWidth();
			ImGui::PopID();
		}
		if (!found) {
			ImGui::TextUnformatted("Add a WaterComponent to this scene.");
		}
	}
	ImGui::End();
}
void drawFacialExpressions(IKIGAI::SCENE_SYSTEM::Scene& scene) {
	using namespace IKIGAI;
	const auto object = scene.findObjectByName("Ceramic face");
	if (!object) {
		return;
	}
	const auto facial = object->getComponent<ECS::FacialAnimationComponent>();
	const auto renderer = object->getComponent<ECS::ModelRenderer>();
	if (!facial || !renderer || !renderer->getModel()) {
		return;
	}
	const auto* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos({ viewport->Pos.x + 12, viewport->Pos.y + ImGui::GetFrameHeight() + 8 }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Facial Expressions", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}
	try {
		facial->bind(*renderer->getModel());
		if (ImGui::Button("Neutral")) {
			facial->clearWeights();
		}
		for (const auto& clip : facial->availableClips()) {
			ImGui::SameLine();
			if (ImGui::Button(clip.c_str())) {
				facial->clearWeights();
				facial->play(clip);
			}
		}
		for (const auto& target : facial->availableTargets()) {
			float value = facial->weight(target);
			if (ImGui::SliderFloat(target.c_str(), &value, 0.f, 1.f)) {
				facial->setWeight(target, value);
			}
		}
	} catch (const std::exception& error) {
		ImGui::TextWrapped("%s", error.what());
	}
	ImGui::End();
}
}

struct EditorRender::Internal {
	std::unique_ptr<MenuBar> mMenu;
	ProfillerWindow mProfiler;
	SurfaceBrushWindow mSurfaceBrushes;
	EnvironmentWindow mEnvironment;
	SceneTransformTool mSceneTransform;
	ScreenProbeRoomControls mScreenProbeRoom;
	bool mWaterOpen = false;
	bool mWorkspaceVisible = true;
	TimelineAnimationWindow* mTimelineAnimation = nullptr;

	std::list<std::unique_ptr<EditorWindow>> mWindows;
	std::list<std::unique_ptr<EditorWindow>> mResWindows;

	Internal() {
		mMenu = std::make_unique<MenuBar>();
		mProfiler.setOpen(false);

		mWindows.push_back(std::make_unique<CameraControlWindow>());
		mWindows.push_back(std::make_unique<ComponentManagerWindow>());
		mWindows.push_back(std::make_unique<ComponentInspectorWindow>());
		mWindows.push_back(std::make_unique<TreeWindow>());
		mWindows.push_back(std::make_unique<StatWindow>());
		{
			auto timeline = std::make_unique<TimelineAnimationWindow>();
			mTimelineAnimation = timeline.get();
			mWindows.push_back(std::move(timeline));
		}
		mWindows.push_back(std::make_unique<FileBrowserWindow>("/"));
		mWindows.push_back(std::make_unique<TextureWatcherWindow>());

		// RENDER::ShaderResource res;
		// mWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::ShaderResource>>(res));
	}
};

EditorRender::~EditorRender() {
	if (IKIGAI::RESOURCES::ServiceManager::Check<IKIGAI::INPUT_SYSTEM::InputManager>()) {
		IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::INPUT_SYSTEM::InputManager>().setUiCaptureProvider(nullptr);
	}
}
void EditorRender::openSurfaceBrushes() {
	mData->mSurfaceBrushes.setOpen(true);
}

EditorRender::EditorRender() {
	mData = std::make_unique<Internal>();

	// Init imgui style

	ImGuiIO& io = ImGui::GetIO();
	(void)io;

	const float baseFontSize = 14.0f;
	ImFontConfig config;
	config.SizePixels = baseFontSize;
	io.Fonts->AddFontDefault(&config);

	const float iconFontSize = baseFontSize * 2.0f / 3.0f;

	// merge in icons from Font Awesome
	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
	ImFontConfig icons_config;
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;
	icons_config.GlyphMinAdvanceX = iconFontSize;

	const std::string fontPath = std::string("/fonts/") + FONT_ICON_FILE_NAME_FAS;
	auto& fs = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::FileSystem>();
	auto fontFile = fs.getFile(fontPath, IKIGAI::RESOURCES::FileMode::READ);
	if (!fontFile || !fontFile->isValid()) {
		LOG_ERROR << "Icon font not found: " << fontPath;
	}
	else {
		auto bytes = fontFile->read();
		if (bytes.empty()) {
			LOG_ERROR << "Icon font is empty: " << fontPath;
		}
		else {
			void* fontData = IM_ALLOC(bytes.size());
			std::memcpy(fontData, bytes.data(), bytes.size());
			io.Fonts->AddFontFromMemoryTTF(fontData, static_cast<int>(bytes.size()), iconFontSize, &icons_config, icons_ranges);
		}
	}

	// ImGui_ImplVulkan_CreateFontsTexture();
	// #ifdef VULKAN_BACKEND
	//	auto& render =
	// reinterpret_cast<IKIGAI::RENDER::GameRendererVk&>(IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RENDER::GameRendererInterface>());
	//	render.initForVk();
	// #endif

	// #ifdef DX12_BACKEND
	//	auto& render =
	// reinterpret_cast<IKIGAI::RENDER::GameRendererDx12&>(IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RENDER::GameRendererInterface>()); #endif
	if (IKIGAI::RESOURCES::ServiceManager::Check<IKIGAI::INPUT_SYSTEM::InputManager>()) {
		IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::INPUT_SYSTEM::InputManager>().setUiCaptureProvider([] {
			if (ImGui::GetCurrentContext()) {
				const auto& io = ImGui::GetIO();
				return IKIGAI::INPUT_SYSTEM::InputManager::UiCapture { io.WantCaptureKeyboard, io.WantCaptureMouse || GlobalState.mSceneEditing };
			}
			return IKIGAI::INPUT_SYSTEM::InputManager::UiCapture {};
		});
	}
}

void EditorRender::draw() {
	static std::weak_ptr<void> historyScene;
	auto& manager = PREFABS::Manager();
	const auto sourcePath = manager.getCurrentSceneSourcePath();
	const std::string_view path = sourcePath.starts_with('/') ? std::string_view(sourcePath).substr(1) : std::string_view(sourcePath);
	const bool destructionPlayground = path == "scenes/destruction-playground.json";
	const bool layeredPlayground = path == "scenes/layered-materials.json";
	const bool facialPlayground = path == "scenes/facial-expressions.json";
	const bool screenProbeRoom = path == std::string_view(ScreenProbeRoomPath).substr(1);
	const bool virtualGeometryGallery = path == std::string_view(VirtualGeometryGalleryPath).substr(1);
	const bool furRabbit = path == std::string_view(FurRabbitPath).substr(1);
	const bool cloth2D = path == std::string_view(Cloth2DPath).substr(1);
	const bool cloth3D = path == std::string_view(Cloth3DPath).substr(1);
	const bool softBody = path == std::string_view(SoftBodyPath).substr(1);
	const bool particleEvents = path == std::string_view(ParticleEventPath).substr(1);
	const bool weatherDemo = path == "scenes/environment-demo.json";
	const bool rocketLander = path == "scenes/rocket-lander.json";
	const bool boatDemo = path == "scenes/drifting-boat.json";
	const bool focusedDemo = screenProbeRoom || virtualGeometryGallery || furRabbit || cloth2D || cloth3D || softBody || particleEvents || weatherDemo
		|| boatDemo || rocketLander;
	if (manager.hasCurrentScene() && historyScene.lock() != manager.getCurrentScene().lifetime) {
		GlobalState.mCommandHistory.clear();
		GlobalState.mSelectObject.reset();
		historyScene = manager.getCurrentScene().lifetime;
		mData->mScreenProbeRoom.reset();
		mData->mWaterOpen = boatDemo;
		mData->mWorkspaceVisible
			= path != "scenes/gui-widgets.json" && !destructionPlayground && !layeredPlayground && !facialPlayground && !focusedDemo;
		if (focusedDemo) {
			ImGui::SetWindowCollapsed("Camera Tools###json-view/camera_tools", true);
		}
		if (!mData->mWorkspaceVisible) {
			mData->mSceneTransform.enabled = false;
		}
	}
	if (GlobalState.mSelectObject && !GlobalState.mSelectObject->isAlive()) {
		GlobalState.mSelectObject.reset();
	}
	PREFABS::CompletePending();
	if (!mData->mSceneTransform.active() && !mData->mSurfaceBrushes.isPainting() && ImGui::IsKeyPressed(ImGuiKey_Z, false) && ImGui::GetIO().KeyCtrl
		&& !ImGui::GetIO().KeyShift) {
		PREFABS::Try([] { GlobalState.mCommandHistory.undo(); });
	}
	if (!mData->mSceneTransform.active() && !mData->mSurfaceBrushes.isPainting()
		&& ((ImGui::IsKeyPressed(ImGuiKey_Y, false) && ImGui::GetIO().KeyCtrl)
			|| (ImGui::IsKeyPressed(ImGuiKey_Z, false) && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift))) {
		PREFABS::Try([] { GlobalState.mCommandHistory.redo(); });
	}

	mData->mMenu->draw();
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("Window")) {
			ImGui::MenuItem("Scene Editing", nullptr, &mData->mSceneTransform.enabled);
			ImGui::MenuItem("Object Markers", nullptr, &mData->mSceneTransform.showMarkers);
			ImGui::MenuItem("Editor Panels", nullptr, &mData->mWorkspaceVisible);
			bool profilerOpen = mData->mProfiler.isOpen();
			if (ImGui::MenuItem("Profiler", nullptr, &profilerOpen)) {
				mData->mProfiler.setOpen(profilerOpen);
			}
			bool surfaceOpen = mData->mSurfaceBrushes.isOpen();
			if (ImGui::MenuItem("Surface Brushes", nullptr, &surfaceOpen)) {
				mData->mSurfaceBrushes.setOpen(surfaceOpen);
			}
			ImGui::MenuItem("Water and Wind", nullptr, &mData->mWaterOpen);
			bool environmentOpen = mData->mEnvironment.isOpen();
			if (ImGui::MenuItem("Environment", nullptr, &environmentOpen)) {
				mData->mEnvironment.setOpen(environmentOpen);
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		const auto fps = ImGui::GetIO().Framerate;
		ImGui::Text("%.1f FPS | %.1f ms", fps, fps > 0 ? 1000.0f / fps : 0.0f);
		ImGui::EndMainMenuBar();
	}
	if ((mData->mWorkspaceVisible || cloth2D) && manager.hasCurrentScene()) {
		DrawPhysicsDebug(manager.getCurrentScene(), cloth2D);
	}
	if (mData->mWaterOpen && manager.hasCurrentScene()) {
		drawWaterWindow(manager.getCurrentScene(), mData->mWaterOpen);
	}
	if (mData->mWorkspaceVisible || weatherDemo) {
		mData->mEnvironment.draw();
	}
	if (mData->mWorkspaceVisible) {
		mData->mProfiler.draw();
		mData->mSurfaceBrushes.draw();
		for (auto& win : mData->mWindows) {
			win->draw();
		}
		drawGuiAnimationGraphEditor();
	}
	if (manager.hasCurrentScene()) {
		if (!rocketLander || mData->mWorkspaceVisible || mData->mSceneTransform.enabled) {
			mData->mSceneTransform.draw(manager.getCurrentScene(), destructionPlayground);
		}
#if USE_PHYSICS_3D
		if (destructionPlayground) {
			DrawDestructionPlayground(manager.getCurrentScene(), !mData->mSceneTransform.capturesMouse());
		}
#endif
		if (layeredPlayground) {
			DrawLayeredMaterialPlayground(manager.getCurrentScene());
		}
		if (facialPlayground) {
			drawFacialExpressions(manager.getCurrentScene());
		}
		if (screenProbeRoom) {
			mData->mScreenProbeRoom.draw(manager.getCurrentScene());
		}
		if (virtualGeometryGallery) {
			DrawVirtualGeometryGallery(manager.getCurrentScene());
		}
		if (furRabbit) {
			DrawFurRabbit(manager.getCurrentScene());
		}
		if (cloth2D || cloth3D) {
			DrawClothWind(manager.getCurrentScene(), cloth2D);
		}
		if (softBody) {
			DrawSoftBodyDemo();
		}
		if (particleEvents) {
			DrawParticleEvents(manager.getCurrentScene());
		}
	}
	GlobalState.mSceneEditing = mData->mSceneTransform.capturesMouse();

	if (!GlobalState.mResPath.empty()) {
		switch (GlobalState.mResType) {
		case File::FileType::MATERIAL: {
			auto res = UTILS::FromJson<RENDER::MaterialResource>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::MaterialResource>>(GlobalState.mResPath, data));
			break;
		}
		case File::FileType::TEXTURE_RES: {
			auto res = UTILS::FromJson<RENDER::TextureResource>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::TextureResource>>(GlobalState.mResPath, data));
			break;
		}
		case File::FileType::SHADER_RES: {
			auto res = UTILS::FromJson<RENDER::ShaderResource>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::ShaderResource>>(GlobalState.mResPath, data));
			break;
		}
		case File::FileType::AUDIO_RES: {
			auto res = UTILS::FromJson<AUDIO::SoundConfig>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<AUDIO::SoundConfig>>(GlobalState.mResPath, data));
			break;
		}
		case File::FileType::MODEL_RES: {
			auto res = UTILS::FromJson<RENDER::ModelResource>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::ModelResource>>(GlobalState.mResPath, data));
			break;
		}
		case File::FileType::INPUT_RES: {
			auto res = UTILS::FromJson<INPUT_SYSTEM::InputConfig>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<INPUT_SYSTEM::InputConfig>>(GlobalState.mResPath, data));
			break;
		}
		case File::FileType::TRACK_ANIM: {
			if (mData->mTimelineAnimation) {
				mData->mTimelineAnimation->openResource(GlobalState.mResPath);
				mData->mTimelineAnimation->setOpen(true);
			}
			break;
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
	// TODO: move to init
	// file_dialog.current_path = Config::ROOT + Config::ASSETS_PATH;

	bool open = true;
	if (GlobalState.mPopupStates.contains("create_new_scene") && GlobalState.mPopupStates["create_new_scene"]) {
		ImGui::OpenPopup("create_new_scene");
		GlobalState.mPopupStates["create_new_scene"] = false;
	}
	if (ImGui::BeginPopupModal("create_new_scene", &open)) {
		ImGui::TextWrapped("Write name for scene");
		static std::string name;
		if (ImGui::InputText("Scene name", &name)) { }

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
	if (GlobalState.mFileFormatsCb
		&& file_dialog.showFileDialog(
			"Open File", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), GlobalState.mFileFormatsCb())) {
		GlobalState.mFileChooserCb(file_dialog.selected_path);
		std::cout << file_dialog.selected_fn << std::endl; // The name of the selected file or directory in case of Select Directory dialog mode
		std::cout << file_dialog.selected_path << std::endl; // The absolute path to the selected file
		GlobalState.mFileFormatsCb = nullptr;
		GlobalState.mFileChooserCb = nullptr;
	}
}
#endif
