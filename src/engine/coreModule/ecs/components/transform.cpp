#include "transform.h"
#include "generated/transform.generated.h"

#include <windowModule/window/window.h>

#include "coreModule/ecs/object.h"
#include "resourceModule/serviceManager.h"
#include "windowModule/window/window.h"

using namespace IKIGAI::ECS;
using namespace IKIGAI::MATH;

// Transform Notifier

TransformNotifier::NotificationHandlerID TransformNotifier::addNotificationHandler(NotificationHandler notificationHandler) {
	NotificationHandlerID handlerID = availableHandlerID++;
	notificationHandlers.emplace(handlerID, notificationHandler);
	return handlerID;
}

void TransformNotifier::notifyChildren(Notification p_notification) {
	if (!notificationHandlers.empty())
		for (auto const& [id, handler] : notificationHandlers)
			handler(p_notification);
}

bool TransformNotifier::removeNotificationHandler(const NotificationHandlerID& p_notificationHandlerID) {
	return notificationHandlers.erase(p_notificationHandlerID) != 0;
}

// Transform Component

TransformComponent::TransformComponent(UTILS::Ref<ECS::Object> obj, Vector3f localPosition, QuaternionF localRotation, Vector3f localScale)
	: ComponentBase(obj) {
	__NAME__ = "TransformComponent";
	transform = std::make_shared<Transform>();
	transform->generateMatrices(localPosition, localRotation, localScale);
}

TransformComponent::TransformComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: TransformComponent(obj, descriptor.LocalPosition, QuaternionF(descriptor.LocalRotation), descriptor.LocalScale) { }

TransformComponent::Descriptor TransformComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<TransformComponent>();
	descriptor.LocalPosition = getLocalPosition();
	descriptor.LocalRotation = getLocalRotationDeg();
	descriptor.LocalScale = getLocalScale();
	return descriptor;
}

void TransformComponent::setParent(TransformComponent& parent) {
	transform->setParent(parent.getTransform());
}

bool TransformComponent::removeParent() {
	return transform->removeParent();
}

bool TransformComponent::hasParent() const {
	return transform->hasParent();
}

void TransformComponent::setLocalPosition(Vector3f newPosition) {
	transform->setLocalPosition(newPosition);
	obj->componentChangedEvent.run(this);
}

void TransformComponent::setLocalRotation(QuaternionF newRotation) {
	transform->setLocalRotation(newRotation);
	obj->componentChangedEvent.run(this);
}

void TransformComponent::setLocalScale(Vector3f newScale) {
	transform->setLocalScale(newScale);
	obj->componentChangedEvent.run(this);
}

void TransformComponent::translateLocal(const Vector3f& translation) {
	transform->translateLocal(translation);
	obj->componentChangedEvent.run(this);
}

void TransformComponent::rotateLocal(const QuaternionF& p_rotation) {
	transform->rotateLocal(p_rotation);
	obj->componentChangedEvent.run(this);
}

void TransformComponent::scaleLocal(const Vector3f& p_scale) {
	transform->scaleLocal(p_scale);
	obj->componentChangedEvent.run(this);
}

const Vector3f& TransformComponent::getLocalPosition() const {
	return transform->getLocalPosition();
}

const QuaternionF& TransformComponent::getLocalRotation() const {
	return transform->getLocalRotation();
}

const Vector3f& TransformComponent::getLocalScale() const {
	return transform->getLocalScale();
}

const Vector3f& TransformComponent::getWorldPosition() const {
	return transform->getWorldPosition();
}

const QuaternionF& TransformComponent::getWorldRotation() const {
	return transform->getWorldRotation();
}

const Vector3f& TransformComponent::getWorldScale() const {
	return transform->getWorldScale();
}

const Matrix4f& TransformComponent::getLocalMatrix() const {
	return transform->getLocalMatrix();
}

const Matrix4f& TransformComponent::getWorldMatrix() const {
	return transform->getWorldMatrix();
}

Transform& TransformComponent::getTransform() {
	return *transform;
}

Vector3f TransformComponent::getWorldForward() const {
	return transform->getWorldForward();
}

Vector3f TransformComponent::getWorldUp() const {
	return transform->getWorldUp();
}

Vector3f TransformComponent::getWorldRight() const {
	return transform->getWorldRight();
}

Vector3f TransformComponent::getLocalForward() const {
	return transform->getLocalForward();
}

Vector3f TransformComponent::getLocalUp() const {
	return transform->getLocalUp();
}

Vector3f TransformComponent::getLocalRight() const {
	return transform->getLocalRight();
}

