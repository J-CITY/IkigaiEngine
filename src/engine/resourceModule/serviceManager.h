#pragma once

// Compatibility header for existing resource and engine consumers.
#include "utilsModule/serviceManager.h"

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
}
}
