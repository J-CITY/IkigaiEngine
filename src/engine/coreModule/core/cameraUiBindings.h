#pragma once

#if USE_JSON_VIEWS && !defined(OCULUS)
namespace IKIGAI::SCENE_SYSTEM {
class SceneManager;
}

namespace IKIGAI::UI {
class ViewStore;
}

namespace IKIGAI::CORE {
void RegisterCameraUiBindings(UI::ViewStore& store, SCENE_SYSTEM::SceneManager& manager);
}
#endif
