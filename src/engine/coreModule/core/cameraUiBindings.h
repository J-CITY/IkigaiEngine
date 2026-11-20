#pragma once

#if USE_JSON_VIEWS && !defined(OCULUS)
#include <memory>

namespace IKIGAI::ECS {
class Object;
}

namespace IKIGAI::MATH {
template <class T> struct Quaternion;
template <class T> struct Vector3;
}

namespace IKIGAI::SCENE_SYSTEM {
class SceneManager;
}

namespace IKIGAI::UI {
class ViewStore;
}

namespace IKIGAI::CORE {
struct CameraEditing {
	bool (*isLocked)(const ECS::Object&) = nullptr;
	void (*recordTransform)(const std::shared_ptr<ECS::Object>&, std::weak_ptr<void>, MATH::Vector3<float>, MATH::Quaternion<float>) = nullptr;
};

void RegisterCameraUiBindings(UI::ViewStore& store, SCENE_SYSTEM::SceneManager& manager, CameraEditing editing = {});
}
#endif