Matrix3f TransformComponent::GetNormalMatrix(const Matrix4f& model) const {
	if (transform->getWorldScale().x == transform->getWorldScale().y && transform->getWorldScale().y == transform->getWorldScale().z) {
		auto buf = model;
		return Matrix3(buf(0, 0), buf(0, 1), buf(0, 2), buf(1, 0), buf(1, 1), buf(1, 2), buf(2, 0), buf(2, 1), buf(2, 2));
	}
	else {
		auto buf = Matrix4f::Transpose(Matrix4f::Inverse(model));
		return Matrix3(buf(0, 0), buf(0, 1), buf(0, 2), buf(1, 0), buf(1, 1), buf(1, 2), buf(2, 0), buf(2, 1), buf(2, 2));
	}
}

// Transform

Transform::Transform(Vector3f localPosition, QuaternionF localRotation, Vector3f localScale)
	: notificationHandlerID(-1)
	, parent(nullptr) {
	generateMatrices(localPosition, localRotation, localScale);
}

Transform::~Transform() {
	notifier.notifyChildren(TransformNotifier::Notification::DESTROYED);
}

void Transform::notificationHandler(TransformNotifier::Notification p_notification) {
	switch (p_notification) {
	case TransformNotifier::Notification::CHANGED:
		updateWorldMatrix();
		break;

	case TransformNotifier::Notification::DESTROYED:
		parent = nullptr;
		generateMatrices(worldPosition, worldRotation, worldScale);
		updateWorldMatrix();
		break;
	}
}

void Transform::setParent(Transform& p_parent) {
	parent = &p_parent;

	notificationHandlerID = parent->notifier.addNotificationHandler(std::bind(&Transform::notificationHandler, this, std::placeholders::_1));

	updateWorldMatrix();
}

bool Transform::removeParent() {
	if (parent != nullptr) {
		parent->notifier.removeNotificationHandler(notificationHandlerID);
		parent = nullptr;
		updateWorldMatrix();

		return true;
	}

	return false;
}

bool Transform::hasParent() const {
	return parent != nullptr;
}

void Transform::generateMatrices(Vector3f position, QuaternionF rotation, Vector3f scale) {
	if (use2d) {
		auto screenSz = RESOURCES::ServiceManager::Get<WINDOW::Window>().getSize();
		auto parentSize = hasParent() ? parent->mSize : Vector2f(screenSz.x, screenSz.y);

		localMatrix = Matrix4f();
		localMatrix *= Matrix4f::Translation(Vector3f(parentSize.x * mAnchor.x, parentSize.y * mAnchor.y, 0.0f));
		localMatrix *= Matrix4f::Translation(Vector3f(mSize.x * -mPivot.x * localScale.x, mSize.y * -mPivot.y * localScale.y, 0.0f));
		localMatrix *= Matrix4f::Translation(position);
		localMatrix *= Matrix4f::Translation({ mSize.x * mPivot.x * localScale.x, mSize.y * mPivot.y * localScale.y, 0.0f });
		localMatrix *= QuaternionF::ToMatrix4(QuaternionF::Normalize(localRotation));
		localMatrix *= Matrix4f::Translation({ mSize.x * -mPivot.x * localScale.x, mSize.y * -mPivot.y * localScale.y, 0.0f });
		localMatrix *= Matrix4f::Scaling(localScale /* * Vector3f(mSize.x, mSize.y, 1.0f)*/);
	}
	else {
		localMatrix = Matrix4f::Translation(position) * QuaternionF::ToMatrix4(QuaternionF::Normalize(rotation)) * Matrix4f::Scaling(scale);
	}
	localPosition = position;
	localRotation = rotation;
	localScale = scale;
	updateWorldMatrix();
}

void Transform::updateWorldMatrix() {
	prevWorldMatrix = worldMatrix;
	worldMatrix = hasParent() ? parent->worldMatrix * localMatrix : localMatrix;
	preDecomposeWorldMatrix();
	notifier.notifyChildren(TransformNotifier::Notification::CHANGED);
}

void Transform::setLocalPosition(Vector3f newPosition) {
	generateMatrices(newPosition, localRotation, localScale);
}

void Transform::setLocalRotation(QuaternionF p_newRotation) {
	generateMatrices(localPosition, p_newRotation, localScale);
}

void Transform::setLocalScale(Vector3f p_newScale) {
	generateMatrices(localPosition, localRotation, p_newScale);
}

void Transform::setLocalAnchor(Vector2f p_new) {
	mAnchor = p_new;
	generateMatrices(localPosition, localRotation, localScale);
}
void Transform::setLocalPivot(Vector2f p_new) {
	mPivot = p_new;
	generateMatrices(localPosition, localRotation, localScale);
}
void Transform::setLocalSize(Vector2f p_new) {
	mSize = p_new;
	generateMatrices(localPosition, localRotation, localScale);
}

void Transform::translateLocal(const Vector3f& p_translation) {
	setLocalPosition(localPosition + p_translation);
}

