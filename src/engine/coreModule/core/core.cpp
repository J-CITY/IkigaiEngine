#include "core.h"
#include "utilsModule/time/loadTimer.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/components/physics3dComponent.h"
#endif
#if USE_PHYSICS_2D
#include "coreModule/ecs/components/cloth2dComponent.h"
#include "coreModule/ecs/components/forceField2dComponent.h"
#endif
#include "captureModule/screenshot.h"
#include "coreModule/ecs/components/particleSystemComponent.h"
#include "coroutineModule/scheduler.h"
#include "resourceModule/particleManager.h"
#include "uiModule/viewHost.h"
#include "utilsModule/log/loggerDefine.h"

#include "resourceModule/fileWatcher.h"
#include "resourceModule/serviceManager.h"
#include "windowModule/window/window.h"
#include "windowModule/windowManager.h"
#include <stdexcept>
#include <utilsModule/contentRoot.h>
#include <utilsModule/jsonLoader.h>
#include <utilsModule/log/logger.h>

#include "../config.h"
#include "../ecs/systems/audioSystem.h"
#include "../ecs/systems/inputSystem.h"
#include "../ecs/systems/logicSystem.h"
#include "../ecs/systems/scriptSystem.h"
#include "coreModule/gui/components/vectorComponents.h"
#include "networkModule/networkRuntime.h"
#include "resourceModule/audioSourceManager.h"
#include "resourceModule/contentService.h"
#include "resourceModule/fileSystem/storage.h"
#include "resourceModule/fontManager.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/modelManager.h"
#include "resourceModule/playerProfile.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/shaderManager.h"
#include "resourceModule/textureManager.h"
#include "windowModule/inputManager/inputActions.h"
#include "windowModule/inputManager/inputManager.h"
#include <audioModule/audioManager.h>

#include <renderModule/backends/interface/driverInterface.h>
#include <renderModule/gameRendererInterface.h>
#include <resourceModule/fileSystem/fileSystem.h>
#include <sceneModule/sceneManager.h>
#include <taskModule/taskSystem.h>
#include <utilsModule/pathGetter.h>

#include "coreModule/ecs/components/batchComponent.h"
#include "coreModule/ecs/components/renderTargetComponent.h"
#include "ecsModule/world.h"
#include "editorModule/editorRender.h"
#include "renderModule/backends/driverFactory.h"
#include "renderModule/gameRenderer.h"
#include "renderModule/render.h"
#include "resourceModule/skeletonAnimationManager.h"
#include "resourceModule/skeletonBlendspaceManager.h"
#include "resourceModule/skeletonManager.h"
#include "resourceModule/skeletonStateGraphManager.h"
#include <coreModule/ecs/components/scriptComponent.h>
#include <renderModule/gameRenderer.h>

using namespace IKIGAI;
using namespace IKIGAI::CORE;

Core::Core(
#ifdef DX12_BACKEND
	HINSTANCE hInstance
#endif
#ifdef OCULUS
	android_app* app
#endif
	)
#ifdef DX12_BACKEND
	: hInstance(hInstance)
#endif
#ifdef OCULUS
	: androidApp(app)
