#pragma once
#ifdef USE_SDL
#include <SDL3/SDL.h>
#endif
#include <memory>
namespace IKIGAI::ECS2
{
	class World;
}

#ifdef USE_EDITOR
namespace IKIGAI
{
	namespace EDITOR
	{
		class EditorRender;
	}
}
#endif
namespace IKIGAI {
	namespace EVENT {
		class EventBroadcaster;
	}
}
namespace IKIGAI
{
	namespace TASK
	{
		class TaskSystem;
	}
}

namespace IKIGAI
{
	namespace PHYSICS
	{
		class PhysicWorld;
	}
}

namespace IKIGAI
{
	namespace RESOURCES
	{
		class SkeletonAnimationLoader;
		class SkeletonBlendspaceLoader;
		class SkeletonStateGraphLoader;
		class SkeletonLoader;
		class ModelLoader;
		class TextureLoader;
		class MaterialLoader;
		class ShaderLoader;
		class AudioSourceLoader;
		class FileSystem;
	}
}

namespace IKIGAI
{
	namespace AUDIO
	{
		class AudioManager;
	}
}

namespace IKIGAI
{
	namespace SCENE_SYSTEM
	{
		class SceneManager;
	}
}

namespace IKIGAI
{
	namespace SCRIPTING
	{
		class ScriptInterpreter;
	}
}

namespace IKIGAI
{
	namespace RENDER
	{
		class Renderer;
		class GameRendererInterface;
		class DriverInterface;
	}
}

namespace IKIGAI
{
	namespace INPUT_SYSTEM
	{
		class InputManager;
		class InputActions;
	}
}

namespace IKIGAI
{
	namespace UTILS
	{
		namespace LOGG
		{
			class Logger;
		}
	}
}

namespace IKIGAI
{
	namespace WINDOW
	{
		class Window;
		class WindowManager;
	}
}

#ifdef DX12_BACKEND
#include <Windows.h>
#endif
#ifdef OCULUS
#include <android_native_app_glue.h>
#endif
namespace IKIGAI {
	namespace CORE {
		class Core final {
		public:
#ifdef DX12_BACKEND
			HINSTANCE hInstance;
#endif
			Core(
#ifdef DX12_BACKEND
				HINSTANCE hInstance
#endif
#ifdef OCULUS
                android_app* app
#endif
			);
			~Core();

#ifdef OCULUS
			android_app* androidApp = nullptr;
#endif
		
			std::unique_ptr<WINDOW::WindowManager> windowManager;
			std::shared_ptr<WINDOW::Window> window;
			std::unique_ptr<UTILS::LOGG::Logger> mLogger;
			std::unique_ptr<INPUT_SYSTEM::InputManager>   inputManager;
			std::unique_ptr<INPUT_SYSTEM::InputActions>   inputActions;
			std::unique_ptr<RENDER::DriverInterface>         driver;
			std::unique_ptr<SCRIPTING::ScriptInterpreter> scriptInterpreter;
			std::unique_ptr<RENDER::Renderer> render;
			std::unique_ptr<RENDER::GameRendererInterface> renderer;
			std::unique_ptr<SCENE_SYSTEM::SceneManager>   sceneManager;
			std::unique_ptr<AUDIO::AudioManager>          audioManager;
			std::unique_ptr<RESOURCES::AudioSourceLoader>     audioSourceLoader;
			std::unique_ptr<PHYSICS::PhysicWorld>         physicsManger;
			std::unique_ptr<TASK::TaskSystem>             taskManger;
			std::unique_ptr<EVENT::EventBroadcaster>      eventBroadcaster;
			std::unique_ptr<ECS2::World>      world;

#ifdef USE_EDITOR
			std::unique_ptr<IKIGAI::EDITOR::EditorRender> editorRender;
#endif
			std::unique_ptr<RESOURCES::ModelLoader>    modelManager;
			std::unique_ptr<RESOURCES::TextureLoader>  textureManager;
			std::unique_ptr<RESOURCES::ShaderLoader>   shaderManager;
			std::unique_ptr<RESOURCES::MaterialLoader> materialManager;
			std::unique_ptr<RESOURCES::FileSystem>     fileSystem;

			std::unique_ptr<RESOURCES::SkeletonLoader> skeletonLoader;
			std::unique_ptr<RESOURCES::SkeletonStateGraphLoader> skeletalStateGraphLoader;
			std::unique_ptr<RESOURCES::SkeletonBlendspaceLoader> skeletonBlendspaceLoader;
			std::unique_ptr<RESOURCES::SkeletonAnimationLoader> skeletonAnimationLoader;

		};
	}
}
