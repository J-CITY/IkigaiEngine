#pragma once

#include "component.h"
#include "utilsModule/reflection/reflection_macros.h"

#include <functional>

#include "mathModule/math.h"
#include "utilsModule/ref.h"

namespace IKIGAI::ECS {
class Object;
}

namespace IKIGAI::ECS {

class TransformNotifier {
public:
	enum class Notification { CHANGED, DESTROYED };

	using NotificationHandler = std::function<void(Notification)>;
	using NotificationHandlerID = int;

	NotificationHandlerID addNotificationHandler(NotificationHandler notificationHandler);
	void notifyChildren(Notification notification);
	bool removeNotificationHandler(const NotificationHandlerID& notificationHandlerID);

private:
	std::unordered_map<NotificationHandlerID, NotificationHandler> notificationHandlers;
	NotificationHandlerID availableHandlerID = 0;
};

class Transform {
public:
	Transform(IKIGAI::MATH::Vector3f localPosition = IKIGAI::MATH::Vector3f(0.0f, 0.0f, 0.0f),
		IKIGAI::MATH::QuaternionF localRotation = IKIGAI::MATH::QuaternionF::Identity,
		IKIGAI::MATH::Vector3f localScale = IKIGAI::MATH::Vector3f(1.0f, 1.0f, 1.0f));

	~Transform();
	void notificationHandler(TransformNotifier::Notification notification);
	void setParent(Transform& parent);
	bool removeParent();
	bool hasParent() const;

	void generateMatrices(IKIGAI::MATH::Vector3f position, IKIGAI::MATH::QuaternionF rotation, IKIGAI::MATH::Vector3f scale);

	void updateWorldMatrix();
	void setLocalPosition(IKIGAI::MATH::Vector3f p_newPosition);
	void setLocalRotation(IKIGAI::MATH::QuaternionF p_newRotation);
	void setLocalScale(IKIGAI::MATH::Vector3f p_newScale);
	void setLocalAnchor(IKIGAI::MATH::Vector2f p_new);
	void setLocalPivot(IKIGAI::MATH::Vector2f p_new);
	void setLocalSize(IKIGAI::MATH::Vector2f p_new);
	void translateLocal(const IKIGAI::MATH::Vector3f& p_translation);
	void rotateLocal(const IKIGAI::MATH::QuaternionF& p_rotation);
	void scaleLocal(const IKIGAI::MATH::Vector3f& p_scale);
	const IKIGAI::MATH::Vector3f& getLocalPosition() const;
	const IKIGAI::MATH::QuaternionF& getLocalRotation() const;
	const IKIGAI::MATH::Vector3f& getLocalScale() const;
	const IKIGAI::MATH::Vector2f& getLocalAnchor() const;
	const IKIGAI::MATH::Vector2f& getLocalPivot() const;
	const IKIGAI::MATH::Vector2f& getLocalSize() const;
	const IKIGAI::MATH::Vector3f& getWorldPosition() const;
	const IKIGAI::MATH::QuaternionF& getWorldRotation() const;
	const IKIGAI::MATH::Vector3f& getWorldScale() const;
	const IKIGAI::MATH::Matrix4f& getLocalMatrix() const;
	const MATH::Matrix4f& getWorldMatrix() const;
	const MATH::Matrix4f& getPrevWorldMatrix() const;
	IKIGAI::MATH::Vector3f getWorldForward() const;
	IKIGAI::MATH::Vector3f getWorldUp() const;
	IKIGAI::MATH::Vector3f getWorldRight() const;
	IKIGAI::MATH::Vector3f getLocalForward() const;
	IKIGAI::MATH::Vector3f getLocalUp() const;
	IKIGAI::MATH::Vector3f getLocalRight() const;
	void setPrevWorldMatrix(MATH::Matrix4f m);

	void turnOnAnchorPivot();
	void turnOffAnchorPivot();
	bool isAnchorPivotMode() const;

	TransformNotifier notifier;
	TransformNotifier::NotificationHandlerID notificationHandlerID;

private:
	void preDecomposeWorldMatrix();
	void preDecomposeLocalMatrix();

	IKIGAI::MATH::Vector3f localPosition;
	IKIGAI::MATH::QuaternionF localRotation;
	IKIGAI::MATH::Vector3f localScale;
	IKIGAI::MATH::Vector3f worldPosition;
	IKIGAI::MATH::QuaternionF worldRotation;
	IKIGAI::MATH::Vector3f worldScale;
	MATH::Matrix4f localMatrix;
	MATH::Matrix4f worldMatrix;
	MATH::Matrix4f prevWorldMatrix;
	Transform* parent = nullptr;

	// for 2d
	bool use2d = false;
	MATH::Vector2f mAnchor;
	MATH::Vector2f mPivot;
	MATH::Vector2f mSize;
};

IKI_CLASS(Groups = [Component])
class TransformComponent : public ComponentBase {
	IKI_GENERATED_BODY(TransformComponent)
public:
	struct Descriptor : public ComponentBase::Descriptor {
		std::string Type;
		MATH::Vector3f LocalPosition;
		MATH::Vector3f LocalScale;
		MATH::Vector3f LocalRotation;

