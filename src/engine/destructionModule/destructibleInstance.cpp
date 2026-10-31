#include "destructibleInstance.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace IKIGAI::DESTRUCTION {
using Vec3 = MATH::Vector3f;
namespace {
	std::uint64_t nextPieceId() {
		// Components recreate instances on reset/reload. A retired handle must never alias a new root.
		static std::atomic<std::uint64_t> next { 1 };
		return next.fetch_add(1, std::memory_order_relaxed);
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
	++mRevision;
	if (auto world = mWorld.lock()) {
		for (const auto& piece : mPieces) {
			world->destroyBody(piece.body);
		}
	}
	mPieces.clear();
}
void DestructibleInstance::initialize(std::shared_ptr<PHYSICS::World> world, Vec3 position, MATH::QuaternionF rotation, unsigned maximumBodies) {
	clear();
	if (!world) {
		throw std::invalid_argument("Missing destructible world");
	}
	if (world->bodies().size() >= maximumBodies) {
		throw std::runtime_error("Destructible world body budget exceeded");
	}
	mWorld = world;
	DestructiblePiece piece {};
	piece.mesh = mDefinition->mesh;
	piece.id = mNextId = nextPieceId();
	piece.mass = mDefinition->settings.Mass;
	const auto center = mDefinition->center;
	auto hull = mDefinition->hull;
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
SliceStatus DestructibleInstance::bind(SliceRequest& request) const {
	auto world = mWorld.lock();
	if (!world) {
		return SliceStatus::Cancelled;
	}
	if (!PHYSICS_COMMON::finite(request.point) || !PHYSICS_COMMON::finite(request.normal) || !PHYSICS_COMMON::finite(request.impulse)
		|| request.normal.squareMagnitude() < 1e-12f
		|| (request.mode != StaticCutMode::BothDynamic && request.mode != StaticCutMode::KeepNegativeStatic
			&& request.mode != StaticCutMode::KeepPositiveStatic)) {
		return SliceStatus::UnsupportedMesh;
	}
	const DestructiblePiece* selected = nullptr;
	float nearest = std::numeric_limits<float>::max();
	for (const auto& piece : mPieces) {
		auto* body = world->body(piece.body);
		if (!body || (request.piece && request.piece != piece.id)) {
			continue;
		}
		if (request.piece) {
			selected = &piece;
			break;
		}
		const auto local = body->getPointInLocalSpace(request.point);
		const auto normal = body->getDirectionInLocalSpace(request.normal);
		float low = std::numeric_limits<float>::max();
		float high = -low;
		float distance = std::numeric_limits<float>::max();
		const auto& hull = *body->shape.polyhedron;
		for (auto vertex : hull.vertices) {
			const float projection = normal.dot(vertex - local);
			low = std::min(low, projection);
			high = std::max(high, projection);
		}
		if (low >= 0 || high <= 0) {
			continue;
		}
		bool inside = true;
		for (const auto& face : hull.faces) {
			inside = inside && face.normal.dot(local) <= face.offset;
		}
		if (inside) {
			distance = 0;
		}
		else {
			for (const auto& triangle : hull.triangles) {
				const auto point
					= COLLISION::closestTrianglePoint(local, hull.vertices[triangle[0]], hull.vertices[triangle[1]], hull.vertices[triangle[2]]);
				distance = std::min(distance, (point - local).squareMagnitude());
			}
		}
		if (distance < nearest || (distance == nearest && (!selected || piece.id < selected->id))) {
			selected = &piece;
			nearest = distance;
		}
	}
	if (!selected) {
		return request.piece ? SliceStatus::Stale : SliceStatus::NoSplit;
	}
	auto* body = world->body(selected->body);
	if (!request.localSpace) {
		request.point = body->getPointInLocalSpace(request.point);
		request.normal = body->getDirectionInLocalSpace(request.normal);
	}
	request.normal.normalise();
	request.piece = selected->id;
	request.localSpace = true;
	return SliceStatus::Split;
}
FractureSnapshot DestructibleInstance::snapshot(const SliceRequest& request) const {
	auto found = std::find_if(mPieces.begin(), mPieces.end(), [&](const auto& piece) { return piece.id == request.piece; });
	if (!request.localSpace || found == mPieces.end()) {
		throw std::invalid_argument("Slice requires a live bound piece");
	}
	return { found->id, mRevision, found->mesh, mDefinition->slicing, request.point, request.normal };
}
SliceStatus DestructibleInstance::slice(const SliceRequest& request, const std::function<void(std::span<const DestructiblePiece>)>& prepareRender) {
	auto bound = request;
	auto status = bind(bound);
	if (status != SliceStatus::Split) {
		return status;
	}
	return commit(prepareFracture(snapshot(bound)), bound, prepareRender);
}
SliceStatus DestructibleInstance::commit(PreparedFracture prepared, const SliceRequest& request,
	const std::function<void(std::span<const DestructiblePiece>)>& prepareRender, unsigned maximumBodies) {
	mError = prepared.error;
	auto world = mWorld.lock();
	if (!world) {
		return SliceStatus::Cancelled;
	}
	auto found = std::find_if(mPieces.begin(), mPieces.end(), [&](const auto& piece) { return piece.id == prepared.source; });
	if (found == mPieces.end() || !world->body(found->body) || prepared.revision != mRevision || request.piece != prepared.source
		|| !request.localSpace) {
		return SliceStatus::Stale;
	}
	if (prepared.status != SliceStatus::Split) {
		return prepared.status;
	}
	if (mPieces.size() >= mDefinition->settings.MaximumPieces || found->depth >= mDefinition->settings.MaximumDepth
		|| world->bodies().size() >= maximumBodies) {
		return SliceStatus::BudgetExceeded;
	}
	const auto source = *world->body(found->body);
	// External attachments and targeted fields need an explicit remapping policy; never silently discard them.
	for (const auto& [id, joint] : world->joints()) {
		if (joint.bodyA == source.id || joint.bodyB == source.id) {
			mError = "Cannot slice a body attached to a rigid joint";
			return SliceStatus::UnsupportedMesh;
		}
	}
	for (const auto& [id, attachment] : world->attachments()) {
		if (attachment.body == source.id) {
			mError = "Cannot slice a body with particle attachments";
			return SliceStatus::UnsupportedMesh;
		}
	}
	for (const auto& [id, field] : world->forces.fields()) {
		if (field.target == source.id) {
			mError = "Cannot slice a body with a targeted force field";
			return SliceStatus::UnsupportedMesh;
		}
	}
	std::array<DestructiblePiece, 2> candidates {};
	std::array<PHYSICS::BodyDef, 2> definitions {};
	std::array<Vec3, 2> forces {};
	std::array<Vec3, 2> torques {};
	try {
		const auto worldPoint = source.getPointInWorldSpace(request.point);
		if (!PHYSICS_COMMON::finite(worldPoint)) {
			throw std::invalid_argument("Slice impulse point overflow");
		}
		for (unsigned side = 0; side < 2; ++side) {
			auto& piece = candidates[side];
			auto& preparedPiece = prepared.pieces[side];
			if (!preparedPiece.hull) {
				throw std::invalid_argument("Missing cooked fracture hull");
			}
			piece.id = mNextId = nextPieceId();
			piece.depth = found->depth + 1;
			piece.mesh = std::move(preparedPiece.mesh);
			piece.mass = found->mass * static_cast<float>(preparedPiece.hull->volume / source.shape.polyhedron->volume);
			auto& body = definitions[side];
			body.shape = PHYSICS::Shape::convex(preparedPiece.hull);
			body.orientation = source.getOrientation();
			body.position = source.getPosition() + body.orientation * preparedPiece.center;
			body.velocity = source.getVelocity() + source.getRotation().cross(body.position - source.getPosition());
			body.angularVelocity = source.getRotation();
			body.mass = piece.mass;
			body.friction = source.friction;
			body.restitution = source.restitution;
			body.gravityScale = source.gravityScale;
			body.acceleration = source.extraAcceleration;
			body.linearDamping = -std::log(std::max(source.getLinearDamping(), 1e-30f));
			body.angularDamping = -std::log(std::max(source.getAngularDamping(), 1e-30f));
			body.allowSleep = source.getCanSleep();
			body.layer = source.layer;
			body.mask = source.mask;
			if (source.type == PHYSICS::BodyType::Static
				&& ((side == 0 && request.mode == StaticCutMode::KeepNegativeStatic)
					|| (side == 1 && request.mode == StaticCutMode::KeepPositiveStatic))) {
				body.type = PHYSICS::BodyType::Static;
			}
			const float weight = piece.mass / found->mass;
			forces[side] = (source.force + source.getAccumulatedForce()) * weight;
			torques[side] = (source.torque + source.getAccumulatedTorque()) * weight - (body.position - source.getPosition()).cross(forces[side]);
			if (!PHYSICS_COMMON::finite(forces[side]) || !PHYSICS_COMMON::finite(torques[side])) {
				throw std::invalid_argument("Slice force overflow");
			}
			// Retained static material absorbs its proportional impulse. Candidates have no contact islands to wake.
			if (body.type == PHYSICS::BodyType::Dynamic) {
				const auto impulse = request.impulse * weight;
				const auto torque = MATH::QuaternionF::Conjugate(body.orientation) * (worldPoint - body.position).cross(impulse);
				body.velocity += impulse / piece.mass;
				body.angularVelocity += body.orientation * (preparedPiece.hull->inertia * piece.mass).inverse().transform(torque);
			}
		}
		if (prepareRender) {
			prepareRender(candidates);
		}
		auto handles = world->createBodies(definitions);
		for (unsigned side = 0; side < 2; ++side) {
			auto& piece = candidates[side];
			piece.body = handles[side];
			auto* body = world->body(piece.body);
			body->force = forces[side];
			body->torque = torques[side];
		}
	} catch (const std::exception& error) {
		mError = error.what();
		return SliceStatus::UnsupportedMesh;
	}
	world->destroyBody(found->body);
	*found = std::move(candidates[0]);
	mPieces.push_back(std::move(candidates[1]));
	++mRevision;
	// Removing a support wakes the world; restore sleeping descendants only for an impulse-free sleeping source.
	if (!source.getAwake() && request.impulse.squareMagnitude() == 0) {
		world->body(found->body)->setAwake(false);
		world->body(mPieces.back().body)->setAwake(false);
	}
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
