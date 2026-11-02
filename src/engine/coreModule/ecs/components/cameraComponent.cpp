#include "cameraComponent.h"

#include "../object.h"
// #include "../../../game/Chunk.h"
#include <renderModule/camera.h>
#include <windowModule/window/window.h>

#include "renderModule/backends/gl/textureGl.h"
#include "sceneModule/sceneManager.h"
using namespace IKIGAI::ECS;

CameraComponent::CameraComponent(UTILS::Ref<ECS::Object> _obj)
	: ComponentBase(_obj) {
	__NAME__ = "CameraComponent";
}

CameraComponent::CameraComponent(UTILS::Ref<ECS::Object> _obj, const Descriptor& _descriptor)
	: ComponentBase(_obj) {
	__NAME__ = "CameraComponent";
	setFov(_descriptor.Fov);
	setSize(_descriptor.Size);
	setNear(_descriptor.Near);
	setFar(_descriptor.Far);
	setFrustumGeometryBVHCulling(_descriptor.GeometryBVHCulling);
	setFrustumGeometryCulling(_descriptor.GeometryCulling);
	setFrustumLightCulling(_descriptor.LightCulling);
	setProjectionMode(_descriptor.Mode);

	std::cout << "LOAD CAMERA" << std::endl;
}

void CameraComponent::ResizeRenderTexture(size_t w, size_t h) { }
void CameraComponent::setFov(float value) {
	camera.setFov(value);
}

void CameraComponent::setSize(float value) {
	camera.setSize(value);
}

void CameraComponent::setNear(float value) {
	camera.setNear(value);
}

void CameraComponent::setFar(float value) {
	camera.setFar(value);
}

void CameraComponent::setFrustumGeometryCulling(bool enable) {
	camera.setFrustumGeometryCulling(enable);
}

void CameraComponent::setFrustumGeometryBVHCulling(bool enable) {
	camera.setFrustumGeometryBVHCulling(enable);
}

void CameraComponent::setFrustumLightCulling(bool enable) {
	camera.setFrustumLightCulling(enable);
}

void CameraComponent::setProjectionMode(RENDER::Camera::ProjectionMode p_projectionMode) {
	camera.setProjectionMode(p_projectionMode);
}

float CameraComponent::getFov() const {
	return camera.getFov();
}

float CameraComponent::getSize() const {
	return camera.getSize();
}

float CameraComponent::getNear() const {
	return camera.getNear();
}

float CameraComponent::getFar() const {
	return camera.getFar();
}

bool CameraComponent::isFrustumGeometryCulling() const {
	return camera.isFrustumGeometryCulling();
}

bool CameraComponent::isFrustumGeometryBVHCulling() const {
	return camera.isFrustumGeometryBVHCulling();
}

bool CameraComponent::isFrustumLightCulling() const {
	return camera.isFrustumLightCulling();
}

IKIGAI::RENDER::Camera::ProjectionMode CameraComponent::getProjectionMode() const {
	return camera.getProjectionMode();
}

IKIGAI::RENDER::Camera& CameraComponent::getCamera() {
	return camera;
}

CameraComponent::Descriptor CameraComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<CameraComponent>();
	descriptor.Fov = getFov();
	descriptor.Size = getSize();
	descriptor.Near = getNear();
	descriptor.Far = getFar();
	descriptor.GeometryCulling = isFrustumGeometryCulling();
	descriptor.GeometryBVHCulling = isFrustumGeometryBVHCulling();
	descriptor.LightCulling = isFrustumLightCulling();
	descriptor.Mode = getProjectionMode();
	return descriptor;
}

