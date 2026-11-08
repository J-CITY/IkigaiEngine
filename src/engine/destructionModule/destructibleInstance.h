#pragma once
#include "destructibleDefinition.h"
#include "fracturePreparation.h"
#include "supportAnchors.h"
#include "supportGraph.h"
#include <functional>
namespace IKIGAI::DESTRUCTION {
enum class StaticCutMode { BothDynamic, KeepNegativeStatic, KeepPositiveStatic };
enum class SupportAnchorStatus { Queued, Applied, Unchanged, Rejected, Cancelled };
struct SliceRequest {
	std::uint64_t piece = 0;
	MATH::Vector3f point {};
	MATH::Vector3f normal { 1, 0, 0 };
	MATH::Vector3f impulse {};
	StaticCutMode mode = StaticCutMode::BothDynamic;
	bool localSpace = false;
	std::optional<BoxCutout> cutout {};
};
struct DestructiblePiece {
	std::uint64_t id = 0;
	PHYSICS::Id body = 0;
	FractureMesh mesh {};
	unsigned depth = 0;
	float mass = 0;
	float age = 0;
	double volume = 0;
	std::size_t topologyBytes = 0;
	bool lifetimeLimited = false;
	std::optional<MATH::Matrix3f> compoundInertiaPerUnitMass {};
};
class DestructibleInstance {
public:
	explicit DestructibleInstance(std::shared_ptr<const DestructibleDefinition> definition);
	~DestructibleInstance();
	DestructibleInstance(const DestructibleInstance&) = delete;
	DestructibleInstance& operator=(const DestructibleInstance&) = delete;
	void initialize(std::shared_ptr<PHYSICS::World> world, MATH::Vector3f position, MATH::QuaternionF rotation, unsigned maximumBodies = 2048);
	void clear();
	SliceStatus slice(const SliceRequest& request, const std::function<void(std::span<const DestructiblePiece>)>& prepareRender = {});
	SliceStatus bind(SliceRequest& request) const;
	FractureSnapshot snapshot(const SliceRequest& localRequest) const;
	SliceStatus commit(PreparedFracture prepared, const SliceRequest& localRequest,
		const std::function<void(std::span<const DestructiblePiece>)>& prepareRender = {}, unsigned maximumBodies = 2048);
	// Call at a physics safe point. Detachment preserves piece IDs and may replace body handles.
	SupportAnchorStatus setSupportAnchorEnabled(SupportId anchor, bool enabled);
	std::span<const AnchorVolume> supportAnchors() const;
	void advance(float dt);
	bool valid() const;
	const std::vector<DestructiblePiece>& pieces() const;
	const DestructibleDefinition& definition() const;
	const std::string& lastError() const;

private:
	std::shared_ptr<const DestructibleDefinition> mDefinition {};
	std::weak_ptr<PHYSICS::World> mWorld {};
	std::vector<DestructiblePiece> mPieces {};
	std::vector<AnchorVolume> mSupportAnchors {};
	std::uint64_t mRevision = 0;
	std::string mError {};
};
}
