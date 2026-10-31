#pragma once
#if USE_PHYSICS_3D
#include "component.h"
#include "destructionModule/destructibleInstance.h"
#include "destructionModule/destructionRuntime.h"
#include "resourceModule/destructibleManager.h"
#include <optional>
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::ECS {
class DestructibleMeshComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		std::string Definition {};
		bool ImpactDamage = false;
		template <class C> static auto serde(C& c, Descriptor& v) {
			using namespace serde::attribute;
			serde::serde_struct(c, v)
				.field(&Descriptor::Type, "DestructibleMeshComponentType")
				.field(&Descriptor::Definition, "Definition", default_ { std::string {} })
				.field(&Descriptor::ImpactDamage, "ImpactDamage", default_ { false });
		}
	};
	explicit DestructibleMeshComponent(UTILS::Ref<Object> owner);
	DestructibleMeshComponent(UTILS::Ref<Object> owner, const ComponentBase::Descriptor& descriptor);
	DestructibleMeshComponent(DestructibleMeshComponent&&) noexcept;
	DestructibleMeshComponent& operator=(DestructibleMeshComponent&&) noexcept;
	~DestructibleMeshComponent() override;
	Descriptor getDescriptor() const;
	static auto GetMembers() {
		return std::tuple {};
	}
	std::uint64_t slice(MATH::Vector3f point, MATH::Vector3f normal);
	std::uint64_t slicePiece(std::uint64_t piece, MATH::Vector3f point, MATH::Vector3f normal, MATH::Vector3f impulse = {},
		DESTRUCTION::StaticCutMode mode = DESTRUCTION::StaticCutMode::BothDynamic);
	std::uint64_t applyDamage(float amount, MATH::Vector3f point, MATH::Vector3f cuttingNormal);
	std::uint64_t applyDamagePiece(std::uint64_t piece, float amount, MATH::Vector3f point, MATH::Vector3f cuttingNormal);
	void reset();
	std::optional<DESTRUCTION::RequestResult> requestResult(std::uint64_t id) const;
	const std::deque<DESTRUCTION::RequestResult>& completedRequests() const;
	std::size_t pendingRequests() const;
	float accumulatedDamage() const;
	DESTRUCTION::SliceStatus lastStatus() const;
	const std::string& lastError() const;
	const std::vector<DESTRUCTION::DestructiblePiece>& pieces() const;
	void onDisable() override;
	void onDestroy() override;
	Descriptor settings {};

private:
	friend class DestructionSystem;
	struct RenderPiece {
		std::uint64_t id = 0;
		std::shared_ptr<Object> object {};
		std::size_t gpuBytes = 0;
		MATH::Vector3f previousPosition {};
		MATH::Vector3f currentPosition {};
		MATH::QuaternionF previousRotation { 0, 0, 0, 1 };
		MATH::QuaternionF currentRotation { 0, 0, 0, 1 };
		bool poseReady = false;
	};
	struct PendingRequest {
		std::uint64_t id = 0;
		std::uint64_t generation = 0;
		DESTRUCTION::SliceRequest slice {};
		bool rootSpace = false;
		float reservedDamage = 0;
		std::chrono::steady_clock::time_point created {};
	};
	void complete(const PendingRequest& pending, DESTRUCTION::SliceStatus status, std::string error = {});
	void cancelRequests();
	void moveState(DestructibleMeshComponent& source);
	void release(bool clearDamage = true);
	SCENE_SYSTEM::Scene* mScene = nullptr;
	std::weak_ptr<void> mSceneLifetime {};
	std::shared_ptr<RESOURCES::DestructibleResource> mResource {};
	std::shared_ptr<DESTRUCTION::DestructibleInstance> mInstance {};
	std::vector<std::shared_ptr<RENDER::MaterialInterface>> mMaterials {};
	std::vector<RenderPiece> mRenderPieces {};
	std::vector<RenderPiece> mCandidates {};
	std::deque<PendingRequest> mRequests {};
	std::optional<PendingRequest> mActive {};
	std::shared_ptr<DESTRUCTION::FractureJob> mJob {};
	std::weak_ptr<DESTRUCTION::DestructionRuntime> mRuntime {};
	std::deque<DESTRUCTION::RequestResult> mResults {};
	std::uint64_t mNextRequestId = 1;
	std::uint64_t mGeneration = 1;
	std::size_t mUploadPiece = 0;
	std::size_t mUploadBatch = 0;
	std::size_t mTopologyBytes = 0;
	bool mFailed = false;
	std::string mPath {};
	std::string mError {};
	DESTRUCTION::SliceStatus mStatus = DESTRUCTION::SliceStatus::NoSplit;
	bool mReset = false;
	float mDamage = 0;
	float mImpactCooldown = 0;
};
template <> inline std::string GetType<DestructibleMeshComponent>() {
	return "class IKIGAI::ECS::DestructibleMeshComponent";
}
template <> inline std::string GetComponentName<DestructibleMeshComponent>() {
	return "DestructibleMeshComponent";
}
}
#endif