VrCameraComponent::VrCameraComponent(UTILS::Ref<ECS::Object> _obj)
	: CameraComponent(_obj) {
	__NAME__ = "VrCamera";
	auto screenRes = RESOURCES::ServiceManager::Get<WINDOW::Window>().getSize();
#ifdef OPENGL_BACKEND
	// TODO: fix it
	// leftTexture = RENDER::TextureGl::CreateForAttach(screenRes.x, screenRes.y, 0x1406/*GL_FLOAT*/);
	// //RENDER::TextureGl::createDepthForAttach(screenRes.x, screenRes.y); rightTexture = RENDER::TextureGl::CreateForAttach(screenRes.x,
	// screenRes.y, 0x1406); //RENDER::TextureGl::createDepthForAttach(screenRes.x, screenRes.y);
#endif

	{
		auto camLeft = createObject("__VrCamera_CameraLeft");
		auto cam = camLeft->addComponent<CameraComponent>();
		cam->setFov(45.0f);
		cam->setSize(5.0f);
		cam->setNear(0.1f);
		cam->setFar(1000.0f);
		cam->setFrustumGeometryCulling(false);
		cam->setFrustumLightCulling(false);
		cam->setProjectionMode(RENDER::Camera::ProjectionMode::PERSPECTIVE);
		left = camLeft;
	}
	{
		auto camRight = createObject("__VrCamera_CameraRight");
		auto cam = camRight->addComponent<CameraComponent>();
		cam->setFov(45.0f);
		cam->setSize(5.0f);
		cam->setNear(0.1f);
		cam->setFar(1000.0f);
		cam->setFrustumGeometryCulling(false);
		cam->setFrustumLightCulling(false);
		cam->setProjectionMode(RENDER::Camera::ProjectionMode::PERSPECTIVE);
		right = camRight;
	}
}

VrCameraComponent::VrCameraComponent(UTILS::Ref<ECS::Object> _obj, const Descriptor& _descriptor)
	: CameraComponent(_obj) {
	__NAME__ = "VrCamera";
	// TODO: init left/right camera
	setFov(_descriptor.Fov);
	setSize(_descriptor.Size);
	setNear(_descriptor.Near);
	setFar(_descriptor.Far);
	setFrustumGeometryBVHCulling(_descriptor.GeometryBVHCulling);
	setFrustumGeometryCulling(_descriptor.GeometryCulling);
	setFrustumLightCulling(_descriptor.LightCulling);
	setProjectionMode(_descriptor.Mode);
}

std::shared_ptr<IKIGAI::ECS::Object> VrCameraComponent::createObject(const std::string& name) {
	static int id = 0;
	auto instance = std::make_shared<ECS::Object>(Object::Id(Object::Id::ID(id)), name, "");
	// if (isExecute) {
	instance->setActive(true);
	if (instance->getIsActive()) {
		instance->onEnable();
		instance->onStart();
	}
	//}
	return instance;
}

void VrCameraComponent::onUpdate(std::chrono::duration<double> dt) {
	updateEyes();
}

void VrCameraComponent::updateEyes() {
	auto& position = obj->getTransform()->getLocalPosition();

	auto LEyeTransform = left->getTransform();
	auto REyeTransform = right->getTransform();

	auto LEyeDistance = -this->EyeDistance * obj->getTransform()->getLocalRight();
	auto REyeDistance = +this->EyeDistance * obj->getTransform()->getLocalRight();

	LEyeTransform->setLocalPosition(position + LEyeDistance);
	REyeTransform->setLocalPosition(position + REyeDistance);

	auto LEyeDirection = this->FocusDistance * obj->getTransform()->getWorldPosition() - LEyeDistance;
	auto REyeDirection = this->FocusDistance * obj->getTransform()->getWorldPosition() - REyeDistance;

	auto screenRes = RESOURCES::ServiceManager::Get<WINDOW::Window>().getSize();
	left->getComponent<CameraComponent>()->getCamera().cacheMatrices(
		screenRes.x, screenRes.y, position + LEyeDistance, obj->getTransform()->getWorldRotation());
	right->getComponent<CameraComponent>()->getCamera().cacheMatrices(
		screenRes.x, screenRes.y, position + REyeDistance, obj->getTransform()->getWorldRotation());
}

