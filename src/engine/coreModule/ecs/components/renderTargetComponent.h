#pragma once
#include "cameraComponent.h"
#include "component.h"
#include "mathModule/math.h"
#include "reflectionModule/reflection_macros.h"
#include "renderModule/backends/interface/frameBufferInterface.h"
#include "utilsModule/enum.h"

namespace IKIGAI::ECS {
class Object;
}

IKIGAI_ENUM_NS(IKIGAI::ECS, RenderFlowType, NODE, NODE_AND_CHILD)

namespace IKIGAI::ECS {
IKI_CLASS(Groups = [Component])
class RenderTargetComponent : public ComponentBase {
	IKI_GENERATED_BODY(RenderTargetComponent)
public:
	IKI_CLASS(Name = RenderTargetComponent::Descriptor)
	struct Descriptor : public ComponentBase::Descriptor {
		IKI_GENERATED_BODY(RenderTargetComponent::Descriptor)
		IKI_PROPERTY(SEREALIZE(name = "RenderTargetComponentType"))
		std::string Type = "class IKIGAI::ECS::RenderTargetComponent";
		IKI_PROPERTY(SEREALIZE(name = "FlowType"))
		IKIGAI::ECS::RenderFlowType flowType = IKIGAI::ECS::RenderFlowType::NODE;
		IKI_PROPERTY(SEREALIZE(name = "FrameBuffer"))
		std::string frameBufferName;
		IKI_PROPERTY(SEREALIZE(name = "CameraObjectId"))
		int cameraObjectId = 0;
	};
	RenderTargetComponent(UTILS::Ref<ECS::Object> obj);
	RenderTargetComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);
	RenderTargetComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
		: RenderTargetComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

	void setName(const std::string& name);
	const std::string& getTargetName() const;
	IKIGAI::ECS::RenderFlowType getType() const;
	std::shared_ptr<RENDER::FrameBufferInterface> getFrameBuffer();
	UTILS::WeakPtr<CameraComponent> getCamera();

	[[nodiscard]] Descriptor getDescriptor() const;

public:
	static auto GetMembers() {
		return std::tuple {};
	}

private:
	std::string mFrameBufferName;
	RenderFlowType mFlowType = RenderFlowType::NODE;
	ECS::Entity mCameraObjectId = ECS::Entity(ECS::Entity::ID(0));

	std::shared_ptr<RENDER::FrameBufferInterface> mFrameBuffer;
	UTILS::WeakPtr<CameraComponent> mCamera;
};

template <> inline std::string ECS::GetType<RenderTargetComponent>() {
	return "class IKIGAI::ECS::RenderTargetComponent";
}

template <> inline std::string IKIGAI::ECS::GetComponentName<RenderTargetComponent>() {
	return "RenderTargetComponent";
}
}

#include "generated/renderTargetComponent.generated.h"
