#include "camera.h"

using namespace IKIGAI;
using namespace IKIGAI::RENDER;

Camera::Camera()
	: projectionMode(ProjectionMode::PERSPECTIVE)
	, fov(45.0f)
	, size(5.0f)
	, _near(0.1f)
	, _far(100.f)
	, frustumGeometryCulling(false)
	, frustumLightCulling(false) { }

void Camera::cacheMatrices(unsigned p_windowWidth, unsigned p_windowHeight, const MATH::Vector3f& p_position, const MATH::QuaternionF& p_rotation) {
	cacheProjectionMatrix(p_windowWidth, p_windowHeight);
	cacheViewMatrix(p_position, p_rotation);
	cacheFrustum(viewMatrix, projectionMatrix);
}

void Camera::cacheProjectionMatrix(unsigned p_windowWidth, unsigned p_windowHeight) {
	projectionMatrix = calculateProjectionMatrix(p_windowWidth, p_windowHeight);
}

void Camera::cacheViewMatrix(const MATH::Vector3f& p_position, const MATH::QuaternionF& p_rotation) {
	viewMatrix = calculateViewMatrix(p_position, p_rotation);
}

void Camera::cacheProjectionMatrix(MATH::Matrix4f mat) {
	projectionMatrix = mat;
}

void Camera::cacheViewMatrix(MATH::Matrix4f mat) {
	viewMatrix = mat;
}

void Camera::cacheFrustum(const MATH::Matrix4f& p_view, const MATH::Matrix4f& p_projection) {
	frustum.make(p_projection * p_view);
}

float Camera::getFov() const {
	return fov;
}

float Camera::getSize() const {
	return size;
}

float Camera::getNear() const {
	return _near;
}

float Camera::getFar() const {
	return _far;
}

const IKIGAI::MATH::Matrix4f& Camera::getProjectionMatrix() const {
	return projectionMatrix;
}

const IKIGAI::MATH::Matrix4f& Camera::getViewMatrix() const {
	return viewMatrix;
}

const Frustum& Camera::getFrustum() const {
	return frustum;
}

bool Camera::isFrustumGeometryCulling() const {
	return frustumGeometryCulling;
}

bool Camera::isFrustumGeometryBVHCulling() const {
	return frustumGeometryBVHCulling;
}

bool Camera::isFrustumLightCulling() const {
	return frustumLightCulling;
}

Camera::ProjectionMode Camera::getProjectionMode() const {
	return projectionMode;
}

void Camera::setFov(float p_value) {
	fov = p_value;
}

void Camera::setSize(float p_value) {
	size = p_value;
}

void Camera::setNear(float p_value) {
	_near = p_value;
}

void Camera::setFar(float p_value) {
	_far = p_value;
}

void Camera::setFrustumGeometryCulling(bool p_enable) {
	frustumGeometryCulling = p_enable;
}

void Camera::setFrustumGeometryBVHCulling(bool p_enable) {
	frustumGeometryBVHCulling = p_enable;
}

void Camera::setFrustumLightCulling(bool p_enable) {
	frustumLightCulling = p_enable;
}

void Camera::setProjectionMode(ProjectionMode p_projectionMode) {
	projectionMode = p_projectionMode;
}

void Camera::setView(MATH::Matrix4f& in) {
	viewMatrix = in;
}

IKIGAI::MATH::Matrix4f Camera::calculateProjectionMatrix(uint16_t p_windowWidth, uint16_t p_windowHeight) const {

	const auto ratio = p_windowWidth / static_cast<float>(p_windowHeight);

	switch (projectionMode) {
	case ProjectionMode::ORTHOGRAPHIC:
		return MATH::Matrix4f::CreateOrthographic(size, ratio, _near, _far);

	case ProjectionMode::PERSPECTIVE:
		return MATH::Matrix4f::CreatePerspective(fov, ratio, _near, _far);

	default:
		return MATH::Matrix4f::Identity;
	}
}

IKIGAI::MATH::Vector3f Camera::calculateViewVector(const MATH::Vector3f& p_position, const MATH::QuaternionF& p_rotation) const {
	const auto forward = p_rotation * MATH::Vector3f::Forward;
	return { p_position.x + forward.x, p_position.y + forward.y, p_position.z + forward.z };
}

IKIGAI::MATH::Matrix4f Camera::calculateViewMatrix(const MATH::Vector3f& p_position, const MATH::QuaternionF& p_rotation) const {
	const auto up = p_rotation * MATH::Vector3f::Up;
	const auto forward = p_rotation * MATH::Vector3f::Forward;

	return MATH::Matrix4f::CreateView(p_position.x, p_position.y, p_position.z, // Position
		p_position.x + forward.x, p_position.y + forward.y, p_position.z + forward.z, // LookAt (Position + Forward)
		up.x, up.y, up.z // Up Vector
	);
}
