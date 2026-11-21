#pragma once


#include "component.h"
// #include "../../../game/TextureAtlas.h"
#include <renderModule/light.h>

namespace IKIGAI::ECS {
class Object;
}

namespace IKIGAI::ECS {
class LightComponent : public ComponentBase {
public:
	LightComponent(UTILS::Ref<ECS::Object> obj);
	const RENDER::Light& getData() const;
	const MATH::Vector3f& getColor() const;
	float getIntensity() const;
	void setColor(const MATH::Vector3f& color);
	void setIntensity(float intensity);
	bool getCastShadows() const {
		return data.castShadows;
	}
	void setCastShadows(bool enabled) {
		data.castShadows = enabled;
	}
	float getShadowNear() const {
		return data.shadowNear;
	}
	float getShadowFar() const {
		return data.shadowFar;
	}
	void setShadowNear(float value) {
		data.shadowNear = value;
	}
	void setShadowFar(float value) {
		data.shadowFar = value;
	}

	MATH::Matrix4f Transform;

protected:
	RENDER::Light data;
};
}
