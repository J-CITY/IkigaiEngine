#pragma once
#include <cstdint>

#include "frustum.h"
#include "mathModule/math.h"
#include "utilsModule/enum.h"

IKIGAI_ENUM_NS(IKIGAI::RENDER, ProjectionMode, ORTHOGRAPHIC, PERSPECTIVE)

namespace IKIGAI {
namespace RENDER {
	class Camera {
	public:
		using ProjectionMode = RENDER::ProjectionMode;

		Camera();
		void cacheMatrices(unsigned int winWidth, unsigned int winHeight, const MATH::Vector3f& position, const MATH::QuaternionF& rotation);
		void cacheProjectionMatrix(unsigned int winWidth, unsigned int winHeight);
		void cacheViewMatrix(const MATH::Vector3f& position, const MATH::QuaternionF& rotation);
		void cacheFrustum(const MATH::Matrix4f& view, const MATH::Matrix4f& projection);
		void cacheProjectionMatrix(MATH::Matrix4f mat);
		void cacheViewMatrix(MATH::Matrix4f mat);

		float getFov() const;
		float getSize() const;
		float getNear() const;
		float getFar() const;
		const MATH::Matrix4f& getProjectionMatrix() const;
		const MATH::Matrix4f& getViewMatrix() const;
		const Frustum& getFrustum() const;
		bool isFrustumGeometryCulling() const;
		bool isFrustumGeometryBVHCulling() const;
		bool isFrustumLightCulling() const;
		ProjectionMode getProjectionMode() const;
		void setFov(float p_value);
		void setSize(float p_value);
		void setNear(float p_value);
		void setFar(float p_value);
		void setFrustumGeometryCulling(bool p_enable);
		void setFrustumGeometryBVHCulling(bool p_enable);
		void setFrustumLightCulling(bool p_enable);
		void setProjectionMode(ProjectionMode p_projectionMode);
		void setView(MATH::Matrix4f& in);
		// private:
		MATH::Matrix4f calculateProjectionMatrix(uint16_t p_windowWidth, uint16_t p_windowHeight) const;
		MATH::Matrix4f calculateViewMatrix(const MATH::Vector3f& p_position, const MATH::QuaternionF& p_rotation) const;
		IKIGAI::MATH::Vector3f calculateViewVector(const MATH::Vector3f& p_position, const MATH::QuaternionF& p_rotation) const;

	private:
		Frustum frustum;
		MATH::Matrix4f viewMatrix;
		MATH::Matrix4f projectionMatrix;
		ProjectionMode projectionMode;

		float fov;
		float size;
		float _near;
		float _far;

		bool frustumGeometryCulling = false;
		bool frustumGeometryBVHCulling = false;
		bool frustumLightCulling = false;
	};
}
}
