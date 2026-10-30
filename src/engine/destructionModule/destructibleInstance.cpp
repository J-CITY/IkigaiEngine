#include "destructibleInstance.h"
#include <algorithm>
#include <stdexcept>
namespace IKIGAI::DESTRUCTION {
using Vec3 = MATH::Vector3f;
namespace {
	std::shared_ptr<const COLLISION::ConvexPolyhedron> centerMesh(FractureMesh& mesh, const SliceSettings& settings, Vec3& center) {
		auto hull = validateMesh(mesh, settings);
		center = hull->center;
		for (auto& vertex : mesh.vertices) {
			vertex.position -= center;
		}
		return validateMesh(mesh, settings);
	}
}
DestructibleInstance::DestructibleInstance(std::shared_ptr<const DestructibleDefinition> definition)
	: mDefinition(std::move(definition)) {
	if (!mDefinition) {
		throw std::invalid_argument("Missing destructible definition");
	}
}
DestructibleInstance::~DestructibleInstance() {
	clear();
}
void DestructibleInstance::clear() {
	if (auto world = mWorld.lock()) {
		for (const auto& piece : mPieces) {
			world->destroyBody(piece.body);
		}
	}
	mPieces.clear();
}
void DestructibleInstance::initialize(std::shared_ptr<PHYSICS::World> world, Vec3 position, MATH::QuaternionF rotation) {
	clear();
	if (!world) {
		throw std::invalid_argument("Missing destructible world");
	}
	if (world->bodies().size() >= 2048) {
		throw std::runtime_error("Destructible world body budget exceeded");
	}
	mWorld = world;
	DestructiblePiece piece {};
	piece.mesh = mDefinition->mesh;
	piece.id = mNextId++;
	piece.mass = mDefinition->settings.Mass;
	Vec3 center {};
	auto hull = centerMesh(piece.mesh, mDefinition->slicing, center);
	PHYSICS::BodyDef body {};
	body.position = position + rotation * center;
	body.orientation = rotation;
	body.mass = piece.mass;
	body.type = mDefinition->settings.Static ? PHYSICS::BodyType::Static : PHYSICS::BodyType::Dynamic;
	body.shape = PHYSICS::Shape::convex(hull);
	body.friction = mDefinition->settings.Friction;
	body.restitution = mDefinition->settings.Restitution;
	mPieces.reserve(mDefinition->settings.MaximumPieces);
	piece.body = world->createBody(body);
	mPieces.push_back(std::move(piece));
}
SliceStatus DestructibleInstance::slice(const SliceRequest& request, const std::function<void(std::span<const DestructiblePiece>)>& prepareRender) {
	mError.clear();
	auto world = mWorld.lock();
	if (!world) {
		return SliceStatus::Cancelled;
	}
	auto found = std::find_if(mPieces.begin(), mPieces.end(), [&](const auto& piece) { return piece.id == request.piece; });
	if (found == mPieces.end() || !world->body(found->body)) {
		return SliceStatus::Stale;
	}
	if (!PHYSICS_COMMON::finite(request.impulse) || !PHYSICS_COMMON::finite(request.point) || !PHYSICS_COMMON::finite(request.normal)
		|| request.normal.squareMagnitude() < 1e-12f
		|| (request.mode != StaticCutMode::BothDynamic && request.mode != StaticCutMode::KeepNegativeStatic
			&& request.mode != StaticCutMode::KeepPositiveStatic)) {
		mError = "Invalid slice request";
		return SliceStatus::UnsupportedMesh;
	}
	if (mPieces.size() >= mDefinition->settings.MaximumPieces || found->depth >= mDefinition->settings.MaximumDepth
		|| world->bodies().size() >= 2048) {
		return SliceStatus::BudgetExceeded;
	}
	const auto source = *world->body(found->body);
	auto inverse = MATH::QuaternionF::Conjugate(source.getOrientation());
	auto cut = sliceMesh(found->mesh, inverse * (request.point - source.getPosition()), inverse * request.normal, mDefinition->slicing);
	mError = cut.error;
	if (cut.status != SliceStatus::Split) {
		return cut.status;
	}
	std::array<DestructiblePiece, 2> candidates {};
	try {
		const double sourceVolume = source.shape.polyhedron->volume;
		for (int side = 0; side < 2; ++side) {
			auto& piece = candidates[side];
			piece.id = mNextId++;
			piece.depth = found->depth + 1;
			piece.mesh = side == 0 ? std::move(cut.negative) : std::move(cut.positive);
			Vec3 center {};
			auto hull = centerMesh(piece.mesh, mDefinition->slicing, center);
			piece.mass = found->mass * static_cast<float>(hull->volume / sourceVolume);
			PHYSICS::BodyDef body {};
			body.shape = PHYSICS::Shape::convex(hull);
			body.orientation = source.getOrientation();
			body.position = source.getPosition() + body.orientation * center;
			body.velocity = source.getVelocity() + source.getRotation().cross(body.position - source.getPosition());
			body.angularVelocity = source.getRotation();
			body.mass = piece.mass;
			body.friction = source.friction;
			body.restitution = source.restitution;
			body.gravityScale = source.gravityScale;
			body.layer = source.layer;
			body.mask = source.mask;
			if (source.type == PHYSICS::BodyType::Static
				&& ((side == 0 && request.mode == StaticCutMode::KeepNegativeStatic)
					|| (side == 1 && request.mode == StaticCutMode::KeepPositiveStatic))) {
				body.type = PHYSICS::BodyType::Static;
			}
			piece.body = world->createBody(body);
			world->applyImpulse(piece.body, request.impulse * (piece.mass / found->mass), request.point);
		}
		if (prepareRender) {
			prepareRender(candidates);
		}
	} catch (const std::exception& error) {
		for (const auto& piece : candidates) {
			if (piece.body) {
				world->destroyBody(piece.body);
			}
		}
		mError = error.what();
		return SliceStatus::UnsupportedMesh;
	}
	world->destroyBody(found->body);
	*found = std::move(candidates[0]);
	mPieces.push_back(std::move(candidates[1]));
	return SliceStatus::Split;
}
void DestructibleInstance::advance(float dt) {
	auto world = mWorld.lock();
	if (!world) {
		mPieces.clear();
		return;
	}
	for (auto& piece : mPieces) {
		piece.age += dt;
		if (piece.depth && mDefinition->settings.Lifetime > 0 && piece.age >= mDefinition->settings.Lifetime) {
			world->destroyBody(piece.body);
		}
	}
	std::erase_if(mPieces, [&](const auto& piece) { return !world->body(piece.body); });
}
bool DestructibleInstance::valid() const {
	auto world = mWorld.lock();
	return world && std::all_of(mPieces.begin(), mPieces.end(), [&](const auto& piece) { return world->body(piece.body) != nullptr; });
}
const std::vector<DestructiblePiece>& DestructibleInstance::pieces() const {
	return mPieces;
}
const DestructibleDefinition& DestructibleInstance::definition() const {
	return *mDefinition;
}
const std::string& DestructibleInstance::lastError() const {
	return mError;
}
}
