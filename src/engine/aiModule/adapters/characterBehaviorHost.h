#pragma once
#include "aiModule/behaviorTree/behaviorTree.h"
#include "coreModule/ecs/componentReference.h"

namespace IKIGAI::AI {
class CharacterBehaviorHost final : public ExecutionHost {
public:
	explicit CharacterBehaviorHost(std::weak_ptr<ECS::Object>);
	~CharacterBehaviorHost() override;
	void bind(SCENE_SYSTEM::Scene&, const ECS::ComponentReference&);
	void apply();
	void clear();
	std::optional<MATH::Vector3f> position(const EntityRef&) const override;
	std::optional<MATH::Vector3f> ownerPosition() const override;
	std::optional<float> ownerHeading() const override;
	bool move(const TaskToken&, MATH::Vector3f, std::optional<float>) override;
	void release(const TaskToken&) override;

private:
	std::weak_ptr<ECS::Object> mOwner {};
	UTILS::WeakPtr<ECS::ComponentBase> mMotor;
	TaskToken mToken {};
	MATH::Vector3f mVelocity {};
	std::optional<float> mHeading {};
	bool mQueued = false;
	bool mControlling = false;
};
}