VrCameraComponent::Descriptor VrCameraComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<VrCameraComponent>();
	descriptor.Fov = getFov();
	descriptor.Size = getSize();
	descriptor.Near = getNear();
	descriptor.Far = getFar();
	descriptor.GeometryCulling = isFrustumGeometryCulling();
	descriptor.GeometryBVHCulling = isFrustumGeometryBVHCulling();
	descriptor.LightCulling = isFrustumLightCulling();
	descriptor.Mode = getProjectionMode();
	return descriptor;
}

/*
ArCameraComponent::ArCameraComponent(UTILS::Ref<ECS::Object> obj) : Component(obj) {
	__NAME__ = "ArCamera";
	auto screenRes = RESOURCES::ServiceManager::Get<WINDOW_SYSTEM::Window>().getSize();
	cameraTexture = std::make_shared<RESOURCES::Texture>(screenRes.x, screenRes.y);
	cameraTexture->setFilter(RESOURCES::TextureFiltering::NEAREST, RESOURCES::TextureFiltering::NEAREST);

	cap.open(0);
	if (!cap.isOpened()) {
		std::cerr << "couldn't open capture." << std::endl;
		throw;
	}
	dictionary = getPredefinedDictionary(cv::aruco::DICT_6X6_250);
	markerDetector = cv::aruco::ArucoDetector(dictionary);

	float data[9] = { 628.158, 0., 324.099,
		0., 628.156, 260.908,
		0., 0., 1. };
	cameraMatrix = cv::Mat(3, 3, CV_8UC1, data);

	float data2[5] = { 0.0995485, -0.206384,
	   0.00754589, 0.00336531, 0 };
	distCoeffs = cv::Mat(5, 1, CV_8UC1, data2);

}

unsigned char* cvMat2TexInput(cv::Mat& img) {
	cv::cvtColor(img, img, cv::COLOR_BGR2RGBA);
	cv::flip(img, img, -1);
	return img.data;
}

cv::Matx44f createGLMatrix(const cv::Mat& tVec, const cv::Mat& rVec) {
	cv::Mat rMat;
	cv::Rodrigues(rVec, rMat);


	cv::Matx44f glM((float)rMat.at<double>(0, 0), (float)rMat.at<double>(0, 1), (float)rMat.at<double>(0, 2), (float)tVec.at<double>(0, 0),
					(float)-rMat.at<double>(1, 0), (float)-rMat.at<double>(1, 1), (float)-rMat.at<double>(1, 2), (float)-tVec.at<double>(1, 0),
					(float)-rMat.at<double>(2, 0), (float)-rMat.at<double>(2, 1), (float)-rMat.at<double>(2, 2), (float)-tVec.at<double>(2, 0),
					0.0f, 0.0f, 0.0f, 1.0f);
	return glM;
}

void ArCameraComponent::onUpdate(std::chrono::duration<double> dt) {
	cap >> frame;
	//if (frame.empty())
	//	break;

	std::vector<std::vector<cv::Point2f>> markerCorners, rejectedCandidates;
	std::vector<int> markerIds;
	markerDetector.detectMarkers(frame, markerCorners, markerIds, rejectedCandidates);

	if (markerCorners.size() > 0 && markerIds.size() > 0) {

		cv::aruco::drawDetectedMarkers(frame, markerCorners, markerIds);

		int nMarkers = markerCorners.size();

		std::vector<cv::Vec3d> rvecs(nMarkers), tvecs(nMarkers);
		cv::aruco::estimatePoseSingleMarkers(markerCorners, 0.5, cameraMatrix, distCoeffs, rvecs, tvecs);

		markerFind = true;
		//cv::Matx44f ovm = createGLMatrix(cv::Mat(tvecs[0]), cv::Mat(tvecs[0]));

		const auto& up = MATHGL::Vector3(rvecs[0][0], rvecs[0][1], rvecs[0][2]) * MATHGL::Vector3::Up;
		const auto& forward = MATHGL::Vector3(rvecs[0][0], rvecs[0][1], rvecs[0][2]) * MATHGL::Vector3::Forward;

		view = MATHGL::Matrix4::CreateView(
			tvecs[0][0]*10, tvecs[0][1] * 10, tvecs[0][2] * 10,
			tvecs[0][0] * 10 + forward.x, tvecs[0][1] * 10 + forward.y, tvecs[0][2] * 10 + forward.z,
			up.x, up.y, up.z
		);

		//view = MATHGL::Matrix4(
		//	ovm(0, 0), ovm(0, 1), ovm(0, 2), ovm(0, 3),
		//	ovm(1, 0), ovm(1, 1), ovm(1, 2), ovm(1, 3),
		//	ovm(2, 0), ovm(2, 1), ovm(2, 2), ovm(2, 3),
		//	ovm(3, 0), ovm(3, 1), ovm(3, 2), ovm(3, 3)
		//);


		for (auto i : markerIds) {
			std::cout << i << " [pos: " << tvecs[0][0] << " " << tvecs[0][1] << " " << tvecs[0][2] << "] [rot "
				<< rvecs[0][0] << " " << rvecs[0][1] << " " << rvecs[0][2] << "]\n";
		}
	}
	else {
		markerFind = false;
	}

	unsigned char* image = cvMat2TexInput(frame);
	//auto screenRes = RESOURCES::ServiceManager::Get<WINDOW_SYSTEM::Window>().getSize();
	cameraTexture->bindWithoutAttach();

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frame.cols, frame.rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(RESOURCES::TextureFiltering::LINEAR_MIPMAP_LINEAR));
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(RESOURCES::TextureFiltering::LINEAR));

	cameraTexture->unbind();

	//cv::imwrite("test.jpg", frame);
}*/

