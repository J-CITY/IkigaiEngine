#pragma once
#ifdef USE_SDL
#include <SDL3/SDL.h>
#endif
#include <memory>

namespace IKIGAI::UI {
class ViewHost;
}
namespace IKIGAI::NETWORK {
class NetworkRuntime;
}
namespace IKIGAI::COROUTINE {
class Scheduler;
}
namespace IKIGAI::ECS {
class World;
}

#ifdef USE_EDITOR
namespace IKIGAI {
namespace EDITOR {
	class EditorRender;
}
}
#endif

namespace IKIGAI {
namespace EVENT {
	class EventBroadcaster;
}
}

namespace IKIGAI {
namespace TASK {
	class TaskSystem;
}
}

namespace IKIGAI {
namespace PHYSICS {
	class PhysicWorld;
}
}

namespace IKIGAI {
namespace RESOURCES {
	class SkeletonAnimationLoader;
	class SkeletonBlendspaceLoader;
	class SkeletonStateGraphLoader;
	class BehaviorTreeLoader;
	class BlackboardLoader;
#if USE_PHYSICS_3D
	class RagdollLoader;
	class DestructibleLoader;
#endif
	class SkeletonLoader;
	class ModelLoader;
	class TextureLoader;
	class ParticleLoader;
	class MaterialLoader;
	class ShaderLoader;
	class AudioSourceLoader;
	class FontLoader;
	class FileSystem;
	class PlayerProfileService;
	class ContentService;
}
}

namespace IKIGAI {
namespace AUDIO {
	class AudioManager;
}
}

namespace IKIGAI {
namespace SCENE_SYSTEM {
	class SceneManager;
}
}

namespace IKIGAI {
namespace SCRIPTING {
	class ScriptInterpreter;
}
}

namespace IKIGAI {
namespace RENDER {
	class Renderer;
	class GameRendererInterface;
	class DriverInterface;
}
}

namespace IKIGAI {
namespace INPUT_SYSTEM {
	class InputManager;
	class InputActions;
}
}

namespace IKIGAI {
namespace UTILS {
	namespace LOGG {
		class Logger;
	}
}
}

namespace IKIGAI {
namespace WINDOW {
	class Window;
	class WindowManager;
}
}

#include "platformStartup.h"
namespace IKIGAI {
namespace CORE {
	class Core final {
	public:
		void* nativeInstance = nullptr;
		android_app* androidApp = nullptr;
		explicit Core(const PlatformStartup& platform);
		~Core();

		std::unique_ptr<WINDOW::WindowManager> windowManager;
		std::shared_ptr<WINDOW::Window> window;
		std::unique_ptr<UTILS::LOGG::Logger> mLogger;
		std::unique_ptr<INPUT_SYSTEM::InputManager> inputManager;
		std::unique_ptr<INPUT_SYSTEM::InputActions> inputActions;
		std::unique_ptr<RENDER::DriverInterface> driver;
#if USE_LUA
		std::unique_ptr<SCRIPTING::ScriptInterpreter> scriptInterpreter;
#endif
		std::unique_ptr<RENDER::Renderer> render;
		std::unique_ptr<RENDER::GameRendererInterface> renderer;
		std::unique_ptr<SCENE_SYSTEM::SceneManager> sceneManager;
		std::unique_ptr<AUDIO::AudioManager> audioManager;
		std::unique_ptr<RESOURCES::AudioSourceLoader> audioSourceLoader;
		std::unique_ptr<TASK::TaskSystem> taskManger;
		std::unique_ptr<COROUTINE::Scheduler> coroutineScheduler;
		std::unique_ptr<EVENT::EventBroadcaster> eventBroadcaster;
		std::unique_ptr<ECS::World> world;
#if USE_JSON_VIEWS && !defined(OCULUS)
		std::unique_ptr<UI::ViewHost> viewHost;
#endif

#ifdef USE_EDITOR
		std::unique_ptr<IKIGAI::EDITOR::EditorRender> editorRender;
#endif
		std::unique_ptr<RESOURCES::ModelLoader> modelManager;
		std::unique_ptr<RESOURCES::TextureLoader> textureManager;
		std::unique_ptr<RESOURCES::ParticleLoader> particleManager;
		std::unique_ptr<RESOURCES::ShaderLoader> shaderManager;
		std::unique_ptr<RESOURCES::MaterialLoader> materialManager;
		std::unique_ptr<RESOURCES::FontLoader> fontManager;
		std::unique_ptr<RESOURCES::FileSystem> fileSystem;
		std::unique_ptr<RESOURCES::PlayerProfileService> playerProfiles;
		std::unique_ptr<RESOURCES::ContentService> content;
		std::shared_ptr<NETWORK::NetworkRuntime> network;

		std::unique_ptr<RESOURCES::SkeletonLoader> skeletonLoader;
		std::unique_ptr<RESOURCES::SkeletonStateGraphLoader> skeletalStateGraphLoader;
		std::unique_ptr<RESOURCES::BehaviorTreeLoader> behaviorTreeLoader;
		std::unique_ptr<RESOURCES::BlackboardLoader> blackboardLoader;
#if USE_PHYSICS_3D
		std::unique_ptr<RESOURCES::RagdollLoader> ragdollLoader {};
		std::unique_ptr<RESOURCES::DestructibleLoader> destructibleLoader {};
#endif
		std::unique_ptr<RESOURCES::SkeletonBlendspaceLoader> skeletonBlendspaceLoader;
		std::unique_ptr<RESOURCES::SkeletonAnimationLoader> skeletonAnimationLoader;
	};
}
}
