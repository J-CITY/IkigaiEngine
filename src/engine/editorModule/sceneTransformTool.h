#pragma once

#ifdef USE_EDITOR
#include "imgui.h"

#include "editorModule/imguiHelper/ImGuizmo.h"
#include "mathModule/math.h"
#include <memory>

namespace IKIGAI::ECS {
class Object;
}
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::EDITOR {
struct SceneLocalTransform {
	MATH::Vector3f position {};
	MATH::QuaternionF rotation {};
	MATH::Vector3f scale { 1, 1, 1 };
	bool operator==(const SceneLocalTransform&) const = default;
};

class SceneTransformTool {
public:
	void draw(SCENE_SYSTEM::Scene& scene, bool previewOwnsClick);
	bool active() const;
	bool capturesMouse() const;
	bool enabled = false;
	bool showMarkers = true;

private:
	enum class Gesture { None, Surface, Gizmo };
	void finish(bool cancel);

	std::weak_ptr<ECS::Object> mObject {};
	std::weak_ptr<ECS::Object> mParent {};
	std::weak_ptr<void> mScene {};
	SceneLocalTransform mBefore {};
	MATH::Vector3f mAnchor {};
	MATH::Vector3f mPlaneNormal {};
	MATH::Vector3f mWorldStart {};
	ImVec2 mSurfacePress {};
	ImGuizmo::OPERATION mOperation = ImGuizmo::TRANSLATE;
	ImGuizmo::MODE mSpace = ImGuizmo::WORLD;
	Gesture mGesture = Gesture::None;
	bool mSurfaceMoved = false;
	bool mWaitForRelease = false;
};
}
#endif