// #include <rttr/registration>
//
// RTTR_REGISTRATION
//{
//	rttr::registration::enumeration<IKIGAI::RENDER::Camera::ProjectionMode>("ProjectionMode")
//	(
//		rttr::value("ORTHOGRAPHIC",    IKIGAI::RENDER::Camera::ProjectionMode::ORTHOGRAPHIC),
//		rttr::value("PERSPECTIVE",   IKIGAI::RENDER::Camera::ProjectionMode::PERSPECTIVE)
//	);
//	rttr::registration::class_<IKIGAI::ECS::CameraComponent>("CameraComponent")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE| MetaInfo::USE_IN_EDITOR_ANIMATION)
//	)
//	.property("Far", &IKIGAI::ECS::CameraComponent::getFar, &IKIGAI::ECS::CameraComponent::setFar)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE| MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 10000.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	)
//	.property("Fov", &IKIGAI::ECS::CameraComponent::getFov, &IKIGAI::ECS::CameraComponent::setFov)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE| MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 10000.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	)
//	.property("Size", &IKIGAI::ECS::CameraComponent::getSize, &IKIGAI::ECS::CameraComponent::setSize)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE| MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 10000.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	)
//	.property("Near", &IKIGAI::ECS::CameraComponent::getNear, &IKIGAI::ECS::CameraComponent::setNear)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE| MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 10000.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	)
//	.property("ProjectionMode", &IKIGAI::ECS::CameraComponent::getProjectionMode, &IKIGAI::ECS::CameraComponent::setProjectionMode)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE| MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::COMBO)
//	)
//	.property("FrustumCulling", &IKIGAI::ECS::CameraComponent::isFrustumGeometryCulling, &IKIGAI::ECS::CameraComponent::setFrustumGeometryCulling)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::BOOL)
//		)
//	.property("FrustumLight", &IKIGAI::ECS::CameraComponent::isFrustumLightCulling, &IKIGAI::ECS::CameraComponent::setFrustumLightCulling)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::BOOL)
//	)
//	.property("FrustumCullingBVH", &IKIGAI::ECS::CameraComponent::isFrustumGeometryBVHCulling,
//&IKIGAI::ECS::CameraComponent::setFrustumGeometryBVHCulling)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::BOOL)
//	);
// }

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::CameraComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::VrCameraComponent>();
	return true;
}();
}
