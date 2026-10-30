#include "destructibleMeshComponent.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/object.h"
#include "modelRenderer.h"
#include "sceneModule/scene.h"
namespace IKIGAI::ECS {
DestructibleMeshComponent::DestructibleMeshComponent(UTILS::Ref<Object> owner)
	: ComponentBase(owner) {
	__NAME__ = "DestructibleMeshComponent";
	settings.Type = GetType<DestructibleMeshComponent>();
}
DestructibleMeshComponent::DestructibleMeshComponent(UTILS::Ref<Object> owner, const ComponentBase::Descriptor& descriptor)
	: DestructibleMeshComponent(owner) {
	settings = static_cast<const Descriptor&>(descriptor);
}
DestructibleMeshComponent::DestructibleMeshComponent(DestructibleMeshComponent&&) noexcept = default;
DestructibleMeshComponent& DestructibleMeshComponent::operator=(DestructibleMeshComponent&&) noexcept = default;
DestructibleMeshComponent::~DestructibleMeshComponent() {
	release();
}
DestructibleMeshComponent::Descriptor DestructibleMeshComponent::getDescriptor() const {
	return settings;
}
void DestructibleMeshComponent::slice(MATH::Vector3f point, MATH::Vector3f normal) {
	slicePiece(0, point, normal);
}
void DestructibleMeshComponent::slicePiece(
	std::uint64_t piece, MATH::Vector3f point, MATH::Vector3f normal, MATH::Vector3f impulse, DESTRUCTION::StaticCutMode mode) {
	if (!PHYSICS_COMMON::finite(point) || !PHYSICS_COMMON::finite(normal) || normal.squareMagnitude() < 1e-12f || !PHYSICS_COMMON::finite(impulse)) {
		throw std::invalid_argument("Slice requires finite point, nonzero normal and finite impulse");
	}
	if (mRequests.size() >= 32) {
		mStatus = DESTRUCTION::SliceStatus::Busy;
		return;
	}
	mRequests.push_back({ piece, point, normal, impulse, mode });
}
void DestructibleMeshComponent::applyDamage(float amount, MATH::Vector3f point, MATH::Vector3f normal) {
	if (!std::isfinite(amount) || amount < 0 || !PHYSICS_COMMON::finite(point) || !PHYSICS_COMMON::finite(normal)
		|| normal.squareMagnitude() < 1e-12f) {
		throw std::invalid_argument("Invalid destructible damage");
	}
	mDamage = std::min(mDamage + amount, 1e9f);
	float threshold = 0;
	if (mInstance) {
		threshold = mInstance->definition().settings.DamageThreshold;
	}
	else {
		if (settings.Definition.empty()) {
			throw std::invalid_argument("Configure destructible Definition before applying damage");
		}
		mResource = RESOURCES::ServiceManager::Get<RESOURCES::DestructibleLoader>().loadResource(settings.Definition);
		threshold = mResource->definition->settings.DamageThreshold;
	}
	if (mDamage >= threshold) {
		slice(point, normal);
		mDamage = 0;
	}
}
void DestructibleMeshComponent::reset() {
	mReset = true;
	mRequests.clear();
}
DESTRUCTION::SliceStatus DestructibleMeshComponent::lastStatus() const {
	return mStatus;
}
const std::string& DestructibleMeshComponent::lastError() const {
	return mError;
}
const std::vector<DESTRUCTION::DestructiblePiece>& DestructibleMeshComponent::pieces() const {
	static const std::vector<DESTRUCTION::DestructiblePiece> empty {};
	return mInstance ? mInstance->pieces() : empty;
}
void DestructibleMeshComponent::release() {
	for (auto& piece : mRenderPieces) {
		if (mScene && !mSceneLifetime.expired()) {
			if (auto renderer = piece.object->getComponent<ModelRenderer>()) {
				mScene->removeFromBVH(renderer.get());
			}
		}
		piece.object->dispose();
	}
	mRenderPieces.clear();
	mInstance.reset();
	mRequests.clear();
	mMaterials.clear();
	mResource.reset();
	mDamage = 0;
	mReset = false;
}
void DestructibleMeshComponent::onDisable() {
	release();
}
void DestructibleMeshComponent::onDestroy() {
	release();
}
}
#endif
