#include "destructibleMeshComponent.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/object.h"
#include "modelRenderer.h"
#include "sceneModule/scene.h"
#include <algorithm>
#include <utility>
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
DestructibleMeshComponent::DestructibleMeshComponent(DestructibleMeshComponent&& source) noexcept
	: ComponentBase(std::move(source)) {
	moveState(source);
}
DestructibleMeshComponent& DestructibleMeshComponent::operator=(DestructibleMeshComponent&& source) noexcept {
	if (this != &source) {
		release();
		ComponentBase::operator=(std::move(source));
		moveState(source);
	}
	return *this;
}
void DestructibleMeshComponent::moveState(DestructibleMeshComponent& source) {
	settings = std::move(source.settings);
	mScene = std::exchange(source.mScene, nullptr);
	mSceneLifetime = std::move(source.mSceneLifetime);
	mResource = std::move(source.mResource);
	mInstance = std::move(source.mInstance);
	mMaterials = std::move(source.mMaterials);
	mRenderPieces = std::move(source.mRenderPieces);
	mCandidates = std::move(source.mCandidates);
	mRequests = std::move(source.mRequests);
	mAnchorRequests = std::move(source.mAnchorRequests);
	mActive = std::exchange(source.mActive, std::nullopt);
	mJob = std::move(source.mJob);
	mRuntime = std::move(source.mRuntime);
	mResults = std::move(source.mResults);
	mAnchorResults = std::move(source.mAnchorResults);
	mNextRequestId = source.mNextRequestId;
	mGeneration = source.mGeneration;
	mUploadPiece = source.mUploadPiece;
	mUploadBatch = source.mUploadBatch;
	mTopologyBytes = std::exchange(source.mTopologyBytes, 0);
	mReservedGpuBytes = std::exchange(source.mReservedGpuBytes, 0);
	mFailed = source.mFailed;
	mPath = std::move(source.mPath);
	mError = std::move(source.mError);
	mStatus = source.mStatus;
	mReset = std::exchange(source.mReset, false);
	mDamage = std::exchange(source.mDamage, 0.f);
	mImpactCooldown = std::exchange(source.mImpactCooldown, 0.f);
}
DestructibleMeshComponent::~DestructibleMeshComponent() {
	release();
}
DestructibleMeshComponent::Descriptor DestructibleMeshComponent::getDescriptor() const {
	return settings;
}
std::uint64_t DestructibleMeshComponent::slice(MATH::Vector3f point, MATH::Vector3f normal) {
	return slicePiece(0, point, normal);
}
std::uint64_t DestructibleMeshComponent::slicePiece(
	std::uint64_t piece, MATH::Vector3f point, MATH::Vector3f normal, MATH::Vector3f impulse, DESTRUCTION::StaticCutMode mode) {
	if (!PHYSICS_COMMON::finite(point) || !PHYSICS_COMMON::finite(normal) || normal.squareMagnitude() < 1e-12f || !PHYSICS_COMMON::finite(impulse)
		|| (mode != DESTRUCTION::StaticCutMode::BothDynamic && mode != DESTRUCTION::StaticCutMode::KeepNegativeStatic
			&& mode != DESTRUCTION::StaticCutMode::KeepPositiveStatic)) {
		throw std::invalid_argument("Slice requires finite point, nonzero normal, finite impulse and valid static mode");
	}
	return enqueue({ piece, point, normal, impulse, mode });
}
std::uint64_t DestructibleMeshComponent::cutoutBox(
	std::uint64_t piece, MATH::Vector3f center, MATH::Vector3f halfExtents, MATH::QuaternionF orientation) {
	const DESTRUCTION::BoxCutout box { halfExtents, orientation };
	if (!piece || !PHYSICS_COMMON::finite(center) || !DESTRUCTION::validBoxCutout(box)) {
		throw std::invalid_argument("Box cutout requires a piece ID, finite center, bounded half extents and unit orientation");
	}
	DESTRUCTION::SliceRequest request {};
	request.piece = piece;
	request.point = center;
	request.cutout = box;
	return enqueue(request);
}
std::uint64_t DestructibleMeshComponent::setSupportAnchorEnabled(DESTRUCTION::SupportId anchor, bool enabled) {
	DESTRUCTION::SupportAnchorResult request {};
	request.id = mNextRequestId++;
	request.anchor = anchor;
	request.enabled = enabled;
	const auto id = request.id;
	const auto limits = mScene && !mSceneLifetime.expired() ? mScene->destructionSettings : DESTRUCTION::RuntimeSettings {};
	if (!anchor) {
		request.error = "Support anchor ID must be nonzero";
	}
	else if (mReset) {
		request.error = "Destructible reset is pending";
	}
	else if (mFailed && mPath == settings.Definition) {
		request.error = mError;
	}
	else if (settings.Definition.empty()) {
		request.error = "Configure destructible Definition before changing support anchors";
	}
	else if (pendingRequests() >= limits.MaximumPendingPerObject) {
		request.error = "Destructible request queue is full";
	}
	if (request.error.empty()) {
		mAnchorRequests.push_back(std::move(request));
	}
	else {
		request.status = DESTRUCTION::SupportAnchorStatus::Rejected;
		completeSupportAnchor(std::move(request));
	}
	return id;
}
std::optional<DESTRUCTION::SupportAnchorResult> DestructibleMeshComponent::supportAnchorResult(std::uint64_t id) const {
	const auto matches = [id](const auto& result) { return result.id == id; };
	const auto completed = std::find_if(mAnchorResults.begin(), mAnchorResults.end(), matches);
	if (completed != mAnchorResults.end()) {
		return *completed;
	}
	const auto queued = std::find_if(mAnchorRequests.begin(), mAnchorRequests.end(), matches);
	return queued == mAnchorRequests.end() ? std::nullopt : std::optional { *queued };
}
void DestructibleMeshComponent::completeSupportAnchor(DESTRUCTION::SupportAnchorResult result) {
	const auto maximum = mScene && !mSceneLifetime.expired() ? std::max(1u, mScene->destructionSettings.MaximumResults) : 64;
	while (mAnchorResults.size() >= maximum) {
		mAnchorResults.pop_front();
	}
	mAnchorResults.push_back(std::move(result));
}
std::uint64_t DestructibleMeshComponent::enqueue(DESTRUCTION::SliceRequest request) {
	PendingRequest pending {};
	pending.id = mNextRequestId++;
	pending.generation = mGeneration;
	pending.slice = request;
	pending.created = std::chrono::steady_clock::now();
	const auto limits = mScene && !mSceneLifetime.expired() ? mScene->destructionSettings : DESTRUCTION::RuntimeSettings {};
	if (mReset) {
		complete(pending, DESTRUCTION::SliceStatus::Busy, "Destructible reset is pending");
		return pending.id;
	}
	if (mFailed && mPath == settings.Definition) {
		complete(pending, DESTRUCTION::SliceStatus::UnsupportedMesh, mError);
		return pending.id;
	}
	if (settings.Definition.empty()) {
		complete(pending, DESTRUCTION::SliceStatus::UnsupportedMesh, "Configure destructible Definition before cutting");
		return pending.id;
	}
	if (pendingRequests() >= limits.MaximumPendingPerObject) {
		complete(pending, DESTRUCTION::SliceStatus::Busy, "Destructible request queue is full");
		return pending.id;
	}
	if (mInstance) {
		const auto status = mInstance->bind(pending.slice);
		if (status != DESTRUCTION::SliceStatus::Split) {
			complete(pending, status,
				request.cutout ? "Cutout requires a live piece of a Concave destructible" : "No live intersected piece for the requested plane");
			return pending.id;
		}
	}
	else {
		if (request.piece) {
			complete(pending, DESTRUCTION::SliceStatus::Stale, "Piece does not exist");
			return pending.id;
		}
		// Before initialization, capture in the root's material frame. Definition COM is resolved on initialization.
		auto inverse = MATH::QuaternionF::Conjugate(obj->tryGetTransform()->getWorldRotation());
		pending.slice.point = inverse * (request.point - obj->tryGetTransform()->getWorldPosition());
		pending.slice.normal = inverse * request.normal;
		pending.rootSpace = true;
	}
	mRequests.push_back(pending);
	return pending.id;
}
std::uint64_t DestructibleMeshComponent::applyDamage(float amount, MATH::Vector3f point, MATH::Vector3f normal) {
	return applyDamagePiece(0, amount, point, normal);
}
std::uint64_t DestructibleMeshComponent::applyDamagePiece(std::uint64_t piece, float amount, MATH::Vector3f point, MATH::Vector3f normal) {
	if (!std::isfinite(amount) || amount < 0 || !PHYSICS_COMMON::finite(point) || !PHYSICS_COMMON::finite(normal)
		|| normal.squareMagnitude() < 1e-12f) {
		throw std::invalid_argument("Invalid destructible damage");
	}
	if (!mInstance && settings.Definition.empty()) {
		throw std::invalid_argument("Configure destructible Definition before applying damage");
	}
	if (!mInstance) {
		mResource = RESOURCES::ServiceManager::Get<RESOURCES::DestructibleLoader>().loadResource(settings.Definition);
	}
	const float threshold = mInstance ? mInstance->definition().settings.DamageThreshold : mResource->definition->settings.DamageThreshold;
	mDamage = std::min(mDamage + amount, 1e9f);
	if (mDamage < threshold) {
		return 0;
	}
	const auto id = slicePiece(piece, point, normal);
	auto pending = std::find_if(mRequests.begin(), mRequests.end(), [&](const auto& request) { return request.id == id; });
	if (pending != mRequests.end()) {
		pending->reservedDamage = threshold;
		mDamage -= threshold;
	}
	return id;
}
void DestructibleMeshComponent::complete(const PendingRequest& pending, DESTRUCTION::SliceStatus status, std::string error) {
	DESTRUCTION::RequestResult result {};
	result.id = pending.id;
	result.piece = pending.slice.piece;
	result.generation = pending.generation;
	result.stage = DESTRUCTION::RequestStage::Completed;
	result.status = status;
	result.error = std::move(error);
	result.latencyMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pending.created).count();
	if (mJob && mActive && mActive->id == pending.id && mJob->done.load(std::memory_order_acquire)) {
		result.slicingMilliseconds = mJob->result.slicingMilliseconds;
		result.cookingMilliseconds = mJob->result.cookingMilliseconds;
		mJob->stage.store(DESTRUCTION::RequestStage::Completed);
	}
	if (status != DESTRUCTION::SliceStatus::Split) {
		mDamage = std::min(mDamage + pending.reservedDamage, 1e9f);
	}
	mStatus = status;
	mError = result.error;
	const auto maximum = mScene && !mSceneLifetime.expired() ? std::max(1u, mScene->destructionSettings.MaximumResults) : 64;
	while (mResults.size() >= maximum) {
		mResults.pop_front();
	}
	mResults.push_back(std::move(result));
}
void DestructibleMeshComponent::cancelRequests() {
	if (mJob) {
		mJob->cancelled.store(true);
	}
	for (const auto& pending : mRequests) {
		complete(pending, DESTRUCTION::SliceStatus::Cancelled, "Destructible lifecycle changed");
	}
	mRequests.clear();
	for (auto& request : mAnchorRequests) {
		request.status = DESTRUCTION::SupportAnchorStatus::Cancelled;
		request.error = "Destructible lifecycle changed";
		completeSupportAnchor(std::move(request));
	}
	mAnchorRequests.clear();
	if (mActive) {
		complete(*mActive, DESTRUCTION::SliceStatus::Cancelled, "Destructible lifecycle changed");
	}
	mActive.reset();
	mJob.reset();
	++mGeneration;
}
void DestructibleMeshComponent::reset() {
	cancelRequests();
	mDamage = 0;
	mReset = true;
	mFailed = false;
}
void DestructibleMeshComponent::discardCandidates() {
	const auto runtime = mRuntime.lock();
	if (runtime) {
		runtime->statistics.gpuReservedBytes -= mReservedGpuBytes;
	}
	mReservedGpuBytes = 0;
	for (auto& candidate : mCandidates) {
		if (runtime) {
			runtime->statistics.gpuBytes -= candidate.gpuBytes;
		}
		// Candidates are inactive and have never entered the BVH.
		candidate.object->dispose();
	}
	mCandidates.clear();
}
std::optional<DESTRUCTION::RequestResult> DestructibleMeshComponent::requestResult(std::uint64_t id) const {
	const auto found = std::find_if(mResults.begin(), mResults.end(), [&](const auto& result) { return result.id == id; });
	if (found != mResults.end()) {
		return *found;
	}
	const PendingRequest* pending = nullptr;
	for (const auto& request : mRequests) {
		if (request.id == id) {
			pending = &request;
			break;
		}
	}
	if (mActive && mActive->id == id) {
		pending = &*mActive;
	}
	if (!pending) {
		return {};
	}
	DESTRUCTION::RequestResult result {};
	result.id = id;
	result.piece = pending->slice.piece;
	result.generation = pending->generation;
	result.stage = mActive && mActive->id == id && mJob ? mJob->stage.load() : DESTRUCTION::RequestStage::Queued;
	result.latencyMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pending->created).count();
	return result;
}
const std::deque<DESTRUCTION::RequestResult>& DestructibleMeshComponent::completedRequests() const {
	return mResults;
}
std::size_t DestructibleMeshComponent::pendingRequests() const {
	return mRequests.size() + mAnchorRequests.size() + (mActive ? 1 : 0);
}
float DestructibleMeshComponent::accumulatedDamage() const {
	return mDamage;
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
void DestructibleMeshComponent::release(bool clearDamage) {
	if (!mRenderPieces.empty() && mScene && !mSceneLifetime.expired()) {
		++mScene->renderTopologyRevision;
	}
	cancelRequests();
	if (auto runtime = mRuntime.lock()) {
		runtime->statistics.topologyBytes -= mTopologyBytes;
	}
	mTopologyBytes = 0;
	const auto disposeRender = [&](auto& pieces) {
		for (auto& piece : pieces) {
			if (auto runtime = mRuntime.lock()) {
				runtime->statistics.gpuBytes -= piece.gpuBytes;
			}
			if (mScene && !mSceneLifetime.expired()) {
				if (auto renderer = piece.object->getComponent<ModelRenderer>()) {
					mScene->removeFromBVH(renderer.get());
				}
			}
			piece.object->dispose();
		}
		pieces.clear();
	};
	discardCandidates();
	disposeRender(mRenderPieces);
	mRenderPieces.clear();
	mInstance.reset();
	mRequests.clear();
	mMaterials.clear();
	mResource.reset();
	if (clearDamage) {
		mDamage = 0;
	}
	mReset = false;
	mImpactCooldown = 0;
}
void DestructibleMeshComponent::onDisable() {
	release();
}
void DestructibleMeshComponent::onDestroy() {
	release();
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::DestructibleMeshComponent>();
	return true;
}();
}

#endif
