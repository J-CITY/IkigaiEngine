#include "core.h"

#include "resourceModule/serviceManager.h"
#include "windowModule/window/window.h"
#include "windowModule/windowManager.h"
#include <stdexcept>
#include <utilsModule/log/logger.h>
#include <utilsModule/jsonLoader.h>

#include "../config.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/modelManager.h"
#include "resourceModule/ServiceManager.h"
#include "resourceModule/shaderManager.h"
#include "resourceModule/audioSourceManager.h"
#include "resourceModule/textureManager.h"
#include <audioModule/audioManager.h>
#include <debugModule/debugRender.h>
#include <physicsModule/PhysicWorld.h>
#include <resourceModule/fileSystem/fileSystem.h>
#include <taskModule/taskSystem.h>
#include <utilsModule/pathGetter.h>
#include "windowModule/inputManager/inputManager.h"
#include "windowModule/inputManager/inputActions.h"
#include <sceneModule/sceneManager.h>
#include "../ecs/systems/audioSystem.h"
#include "../ecs/systems/scriptSystem.h"
#include "../ecs/systems/logicSystem.h"
#include "../ecs/systems/inputSystem.h"
#include <renderModule/gameRendererInterface.h>
#include <renderModule/backends/interface/driverInterface.h>

#include <coreModule/ecs/components/scriptComponent.h>
#include "coreModule/ecs/components/batchComponent.h"
#include "coreModule/ecs/components/renderTargetComponent.h"
#include "ecsModule/world.h"
#include "editorModule/editorRender.h"
#include "renderModule/gameRenderer.h"
#include "renderModule/render.h"
#include "resourceModule/skeletonAnimationManager.h"
#include "resourceModule/skeletonBlendspaceManager.h"
#include "resourceModule/skeletonManager.h"
#include "resourceModule/skeletonStateGraphManager.h"
#include <renderModule/gameRenderer.h>
#include "renderModule/backends/driverFactory.h"


//namespace IKIGAI
//{
//	namespace ECS
//	{
//		class InputComponent;
//		class LogicComponent;
//		class AmbientSphereLight;
//		class AmbientLight;
//	}
//}

using namespace IKIGAI;
using namespace IKIGAI::CORE;


