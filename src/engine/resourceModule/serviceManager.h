#pragma once
#include <any>
#include <map>
#include <mutex>
#include <typeindex>
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
namespace RESOURCES {
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
		template <class T> static void Set(T* p_service) {
			std::lock_guard<std::mutex> lock(MUTEX);
			SERVICES[std::type_index(typeid(T))] = p_service;
		}

		template <class T> static T& Get() {
			std::lock_guard<std::mutex> lock(MUTEX);
			return *std::any_cast<T*>(SERVICES.at(std::type_index(typeid(T))));
		}

		template <class T> static bool Check() {
			std::lock_guard<std::mutex> lock(MUTEX);
			return SERVICES.contains(std::type_index(typeid(T)));
		}

		template <class T> static void Remove() {
			std::lock_guard<std::mutex> lock(MUTEX);
			SERVICES.erase(std::type_index(typeid(T)));
		}

	private:
		static std::mutex MUTEX;
		static std::map<std::type_index, ServiceType> SERVICES;
	};
}
}
