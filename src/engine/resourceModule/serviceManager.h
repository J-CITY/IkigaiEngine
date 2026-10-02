#pragma once
#include <any>
#include <map>
#include <variant>

namespace IKIGAI::RENDER {
	class GameRendererInterface;
	class DriverInterface;
}

namespace IKIGAI::TASK {
	class TaskSystem;
}

namespace IKIGAI::AUDIO {
	class AudioManagerAL;
	class AudioManager;
}

namespace IKIGAI::SCENE_SYSTEM {
	class SceneManager;
}

namespace IKIGAI::WINDOW {
	class Window;
}

namespace IKIGAI::INPUT_SYSTEM {
	class InputManager;
	class InputActions;
}

namespace IKIGAI {
	namespace RESOURCES	{
		class AudioSourceLoader;
		class MaterialLoader;
		class ShaderLoader;
		class TextureLoader;
		class ModelLoader;
		class SkeletonLoader;
		class SkeletonAnimationLoader;
		class SkeletonBlendspaceLoader;
		
		using ServiceType = std::any;

		class ServiceManager {
		public:
			template<class T>
			static void Set(T* p_service) {
				SERVICES[typeid(T).hash_code()] = p_service;
			}

			template<class T>
			static T& Get() {
				return *std::any_cast<T*>(SERVICES.at(typeid(T).hash_code()));
			}

			template<class T>
			static bool Check() {
				return SERVICES.contains(typeid(T).hash_code());
			}

			template<class T>
			static void Remove() {
				SERVICES.erase(typeid(T).hash_code());
			}
		private:
			static std::map<size_t, ServiceType> SERVICES;
		};
	}
}