void Transform::rotateLocal(const QuaternionF& p_rotation) {
	setLocalRotation(localRotation * p_rotation);
}

void Transform::scaleLocal(const Vector3f& p_scale) {
	setLocalScale(Vector3f(localScale.x * p_scale.x, localScale.y * p_scale.y, localScale.z * p_scale.z));
}

const Vector3f& Transform::getLocalPosition() const {
	return localPosition;
}

const QuaternionF& Transform::getLocalRotation() const {
	return localRotation;
}

const Vector3f& Transform::getLocalScale() const {
	return localScale;
}

const Vector2f& Transform::getLocalAnchor() const {
	return mAnchor;
}
const Vector2f& Transform::getLocalPivot() const {
	return mPivot;
}
const Vector2f& Transform::getLocalSize() const {
	return mSize;
}

const Vector3f& Transform::getWorldPosition() const {
	return worldPosition;
}

const QuaternionF& Transform::getWorldRotation() const {
	return worldRotation;
}

const Vector3f& Transform::getWorldScale() const {
	return worldScale;
}

const Matrix4f& Transform::getLocalMatrix() const {
	return localMatrix;
}

const Matrix4f& Transform::getWorldMatrix() const {
	return worldMatrix;
}

const Matrix4f& Transform::getPrevWorldMatrix() const {
	return prevWorldMatrix;
}

Vector3f Transform::getWorldForward() const {
	return worldRotation * Vector3f::Forward;
}

Vector3f Transform::getWorldUp() const {
	return worldRotation * Vector3f::Up;
}

Vector3f Transform::getWorldRight() const {
	return worldRotation * Vector3f::Right;
}

Vector3f Transform::getLocalForward() const {
	return localRotation * Vector3f::Forward;
}

Vector3f Transform::getLocalUp() const {
	return localRotation * Vector3f::Up;
}

Vector3f Transform::getLocalRight() const {
	return localRotation * Vector3f::Right;
}

void Transform::setPrevWorldMatrix(Matrix4f m) {
	prevWorldMatrix = m;
}

void Transform::turnOnAnchorPivot() {
	use2d = true;
}

void Transform::turnOffAnchorPivot() {
	use2d = false;
}

bool Transform::isAnchorPivotMode() const {
	return use2d;
}

void Transform::preDecomposeWorldMatrix() {
	worldPosition.x = worldMatrix(0, 3);
	worldPosition.y = worldMatrix(1, 3);
	worldPosition.z = worldMatrix(2, 3);

	Vector3f columns[3] = {
		{ worldMatrix(0, 0), worldMatrix(1, 0), worldMatrix(2, 0) },
		{ worldMatrix(0, 1), worldMatrix(1, 1), worldMatrix(2, 1) },
		{ worldMatrix(0, 2), worldMatrix(1, 2), worldMatrix(2, 2) },
	};

	worldScale.x = Vector3f::Length(columns[0]);
	worldScale.y = Vector3f::Length(columns[1]);
	worldScale.z = Vector3f::Length(columns[2]);

	if (worldScale.x) {
		columns[0] /= worldScale.x;
	}
	if (worldScale.y) {
		columns[1] /= worldScale.y;
	}
	if (worldScale.z) {
		columns[2] /= worldScale.z;
	}

	Matrix3 rotationMatrix(
		columns[0].x, columns[1].x, columns[2].x, columns[0].y, columns[1].y, columns[2].y, columns[0].z, columns[1].z, columns[2].z);

	worldRotation = QuaternionF(rotationMatrix);
}

void Transform::preDecomposeLocalMatrix() {
	localPosition.x = localMatrix(0, 3);
	localPosition.y = localMatrix(1, 3);
	localPosition.z = localMatrix(2, 3);

	Vector3f columns[3] = {
		{ localMatrix(0, 0), localMatrix(1, 0), localMatrix(2, 0) },
		{ localMatrix(0, 1), localMatrix(1, 1), localMatrix(2, 1) },
		{ localMatrix(0, 2), localMatrix(1, 2), localMatrix(2, 2) },
	};

	localScale.x = Vector3f::Length(columns[0]);
	localScale.y = Vector3f::Length(columns[1]);
	localScale.z = Vector3f::Length(columns[2]);

	if (localScale.x) {
		columns[0] /= localScale.x;
	}
	if (localScale.y) {
		columns[1] /= localScale.y;
	}
	if (localScale.z) {
		columns[2] /= localScale.z;
	}

	Matrix3 rotationMatrix(
		columns[0].x, columns[1].x, columns[2].x, columns[0].y, columns[1].y, columns[2].y, columns[0].z, columns[1].z, columns[2].z);

	localRotation = QuaternionF(rotationMatrix);
}