Core:: Core(
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
{
	//TODO: init Config::ROOT | UTILS::ReplaceSubstrings(std::filesystem::current_path().string(), "\\", "/") + "/";
	std::cout << "Create Core\n";
	mLogger = std::make_unique<UTILS::LOGG::Logger>();
	UTILS::LOGG::Manager::AddOutput(std::make_shared<UTILS::LOGG::OutputConsole>());
	RESOURCES::ServiceManager::Set<UTILS::LOGG::Logger>(mLogger.get());

	fileSystem = std::make_unique<RESOURCES::FileSystem>();
	RESOURCES::ServiceManager::Set<RESOURCES::FileSystem>(fileSystem.get());
	//fileSystem->addNativeFileSystem(".", "/");
	fileSystem->addNativeFileSystem(Config::ENGINE_ASSETS_PATH, "/");
	fileSystem->addNativeFileSystem(Config::USER_ASSETS_PATH, "/");


	RESOURCES::ModelLoader::SetAssetPaths(Config::USER_ASSETS_PATH, Config::ENGINE_ASSETS_PATH);
	RESOURCES::TextureLoader::SetAssetPaths(Config::USER_ASSETS_PATH, Config::ENGINE_ASSETS_PATH);
	RESOURCES::ShaderLoader::SetAssetPaths(Config::USER_ASSETS_PATH, Config::ENGINE_ASSETS_PATH);
	RESOURCES::MaterialLoader::SetAssetPaths(Config::USER_ASSETS_PATH, Config::ENGINE_ASSETS_PATH);

	auto renderSettings = IKIGAI::UTILS::FromJson<RENDER::RenderSettings>("/Configs/render.json");
	if (renderSettings.isErr()) {
		LOG_ERROR << renderSettings.unwrapErr().text;
		RENDER::DriverInterface::settings = {};
	} else {
		RENDER::DriverInterface::settings = renderSettings.unwrap();
	}
	if (!RENDER::ApplyCliRenderBackendOverride(RENDER::DriverInterface::settings)) {
		throw std::runtime_error("Invalid --render-backend value");
	}
	RENDER::ValidateBackendAvailable(RENDER::DriverInterface::settings.backend);

	WINDOW::WindowSettings windowSettings;
	windowSettings.renderBackend = RENDER::DriverInterface::settings.backend;

	windowManager = std::make_unique<WINDOW::WindowManager>();
	window = windowManager->createMainWindow(windowSettings);

	std::cout << "Create Window\n";
	
	modelManager = std::make_unique<RESOURCES::ModelLoader>();
	textureManager = std::make_unique<RESOURCES::TextureLoader>();
	shaderManager = std::make_unique<RESOURCES::ShaderLoader>();
	materialManager = std::make_unique<RESOURCES::MaterialLoader>();
	sceneManager = std::make_unique<SCENE_SYSTEM::SceneManager>(Config::ENGINE_ASSETS_PATH);
	inputManager = std::make_unique<INPUT_SYSTEM::InputManager>(*window);
	inputActions = std::make_unique<INPUT_SYSTEM::InputActions>(*inputManager);
	inputActions->load(INPUT_SYSTEM::DefaultInputResourcePath);
	RESOURCES::ServiceManager::Set<WINDOW::Window>(window.get());

	driver = RENDER::CreateRenderDriver(RENDER::DriverInterface::settings.backend, *window);
	if (!driver) {
		throw std::runtime_error("Failed to create render driver");
	}

	render = std::make_unique<RENDER::Renderer>(driver.get(), std::make_unique<RENDER::ImmediateExecutor>());

	std::cout << "Create driver\n";
	scriptInterpreter = std::make_unique<SCRIPTING::ScriptInterpreter>(Config::ROOT + Config::USER_ASSETS_PATH + "scripts/");
	audioManager = std::make_unique<AUDIO::AudioManager>();
	audioSourceLoader = std::make_unique<RESOURCES::AudioSourceLoader>();
	physicsManger = std::make_unique<PHYSICS::PhysicWorld>(256);
	taskManger = std::make_unique<TASK::TaskSystem>();
	eventBroadcaster = std::make_unique<EVENT::EventBroadcaster>();
	world = std::make_unique<ECS2::World>();

#ifndef __EMSCRIPTEN__
	taskManger->setup();
#endif

	RESOURCES::ServiceManager::Set<ECS2::World>(world.get());

	auto compMgr = world->getComponentManager();
	auto sysMgr = world->getSystemManager();
	{
		compMgr->registerComponent<ECS::TransformComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::AmbientLight>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::AmbientSphereLight>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::AudioComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::AudioListenerComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::CameraComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::VrCameraComponent>(IKIGAI::ECS2::StorageType::Sparse);
		//compMgr->registerComponent<ECS::ArCameraComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::DirectionalLight>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::InputComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::LogicComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::MaterialRenderer>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::ModelRenderer>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::PointLight>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::ScriptComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::Skeletal>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::SpotLight>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::PhysicsComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::ModelLODRenderer>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::BatchComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::RenderTargetComponent>(IKIGAI::ECS2::StorageType::Sparse);

		compMgr->registerComponent<ECS::SkeletalComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::SkeletalAnimationComponent>(IKIGAI::ECS2::StorageType::Sparse);
		//GUI
#ifdef OPENGL_BACKEND
		compMgr->registerComponent<ECS::RootGuiComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::SpriteComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::SpriteAnimateComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::SpriteParticleComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::LabelComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::InteractionComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::ClipComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::ScrollComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::LayoutComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::SpineComponent>(IKIGAI::ECS2::StorageType::Sparse);
		compMgr->registerComponent<ECS::ChunkModelRenderer>(IKIGAI::ECS2::StorageType::Sparse);
#endif

		sysMgr->registerSystem<ECS::ScriptSystem>();
		sysMgr->registerSystem<ECS::LogicSystem>();
		sysMgr->registerSystem<ECS::InputSystem>();
		sysMgr->registerSystem<ECS::AudioSystem>();
	}


	//ECS::ComponentManager::GetInstance().registerComponent<ECS::TransformComponent>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::AmbientLight>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::AmbientSphereLight>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::AudioComponent>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::AudioListenerComponent>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::CameraComponent>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::VrCameraComponent>();
	////ECS::ComponentManager::getInstance()->registerComponent<ECS::ArCameraComponent>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::DirectionalLight>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::InputComponent>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::LogicComponent>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::MaterialRenderer>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::ModelRenderer>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::PointLight>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::ScriptComponent>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::Skeletal>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::SpotLight>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::PhysicsComponent>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::ModelLODRenderer>();
	//ECS::ComponentManager::GetInstance().registerComponent<ECS::BatchComponent>();
	//GUI
//#ifdef OPENGL_BACKEND
//	ECS::ComponentManager::GetInstance().registerComponent<ECS::RootGuiComponent>();
//	ECS::ComponentManager::GetInstance().registerComponent<ECS::SpriteComponent>();
//	ECS::ComponentManager::GetInstance().registerComponent<ECS::SpriteAnimateComponent>();
//	ECS::ComponentManager::GetInstance().registerComponent<ECS::SpriteParticleComponent>();
//	ECS::ComponentManager::GetInstance().registerComponent<ECS::LabelComponent>();
//	ECS::ComponentManager::GetInstance().registerComponent<ECS::InteractionComponent>();
//	ECS::ComponentManager::GetInstance().registerComponent<ECS::ClipComponent>();
//	ECS::ComponentManager::GetInstance().registerComponent<ECS::ScrollComponent>();
//	ECS::ComponentManager::GetInstance().registerComponent<ECS::LayoutComponent>();
//	ECS::ComponentManager::GetInstance().registerComponent<ECS::SpineComponent>();
//	ECS::ComponentManager::GetInstance().registerComponent<ECS::ChunkModelRenderer>();
//#endif
	//ECS::ComponentManager::GetInstance().getSystemManager().registerSystem<ECS::ScriptSystem>();
	//ECS::ComponentManager::GetInstance().getSystemManager().registerSystem<ECS::LogicSystem>();
	//ECS::ComponentManager::GetInstance().getSystemManager().registerSystem<ECS::InputSystem>();
	//ECS::ComponentManager::GetInstance().getSystemManager().registerSystem<ECS::AudioSystem>();
	//ECS::Signature signature;
	//signature.set(static_cast<unsigned int>(ECS::ComponentManager::GetInstance().getComponentType<ECS::AudioComponent>()));
	//ECS::ComponentManager::GetInstance().setSystemSignature<ECS::AudioSystem>(signature);

	RESOURCES::ServiceManager::Set<RESOURCES::ModelLoader>(modelManager.get());
	RESOURCES::ServiceManager::Set<RESOURCES::TextureLoader>(textureManager.get());
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

	skeletonLoader = std::make_unique<RESOURCES::SkeletonLoader>();
	skeletalStateGraphLoader = std::make_unique<RESOURCES::SkeletonStateGraphLoader>();
	skeletonBlendspaceLoader = std::make_unique<RESOURCES::SkeletonBlendspaceLoader>();
	skeletonAnimationLoader = std::make_unique<RESOURCES::SkeletonAnimationLoader>();

	RESOURCES::ServiceManager::Set<RESOURCES::SkeletonLoader>(skeletonLoader.get());
	RESOURCES::ServiceManager::Set<RESOURCES::SkeletonStateGraphLoader>(skeletalStateGraphLoader.get());
	RESOURCES::ServiceManager::Set<RESOURCES::SkeletonBlendspaceLoader>(skeletonBlendspaceLoader.get());
	RESOURCES::ServiceManager::Set<RESOURCES::SkeletonAnimationLoader>(skeletonAnimationLoader.get());

	renderer = std::make_unique<RENDER::GameRenderer>(*this);
	if (!renderer) {
		throw;
	}

	std::cout << "Create render\n";
	window->initImGUI();
	
	//renderer->setCapability(RENDER::RenderingCapability::MULTISAMPLE, true);
	RESOURCES::ServiceManager::Set<RENDER::GameRendererInterface>(static_cast<RENDER::GameRendererInterface*>(renderer.get()));
	//driver->init();

//#ifdef VULKAN_BACKEND
//	reinterpret_cast<RENDER::GameRenderer*>(renderer.get())->createVkResources();
//#endif
	//debugRender = std::make_unique<DEBUG::DebugRender>();

#ifdef USE_EDITOR
	editorRender = std::make_unique<EDITOR::EditorRender>();
#endif

	sceneManager->getCurrentScene().init();
	std::cout << "Init Core";
}

Core::~Core() {
	if (window) {
		window->shutdownImGUI();
	}
	RENDER::DriverInterface::SetActive(nullptr);
}
