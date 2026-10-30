#pragma once
#if USE_PHYSICS_3D
#include "component.h"
#include "coreModule/ecs/componentReference.h"
#include "resourceModule/ragdollManager.h"
#include "skeletalModule/ragdollInstance.h"

namespace IKIGAI::ECS {
enum class RagdollState { Animated, Simulated };
class RagdollComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		std::string Definition {};
		ComponentReference Motor {};
		bool StartSimulated = false;
		template <class C> static auto serde(C& c, Descriptor& v) {
			using namespace serde::attribute;
			serde::serde_struct(c, v)
				.field(&Descriptor::Type, "RagdollComponentType")
				.field(&Descriptor::Definition, "Definition", default_ { std::string {} })
				.field(&Descriptor::Motor, "Motor", default_ { ComponentReference {} })
				.field(&Descriptor::StartSimulated, "StartSimulated", default_ { false });
		}
	};
	explicit RagdollComponent(UTILS::Ref<Object>);
	RagdollComponent(UTILS::Ref<Object>, const ComponentBase::Descriptor&);
	~RagdollComponent() override;
	Descriptor getDescriptor() const;
	static auto GetMembers() {
		return std::tuple {};
	}
	void activate();
	void reset();
	void applyImpulse(std::size_t segment, MATH::Vector3f impulse, MATH::Vector3f worldPoint);
	RagdollState state() const;
	const std::string& lastError() const;
	void onDisable() override;
	void onDestroy() override;
	Descriptor settings {};

private:
	friend class RagdollSystem;
	struct PendingImpulse {
		std::size_t segment = 0;
		MATH::Vector3f impulse {};
		MATH::Vector3f point {};
	};
	void release();
	std::shared_ptr<RESOURCES::RagdollResource> mResource {};
	std::shared_ptr<const SKELETON::RagdollDefinition> mDefinition {};
	std::string mLastError {};
	std::shared_ptr<SKELETON::RagdollInstance> mInstance {};
	std::string mLoadedPath {};
	std::uint64_t mRevision = 0;
	bool mActivateRequested = false;
	bool mResetRequested = false;
	bool mStarted = false;
	std::vector<PendingImpulse> mImpulses {};
	SKELETON::Pose mPreviousPose {};
	SKELETON::Pose mCurrentPose {};
	SKELETON::RagdollFrame mPreviousRoot {};
	SKELETON::RagdollFrame mCurrentRoot {};
	std::uint32_t mSamples = 0;
	MATH::Vector3f mSavedLocalPosition {};
	MATH::QuaternionF mSavedLocalRotation {};
	UTILS::WeakPtr<ComponentBase> mMotor {};
	MATH::Vector3f mSavedMotorPosition {};
	float mFixedDt = 1.f / 60;
	float mRemainder = 0;
};
template <> inline std::string GetType<RagdollComponent>() {
	return "class IKIGAI::ECS::RagdollComponent";
}
template <> inline std::string GetComponentName<RagdollComponent>() {
	return "RagdollComponent";
}
}
#endif