		template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
			using Self = Descriptor;
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Self::Type, "TransformComponentType")
				.field(&Self::LocalPosition, "LocalPosition")
				.field(&Self::LocalScale, "LocalScale")
				.field(&Self::LocalRotation, "LocalRotation");
		}
	};

	TransformComponent(UTILS::Ref<ECS::Object> obj, IKIGAI::MATH::Vector3f localPosition = IKIGAI::MATH::Vector3f(0.0f, 0.0f, 0.0f),
		MATH::QuaternionF localRotation = MATH::QuaternionF::Identity, IKIGAI::MATH::Vector3f localScale = IKIGAI::MATH::Vector3f(1.0f, 1.0f, 1.0f));

	TransformComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);
	TransformComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
		: TransformComponent(obj, static_cast<const Descriptor&>(descriptor)) { };

	[[nodiscard]] Descriptor getDescriptor() const;

	void setParent(TransformComponent& p_parent);
	bool removeParent();
	bool hasParent() const;
	void setLocalPosition(IKIGAI::MATH::Vector3f p_newPosition);

	void setLocalRotation(MATH::QuaternionF p_newRotation);

	// Publish position and rotation together, retaining the world matrix from before the pose change.
	void setLocalPose(MATH::Vector3f position, MATH::QuaternionF rotation);

	void setLocalScale(IKIGAI::MATH::Vector3f p_newScale);

	void translateLocal(const IKIGAI::MATH::Vector3f& p_translation);

	void rotateLocal(const MATH::QuaternionF& p_rotation);

	void scaleLocal(const IKIGAI::MATH::Vector3f& p_scale);
	const IKIGAI::MATH::Vector3f& getLocalPosition() const;

	const MATH::QuaternionF& getLocalRotation() const;

	const IKIGAI::MATH::Vector3f& getLocalScale() const;

	const IKIGAI::MATH::Vector3f& getWorldPosition() const;

	const MATH::QuaternionF& getWorldRotation() const;
	const IKIGAI::MATH::Vector3f& getWorldScale() const;

	const MATH::Matrix4f& getLocalMatrix() const;
	const MATH::Matrix4f& getWorldMatrix() const;
	Transform& getTransform();
	IKIGAI::MATH::Vector3f getWorldForward() const;
	IKIGAI::MATH::Vector3f getWorldUp() const;

	IKIGAI::MATH::Vector3f getWorldRight() const;
	IKIGAI::MATH::Vector3f getLocalForward() const;
	IKIGAI::MATH::Vector3f getLocalUp() const;

	IKIGAI::MATH::Vector3f getLocalRight() const;

	MATH::Matrix3f GetNormalMatrix(const MATH::Matrix4f& model) const;

	[[nodiscard]] MATH::Vector3f getLocalRotationDeg() const;
	void setLocalRotationDeg(MATH::Vector3f val);

private:
	[[nodiscard]] float getLocalScaleX() const;
	[[nodiscard]] float getLocalScaleY() const;
	[[nodiscard]] float getLocalScaleZ() const;
	[[nodiscard]] MATH::Vector3f getLocalScale_() const;

	void setLocalScaleX(float val);
	void setLocalScaleY(float val);
	void setLocalScaleZ(float val);
	void setLocalScale_(MATH::Vector3f val);

	[[nodiscard]] float getLocalPositionX() const;
	[[nodiscard]] float getLocalPositionY() const;
	[[nodiscard]] float getLocalPositionZ() const;
	[[nodiscard]] MATH::Vector3f getLocalPosition_() const;

	void setLocalPositionX(float val);
	void setLocalPositionY(float val);
	void setLocalPositionZ(float val);
	void setLocalPosition_(MATH::Vector3f val);

	// In degree
	[[nodiscard]] float getLocalRotationX() const;
	[[nodiscard]] float getLocalRotationY() const;
	[[nodiscard]] float getLocalRotationZ() const;

	void setLocalRotationX(float val);
	void setLocalRotationY(float val);
	void setLocalRotationZ(float val);

private:
	std::shared_ptr<Transform> transform;

public:
	IKI_PROPERTY(Name = LocalScale, Type = MATH::Vector3f, Getter = getLocalScale_, Setter = setLocalScale_,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange = MATH::Vector2f { 0.0f, 10000.0f }, EditStep = 0.1f, EditWidget = DRAG_FLOAT_3)
	IKI_PROPERTY(Name = LocalRotation, Type = MATH::Vector3f, Getter = getLocalRotationDeg, Setter = setLocalRotationDeg,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange = MATH::Vector2f { 0.0f, 10000.0f }, EditStep = 0.1f, EditWidget = DRAG_FLOAT_3)
	IKI_PROPERTY(Name = LocalPosition, Type = MATH::Vector3f, Getter = getLocalPosition_, Setter = setLocalPosition_,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange = MATH::Vector2f { 0.0f, 10000.0f }, EditStep = 0.1f, EditWidget = DRAG_FLOAT_3)

	static auto GetMembers() {
		return std::tuple { IKIGAI::UTILS::MakeMemberInfo("LocalScale", &TransformComponent::getLocalScale_, &TransformComponent::setLocalScale_,
								UTILS::Meta_t {
								{ UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
								{ UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f { 0.0f, 10000.0f } },
								{ UTILS::MetaParam::EDIT_STEP, 0.1f },
								{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT_3 },
								}),

			IKIGAI::UTILS::MakeMemberInfo("LocalRotation", &TransformComponent::getLocalRotationDeg, &TransformComponent::setLocalRotationDeg,
				UTILS::Meta_t {
				{ UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f { 0.0f, 10000.0f } },
				{ UTILS::MetaParam::EDIT_STEP, 0.1f },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT_3 },
				}),

			IKIGAI::UTILS::MakeMemberInfo("LocalPosition", &TransformComponent::getLocalPosition_, &TransformComponent::setLocalPosition_,
				UTILS::Meta_t {
				{ UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f { 0.0f, 10000.0f } },
				{ UTILS::MetaParam::EDIT_STEP, 0.1f },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT_3 },
				}) };
	}
};

template <> inline std::string IKIGAI::ECS::GetType<TransformComponent>() {
	return "class IKIGAI::ECS::TransformComponent";
}

template <> inline std::string IKIGAI::ECS::GetComponentName<TransformComponent>() {
	return "TransformComponent";
}
}

#include "generated/transform.generated.h"
