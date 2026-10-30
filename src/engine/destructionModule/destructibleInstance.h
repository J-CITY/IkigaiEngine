#pragma once
#include "destructibleDefinition.h"
#include <functional>
namespace IKIGAI::DESTRUCTION {
enum class StaticCutMode { BothDynamic, KeepNegativeStatic, KeepPositiveStatic };
struct SliceRequest {
	std::uint64_t piece = 0;
	MATH::Vector3f point {};
	MATH::Vector3f normal { 1, 0, 0 };
	MATH::Vector3f impulse {};
	StaticCutMode mode = StaticCutMode::BothDynamic;
};
struct DestructiblePiece {
	std::uint64_t id = 0;
	PHYSICS::Id body = 0;
	FractureMesh mesh {};
	unsigned depth = 0;
	float mass = 0;
	float age = 0;
};
class DestructibleInstance {
public:
	explicit DestructibleInstance(std::shared_ptr<const DestructibleDefinition> definition);
	~DestructibleInstance();
	DestructibleInstance(const DestructibleInstance&) = delete;
	DestructibleInstance& operator=(const DestructibleInstance&) = delete;
	void initialize(std::shared_ptr<PHYSICS::World> world, MATH::Vector3f position, MATH::QuaternionF rotation);
	void clear();
	SliceStatus slice(const SliceRequest& request, const std::function<void(std::span<const DestructiblePiece>)>& prepareRender = {});
	void advance(float dt);
	bool valid() const;
	const std::vector<DestructiblePiece>& pieces() const;
	const DestructibleDefinition& definition() const;
	const std::string& lastError() const;

private:
	std::shared_ptr<const DestructibleDefinition> mDefinition {};
	std::weak_ptr<PHYSICS::World> mWorld {};
	std::vector<DestructiblePiece> mPieces {};
	std::uint64_t mNextId = 1;
	std::string mError {};
};
}
