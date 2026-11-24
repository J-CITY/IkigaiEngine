#pragma once

#ifdef USE_EDITOR
#include "mathModule/math.h"
#include <memory>

namespace IKIGAI::ECS {
class Object;
}
namespace IKIGAI::EDITOR {
struct SceneRay {
	MATH::Vector3f origin {};
	MATH::Vector3f direction {};
	float length = 0;
};

struct ScenePick {
	std::shared_ptr<ECS::Object> object {};
	float distance = 0;
};

struct SceneObjectIntersection {
	bool hasGeometry = false;
	float distance = 0;
};

SceneRay unprojectSceneRay(const MATH::Matrix4f& inverseViewProjection, MATH::Vector2f ndc);
SceneObjectIntersection intersectSceneObject(ECS::Object& object, const SceneRay& ray);
}
#endif