float TransformComponent::getLocalScaleX() const {
	return transform->getLocalScale().x;
}
float TransformComponent::getLocalScaleY() const {
	return transform->getLocalScale().y;
}
float TransformComponent::getLocalScaleZ() const {
	return transform->getLocalScale().z;
}
Vector3f TransformComponent::getLocalScale_() const {
	return transform->getLocalScale();
}

void TransformComponent::setLocalScaleX(float val) {
	transform->setLocalScale({ val, getLocalScaleY(), getLocalScaleZ() });
}
void TransformComponent::setLocalScaleY(float val) {
	transform->setLocalScale({ getLocalScaleX(), val, getLocalScaleZ() });
}
void TransformComponent::setLocalScaleZ(float val) {
	transform->setLocalScale({ getLocalScaleX(), getLocalScaleY(), val });
}
void TransformComponent::setLocalScale_(Vector3f val) {
	transform->setLocalScale(val);
}

float TransformComponent::getLocalPositionX() const {
	return transform->getLocalPosition().x;
}
float TransformComponent::getLocalPositionY() const {
	return transform->getLocalPosition().y;
}
float TransformComponent::getLocalPositionZ() const {
	return transform->getLocalPosition().z;
}
Vector3f TransformComponent::getLocalPosition_() const {
	return transform->getLocalPosition();
}

void TransformComponent::setLocalPositionX(float val) {
	transform->setLocalPosition({ val, getLocalPositionY(), getLocalPositionZ() });
}
void TransformComponent::setLocalPositionY(float val) {
	transform->setLocalPosition({ getLocalPositionX(), val, getLocalPositionZ() });
}
void TransformComponent::setLocalPositionZ(float val) {
	transform->setLocalPosition({ getLocalPositionX(), getLocalPositionY(), val });
}
void TransformComponent::setLocalPosition_(Vector3f val) {
	transform->setLocalPosition(val);
}

// In degree
float TransformComponent::getLocalRotationX() const {
	const auto rot = QuaternionF::ToEulerAngles(getLocalRotation());
	return TO_DEGREES(rot.x);
}
float TransformComponent::getLocalRotationY() const {
	const auto rot = QuaternionF::ToEulerAngles(getLocalRotation());
	return TO_DEGREES(rot.y);
}
float TransformComponent::getLocalRotationZ() const {
	const auto rot = QuaternionF::ToEulerAngles(getLocalRotation());
	return TO_DEGREES(rot.z);
}
Vector3f TransformComponent::getLocalRotationDeg() const {
	auto rot = QuaternionF::ToEulerAngles(getLocalRotation());
	rot.x = TO_DEGREES(rot.x);
	rot.y = TO_DEGREES(rot.y);
	rot.z = TO_DEGREES(rot.z);
	return rot;
}

void TransformComponent::setLocalRotationX(float val) {
	if (val > 180.0f) {
		val = -180.0f + (val - 180.0f);
	}
	else if (val < -180.0f) {
		val = 180.0f + (val + 180.0f);
	}
	const auto rot = QuaternionF::ToEulerAngles(getLocalRotation());
	Vector3f rotVec = Vector3f { val, TO_DEGREES(rot.y), TO_DEGREES(rot.z) };
	setLocalRotation(QuaternionF({ rotVec }));
}
void TransformComponent::setLocalRotationY(float val) {
	if (val > 89.0f) {
		val = 89.0f;
	}
	else if (val < -89.0f) {
		val = -89.0f;
	}
	const auto rot = QuaternionF::ToEulerAngles(getLocalRotation());
	Vector3f rotVec = Vector3f { TO_DEGREES(rot.x), val, TO_DEGREES(rot.z) };
	setLocalRotation(QuaternionF({ rotVec }));
}
void TransformComponent::setLocalRotationZ(float val) {
	if (val > 180.0f) {
		val = -180.0f + (val - 180.0f);
	}
	else if (val < -180.0f) {
		val = 180.0f + (val + 180.0f);
	}
	const auto rot = QuaternionF::ToEulerAngles(getLocalRotation());
	Vector3f rotVec = Vector3f { TO_DEGREES(rot.x), TO_DEGREES(rot.y), val };
	setLocalRotation(QuaternionF({ rotVec }));
}

void TransformComponent::setLocalRotationDeg(Vector3f val) {
	if (val.x > 180.0f) {
		val.x = -180.0f + (val.x - 180.0f);
	}
	else if (val.x < -180.0f) {
		val.x = 180.0f + (val.x + 180.0f);
	}
	if (val.z > 180.0f) {
		val.z = -180.0f + (val.z - 180.0f);
	}
	else if (val.z < -180.0f) {
		val.z = 180.0f + (val.z + 180.0f);
	}
	if (val.y > 89.0f) {
		val.y = 89.0f;
	}
	else if (val.y < -89.0f) {
		val.y = -89.0f;
	}
	setLocalRotation(QuaternionF({ val }));
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::TransformComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	return true;
}();
}
