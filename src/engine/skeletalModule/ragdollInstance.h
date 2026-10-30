#pragma once
#if USE_PHYSICS_3D
#include "iAnimationPlayable.h"
#include "ragdollDefinition.h"

namespace IKIGAI::SKELETON {
struct RagdollFrame {
	MATH::Vector3f position {};
	MATH::QuaternionF rotation {};
};
// Per-character state; definitions are shared, simulation handles never are.
class RagdollInstance final : public IAnimationPlayable {
public:
	explicit RagdollInstance(std::shared_ptr<const RagdollDefinition>);
	~RagdollInstance() override;
	RagdollInstance(const RagdollInstance&) = delete;
	RagdollInstance& operator=(const RagdollInstance&) = delete;
	void activate(std::shared_ptr<PHYSICS::World>, const Pose&, RagdollFrame root, const Pose* previous = nullptr,
		const RagdollFrame* previousRoot = nullptr, float sampleDt = 0);
	void reset();
	bool simulated() const;
	void capture();
	void interpolate(float alpha);
	void applyImpulse(std::size_t segment, MATH::Vector3f impulse, MATH::Vector3f worldPoint);
	void update(float) override;
	Pose* getPose() override;
	std::unique_ptr<IAnimationPlayable> clone() const override;
	RagdollFrame root() const;
	RagdollFrame simulationRoot() const;
	float boundsRadius() const;
	const std::vector<PHYSICS::Id>& bodies() const;
	const RagdollDefinition& definition() const;
	static std::vector<RagdollFrame> modelFrames(const Skeleton&, const Pose&, float unitsToMeters);

private:
	std::shared_ptr<const RagdollDefinition> mDefinition {};
	std::weak_ptr<PHYSICS::World> mWorld {};
	std::vector<PHYSICS::Id> mBodies {};
	std::vector<PHYSICS::Id> mJoints {};
	std::vector<RagdollFrame> mPrevious {};
	std::vector<RagdollFrame> mCurrent {};
	Pose mReference {};
	Pose mOutput {};
	RagdollFrame mRoot {};
	RagdollFrame mSimulationRoot {};
	MATH::Vector3f mPelvisOffset {};
	float mBoundsRadius = 0;
};
}
#endif
