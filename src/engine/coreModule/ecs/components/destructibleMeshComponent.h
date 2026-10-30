#pragma once
#if USE_PHYSICS_3D
#include "component.h"
#include "destructionModule/destructibleInstance.h"
#include "resourceModule/destructibleManager.h"
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
	void slice(MATH::Vector3f point, MATH::Vector3f normal);
	void slicePiece(std::uint64_t piece, MATH::Vector3f point, MATH::Vector3f normal, MATH::Vector3f impulse = {},
		DESTRUCTION::StaticCutMode mode = DESTRUCTION::StaticCutMode::BothDynamic);
	void applyDamage(float amount, MATH::Vector3f point, MATH::Vector3f cuttingNormal);
	void reset();
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
	};
	void release();
	SCENE_SYSTEM::Scene* mScene = nullptr;
	std::weak_ptr<void> mSceneLifetime {};
	std::shared_ptr<RESOURCES::DestructibleResource> mResource {};
	std::shared_ptr<DESTRUCTION::DestructibleInstance> mInstance {};
	std::vector<std::shared_ptr<RENDER::MaterialInterface>> mMaterials {};
	std::vector<RenderPiece> mRenderPieces {};
	std::vector<DESTRUCTION::SliceRequest> mRequests {};
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