#endif
{
	UTILS::LoadTimer coreTiming("core");
	UTILS::InitContentRoot();
	if (!Config::ROOT.empty()) {
		std::cout << "Content root: " << Config::ROOT << "\n";
	}
	std::cout << "Create Core\n";
	mLogger = std::make_unique<UTILS::LOGG::Logger>();
	UTILS::LOGG::Manager::AddOutput(std::make_shared<UTILS::LOGG::OutputConsole>());
	RESOURCES::ServiceManager::Set<UTILS::LOGG::Logger>(mLogger.get());

	fileSystem = std::make_unique<RESOURCES::FileSystem>();
	RESOURCES::ServiceManager::Set<RESOURCES::FileSystem>(fileSystem.get());
#ifdef USE_FILE_WATCHER
	RESOURCES::FileWatcher::getInstance()->start();
#endif
	const auto mountLooseAssets = [&] {
		if (Config::ROOT.empty()) {
			std::cerr << "Content root not found (expected assets/engine/ near cwd or executable). "
				<< "Run: python3 utils/sync_assets.py ensure. "
				<< "For Xcode, set scheme Working Directory to mac/build_xcode (with assets symlink).\n";
		}
		// fileSystem->addNativeFileSystem(".", "/");
#ifdef OCULUS
		fileSystem->addAndroidAssetFileSystem("engine", "/", app->activity->assetManager, app);
		fileSystem->addAndroidAssetFileSystem("game", "/", app->activity->assetManager, app);
#elif defined(__ANDROID__)
		// APK assets root is the repo assets/ folder. SDL_IOFromFile reads engine/ and game/.
		fileSystem->addSdlFileSystem("engine", "/");
		fileSystem->addSdlFileSystem("game", "/");
#elif defined(__APPLE__) && TARGET_OS_IPHONE
		fileSystem->addSdlFileSystem(Config::ROOT + Config::ENGINE_ASSETS_PATH, "/", true);
		fileSystem->addSdlFileSystem(Config::ROOT + Config::USER_ASSETS_PATH, "/", true);
#else
		fileSystem->addNativeFileSystem(Config::ROOT + Config::ENGINE_ASSETS_PATH, "/");
		fileSystem->addNativeFileSystem(Config::ROOT + Config::USER_ASSETS_PATH, "/");
#endif
	};
	auto storageRoot = Config::STORAGE_ROOT.empty()
#if defined(OCULUS) && !defined(USE_SDL)
		? std::filesystem::path(app->activity->internalDataPath)
#else
		? RESOURCES::STORAGE::AppDataPath(Config::ORGANIZATION, Config::APPLICATION)
#endif
		: std::filesystem::u8path(Config::STORAGE_ROOT);
	std::shared_ptr<RESOURCES::STORAGE::UserStorage> userStorage;
#ifdef USE_SDL
	if (Config::STORAGE_ROOT.empty()) {
		userStorage = std::make_shared<RESOURCES::STORAGE::UserStorage>(Config::ORGANIZATION, Config::APPLICATION);
	}
	else
#endif
		userStorage = std::make_shared<RESOURCES::STORAGE::UserStorage>(storageRoot);
	playerProfiles = std::make_unique<RESOURCES::PlayerProfileService>(*fileSystem, std::move(userStorage));
	const auto profiles = playerProfiles->list();
	if (std::find(profiles.begin(), profiles.end(), Config::PROFILE_ID) == profiles.end()) {
		playerProfiles->create(Config::PROFILE_ID);
	}
	playerProfiles->select(Config::PROFILE_ID);
	RESOURCES::ServiceManager::Set<RESOURCES::PlayerProfileService>(playerProfiles.get());
	network = std::make_shared<NETWORK::NetworkRuntime>();
	RESOURCES::ServiceManager::Set<NETWORK::NetworkRuntime>(network.get());
	std::string contentPlatform = "linux";
#if defined(__EMSCRIPTEN__)
	contentPlatform = "web";
#elif defined(__ANDROID__)
	contentPlatform = "android";
#elif defined(_WIN32)
	contentPlatform = "windows";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
	contentPlatform = "ios";
#elif defined(__APPLE__)
	contentPlatform = "macos";
#endif
	content = std::make_unique<RESOURCES::ContentService>(*fileSystem, storageRoot / "content", contentPlatform);
	if (!Config::CONTENT_RELEASE.empty()) {
		content->stageLocal(std::filesystem::u8path(Config::CONTENT_RELEASE));
		content->activate(nlohmann::json::parse(RESOURCES::STORAGE::Read(std::filesystem::u8path(Config::CONTENT_RELEASE))));
	}
	else if (!content->restore()) {
		mountLooseAssets();
	}
	RESOURCES::ServiceManager::Set<RESOURCES::ContentService>(content.get());

	RESOURCES::ModelLoader::SetAssetPaths(Config::USER_ASSETS_PATH, Config::ENGINE_ASSETS_PATH);
	RESOURCES::TextureLoader::SetAssetPaths(Config::USER_ASSETS_PATH, Config::ENGINE_ASSETS_PATH);
	RESOURCES::ShaderLoader::SetAssetPaths(Config::USER_ASSETS_PATH, Config::ENGINE_ASSETS_PATH);
	RESOURCES::MaterialLoader::SetAssetPaths(Config::USER_ASSETS_PATH, Config::ENGINE_ASSETS_PATH);

	auto renderSettings = IKIGAI::UTILS::FromJson<RENDER::RenderSettings>("/Configs/render.json");
	if (renderSettings.isErr()) {
		LOG_ERROR << renderSettings.unwrapErr().text;
		RENDER::DriverInterface::settings = {};
	}
	else {
		RENDER::DriverInterface::settings = renderSettings.unwrap();
	}
	const auto configuredBackend = RENDER::DriverInterface::settings.backend;
	RENDER::ResolveRenderBackend(RENDER::DriverInterface::settings);
	if (configuredBackend != RENDER::DriverInterface::settings.backend) {
		std::cout << "Render backend: " << RENDER::BackendToString(RENDER::DriverInterface::settings.backend)
				  << " (asset config: " << RENDER::BackendToString(configuredBackend) << ")\n";
	}
	else {
		std::cout << "Render backend: " << RENDER::BackendToString(configuredBackend) << "\n";
	}

	WINDOW::WindowSettings windowSettings;
	if (fileSystem->isFileExist("/Configs/window.json")) {
		auto configured = UTILS::FromJson<WINDOW::WindowSettings>("/Configs/window.json");
		if (configured.isErr()) {
			throw std::runtime_error("Window configuration: " + configured.unwrapErr().text);
		}
		windowSettings = configured.unwrap();
	}
	if (Config::WINDOW_WIDTH) {
		windowSettings.size.x = static_cast<unsigned>(*Config::WINDOW_WIDTH);
	}
	if (Config::WINDOW_HEIGHT) {
		windowSettings.size.y = static_cast<unsigned>(*Config::WINDOW_HEIGHT);
	}
	if (Config::WINDOW_FULLSCREEN) {
		windowSettings.isFullscreen = *Config::WINDOW_FULLSCREEN;
	}
	if (Config::WINDOW_TITLE) {
		windowSettings.title = *Config::WINDOW_TITLE;
	}
	if (!windowSettings.size.x || !windowSettings.size.y || windowSettings.size.x > 16384 || windowSettings.size.y > 16384
		|| windowSettings.refreshRate <= 0) {
		throw std::runtime_error("Invalid window dimensions or refresh rate");
	}
	windowSettings.renderBackend = RENDER::DriverInterface::settings.backend;

	windowManager = std::make_unique<WINDOW::WindowManager>();
#ifdef OCULUS
	windowManager->setAndroidApp(app);
#endif
	window = windowManager->createMainWindow(windowSettings);

	std::cout << "Create Window\n";

	modelManager = std::make_unique<RESOURCES::ModelLoader>();
	textureManager = std::make_unique<RESOURCES::TextureLoader>();
	particleManager = std::make_unique<RESOURCES::ParticleLoader>();
	shaderManager = std::make_unique<RESOURCES::ShaderLoader>();
	materialManager = std::make_unique<RESOURCES::MaterialLoader>();
	sceneManager = std::make_unique<SCENE_SYSTEM::SceneManager>(Config::ENGINE_ASSETS_PATH);
	inputManager = std::make_unique<INPUT_SYSTEM::InputManager>(*window);
	inputActions = std::make_unique<INPUT_SYSTEM::InputActions>(*inputManager);
	inputActions->load(INPUT_SYSTEM::DefaultInputResourcePath);
	RESOURCES::ServiceManager::Set<WINDOW::Window>(window.get());

	{
		UTILS::LoadTimer timing("driver");
		driver = RENDER::CreateRenderDriver(RENDER::DriverInterface::settings.backend, *window);
	}
	if (!driver) {
		throw std::runtime_error("Failed to create render driver");
	}

	render = std::make_unique<RENDER::Renderer>(driver.get(), std::make_unique<RENDER::ImmediateExecutor>());

	std::cout << "Create driver\n";

#if USE_LUA
	scriptInterpreter = std::make_unique<SCRIPTING::ScriptInterpreter>(Config::ROOT + Config::USER_ASSETS_PATH + "scripts/");
#endif
	audioManager = std::make_unique<AUDIO::AudioManager>();
	audioSourceLoader = std::make_unique<RESOURCES::AudioSourceLoader>();
	taskManger = std::make_unique<TASK::TaskSystem>();
	coroutineScheduler = std::make_unique<COROUTINE::Scheduler>();
	coroutineScheduler->setErrorHandler([](const COROUTINE::TaskHandle& task) {
		try {
			task.rethrowIfFailed();
		} catch (const std::exception& error) {
			LOG_ERROR << "Coroutine '" << task.name() << "' failed: " << error.what();
		} catch (...) {
			LOG_ERROR << "Coroutine '" << task.name() << "' failed with an unknown exception";
		}
	});
	RESOURCES::ServiceManager::Set<COROUTINE::Scheduler>(coroutineScheduler.get());
	eventBroadcaster = std::make_unique<EVENT::EventBroadcaster>();
	world = std::make_unique<ECS::World>();

#ifndef __EMSCRIPTEN__
	taskManger->setup();
#endif

	RESOURCES::ServiceManager::Set<ECS::World>(world.get());

	auto compMgr = world->getComponentManager();
	auto sysMgr = world->getSystemManager();
	{
		compMgr->registerComponent<ECS::TransformComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::AmbientLight>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::AmbientSphereLight>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::AudioComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::AudioListenerComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::CameraComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::VrCameraComponent>(IKIGAI::ECS::StorageType::Sparse);
		// compMgr->registerComponent<ECS::ArCameraComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::DirectionalLight>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::InputComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::LogicComponent>(IKIGAI::ECS::StorageType::Sparse);
#if USE_IKIGAI_SCRIPT
		compMgr->registerComponent<ECS::IkigaiScriptComponent>(IKIGAI::ECS::StorageType::Sparse);
#endif
		compMgr->registerComponent<ECS::MaterialRenderer>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::ModelRenderer>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::PointLight>(IKIGAI::ECS::StorageType::Sparse);

#if USE_LUA
		compMgr->registerComponent<ECS::ScriptComponent>(IKIGAI::ECS::StorageType::Sparse);
#endif
		compMgr->registerComponent<ECS::SpotLight>(IKIGAI::ECS::StorageType::Sparse);
#if USE_PHYSICS_3D
		compMgr->registerComponent<ECS::PhysicsComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::Physics3DComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::ThirdPersonController>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::CapsuleColliderComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::CharacterMotorComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::ThirdPersonCameraComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::TraversalComponent>(ECS::StorageType::Sparse);
#ifdef IKIGAI_TEST_MODULE
		compMgr->registerComponent<ECS::CharacterDiagnosticsComponent>(ECS::StorageType::Sparse);
#endif
		ECS::CharacterSystem::registerProperties();
		compMgr->registerComponent<ECS::SoftBody3DComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::Cloth3DComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::Fluid3DComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::ForceField3DComponent>(ECS::StorageType::Sparse);
#endif
#if USE_PHYSICS_2D
		compMgr->registerComponent<ECS::Physics2DComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::SoftBody2DComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::Fluid2DComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::Joint2DComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::ForceField2DComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::Cloth2DComponent>(ECS::StorageType::Sparse);
#endif
		compMgr->registerComponent<ECS::ModelLODRenderer>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::BatchComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::RenderTargetComponent>(IKIGAI::ECS::StorageType::Sparse);

		compMgr->registerComponent<ECS::SkeletalComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::SkeletalAnimationComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::VariableContextComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::SkeletalStateGraphComponent>(ECS::StorageType::Sparse);
		ECS::RegisterSceneComponentReflection();
		// GUI
		compMgr->registerComponent<ECS::RootGuiComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::SpriteComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::SpriteAnimateComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::SpriteParticleComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::ParticleSystem2DComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::ParticleSystem3DComponent>(ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::LabelComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::SdfTextComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::VectorShapeComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::VectorPathComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::VectorCanvasComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::InteractionComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::ClipComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::ScrollComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::LayoutComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::SpineComponent>(IKIGAI::ECS::StorageType::Sparse);
		compMgr->registerComponent<ECS::ChunkModelRenderer>(IKIGAI::ECS::StorageType::Sparse);

#if USE_LUA

		sysMgr->registerSystem<ECS::ScriptSystem>();
#endif
		sysMgr->registerSystem<ECS::LogicSystem>();
		sysMgr->registerSystem<ECS::InputSystem>();
		sysMgr->registerSystem<ECS::AudioSystem>();
	}

	RESOURCES::ServiceManager::Set<RESOURCES::ModelLoader>(modelManager.get());
	RESOURCES::ServiceManager::Set<RESOURCES::TextureLoader>(textureManager.get());
	RESOURCES::ServiceManager::Set<RESOURCES::ParticleLoader>(particleManager.get());
	RESOURCES::ServiceManager::Set<RESOURCES::ShaderLoader>(shaderManager.get());
	RESOURCES::ServiceManager::Set<RESOURCES::MaterialLoader>(materialManager.get());
	RESOURCES::ServiceManager::Set<INPUT_SYSTEM::InputManager>(inputManager.get());
	RESOURCES::ServiceManager::Set<INPUT_SYSTEM::InputActions>(inputActions.get());
	RESOURCES::ServiceManager::Set<SCENE_SYSTEM::SceneManager>(sceneManager.get());
	RESOURCES::ServiceManager::Set<AUDIO::AudioManager>(audioManager.get());
	RESOURCES::ServiceManager::Set<RESOURCES::AudioSourceLoader>(audioSourceLoader.get());
#ifndef __EMSCRIPTEN__
	RESOURCES::ServiceManager::Set<TASK::TaskSystem>(taskManger.get());
#endif
	RESOURCES::ServiceManager::Set<EVENT::EventBroadcaster>(eventBroadcaster.get());
	RESOURCES::ServiceManager::Set<RENDER::Renderer>(render.get());
	fontManager = std::make_unique<RESOURCES::FontLoader>();
	RESOURCES::ServiceManager::Set<RESOURCES::FontLoader>(fontManager.get());

	skeletonLoader = std::make_unique<RESOURCES::SkeletonLoader>();
	skeletalStateGraphLoader = std::make_unique<RESOURCES::SkeletonStateGraphLoader>();
	skeletonBlendspaceLoader = std::make_unique<RESOURCES::SkeletonBlendspaceLoader>();
	skeletonAnimationLoader = std::make_unique<RESOURCES::SkeletonAnimationLoader>();

	RESOURCES::ServiceManager::Set<RESOURCES::SkeletonLoader>(skeletonLoader.get());
	RESOURCES::ServiceManager::Set<RESOURCES::SkeletonStateGraphLoader>(skeletalStateGraphLoader.get());
	RESOURCES::ServiceManager::Set<RESOURCES::SkeletonBlendspaceLoader>(skeletonBlendspaceLoader.get());
	RESOURCES::ServiceManager::Set<RESOURCES::SkeletonAnimationLoader>(skeletonAnimationLoader.get());

	{
		UTILS::LoadTimer timing("renderer");
		auto gameRenderer = std::make_unique<RENDER::GameRenderer>(*this);
		RESOURCES::ServiceManager::Set<RENDER::PrimitiveRender>(&gameRenderer->primitives());
		renderer = std::move(gameRenderer);
	}
	if (!renderer) {
		throw;
	}

	std::cout << "Create render\n";
#ifndef OCULUS
	window->initImGUI();
#endif

	RESOURCES::ServiceManager::Set<RENDER::GameRendererInterface>(static_cast<RENDER::GameRendererInterface*>(renderer.get()));
#ifdef USE_EDITOR
	editorRender = std::make_unique<EDITOR::EditorRender>();
#endif
#if USE_JSON_VIEWS && !defined(OCULUS)
	viewHost = std::make_unique<UI::ViewHost>();
#endif

	sceneManager->getCurrentScene().init();
	std::cout << "Init Core";
}

Core::~Core() {
#ifdef USE_FILE_WATCHER
	RESOURCES::FileWatcher::getInstance()->stop();
#endif
	RESOURCES::ServiceManager::Remove<RESOURCES::ContentService>();
	RESOURCES::ServiceManager::Remove<NETWORK::NetworkRuntime>();
	network->stop();
	RESOURCES::ServiceManager::Remove<RESOURCES::PlayerProfileService>();
	// Coroutine locals may reference scenes, ECS, loaders and GPU resources.
	if (sceneManager) {
		sceneManager->shutdownLoading();
	}
	if (coroutineScheduler) {
		coroutineScheduler->shutdown();
	}
	RESOURCES::ServiceManager::Remove<COROUTINE::Scheduler>();
	CAPTURE::ScreenshotService::Get().wait();
	// FG owns presentation workers. Stop them while the window, UI and all
	// tagged scene resources still exist, before ImGui destroys platform windows.
	if (driver) {
		driver->stopPresentation();
	}
#if USE_JSON_VIEWS && !defined(OCULUS)
	viewHost.reset();
#endif
#ifndef OCULUS
	if (window) {
		window->shutdownImGUI();
	}
#endif
	// GPU resources and their loader callbacks must be released while both
	// resource managers and the active backend still exist.
#ifdef USE_EDITOR
	editorRender.reset();
#endif
#if USE_LUA
	// Lua detaches component tables through the ECS. Do this while both the
	// scene's components and the world still exist.
	if (scriptInterpreter) {
		scriptInterpreter->destroyLuaContext();
	}
#endif
	if (sceneManager) {
		sceneManager->unloadCurrentScene();
	}
	world.reset();
	RESOURCES::ServiceManager::Remove<RENDER::PrimitiveRender>();
	renderer.reset();
	// Font subscriptions consult the VFS during removal; release the loader
	// explicitly before fileSystem's member destructor runs.
	fontManager.reset();
	RESOURCES::ServiceManager::Remove<RESOURCES::FontLoader>();
	RENDER::DriverInterface::SetActive(nullptr);
}
