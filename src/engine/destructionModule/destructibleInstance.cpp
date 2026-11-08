#include "destructibleInstance.h"
#include "collisionModule/compoundCollider.h"
#include "collisionModule/convexCollision.h"
#include <algorithm>
#include <cmath>
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
	struct SupportMaterial {
		SupportId piece = 0;
		COLLISION::ShapeView3D shape;
	};
	COLLISION::ShapeView3D supportGeometry(const PHYSICS::BodyDef& body) {
		return { body.shape, body.position,
			{ body.orientation * Vec3 { 1, 0, 0 }, body.orientation * Vec3 { 0, 1, 0 }, body.orientation * Vec3 { 0, 0, 1 } } };
	}
	std::vector<SupportId> supportedPieces(std::span<const SupportMaterial> materials, std::span<const AnchorVolume> volumes) {
		std::vector<SupportId> pieces {};
		std::vector<SupportAnchor> anchors {};
		std::vector<SupportAttachment> attachments {};
		pieces.reserve(materials.size());
		anchors.reserve(volumes.size());
		for (const auto& volume : volumes) {
			anchors.push_back({ volume.id, volume.enabled });
		}
		for (const auto& material : materials) {
			pieces.push_back(material.piece);
			for (const auto& volume : volumes) {
				if (volume.enabled && overlapsSupportAnchor(material.shape, volume)) {
					if (attachments.size() == SupportGraphLimits::MaximumAttachments) {
						throw std::length_error("Support attachment budget exceeded");
					}
					attachments.push_back({ material.piece, volume.id });
				}
			}
		}
		// Fracture cooking already emits one connected solid per piece. Separated
		// solids have no intact bonds, including when their new faces touch.
		const auto analysis = analyzeSupportGraph({ pieces, {}, anchors, attachments });
		std::vector<SupportId> supported {};
		for (const auto& island : analysis.islands) {
			if (island.supported) {
				supported.push_back(island.pieces.front());
			}
		}
		return supported;
	}
	PHYSICS::BodyDef bodyDefinition(const PHYSICS::Body& source, const DestructiblePiece& piece) {
		PHYSICS::BodyDef body {};
		body.shape = source.shape;
		body.position = source.getPosition();
		body.orientation = source.getOrientation();
		body.velocity = source.getVelocity();
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
		body.compoundInertiaPerUnitMass = piece.compoundInertiaPerUnitMass;
		return body;
	}
	const char* bodyDependency(const PHYSICS::World& world, PHYSICS::Id body) {
		for (const auto& [id, joint] : world.joints()) {
			if (joint.bodyA == body || joint.bodyB == body) {
				return " attached to a rigid joint";
			}
		}
		for (const auto& [id, attachment] : world.attachments()) {
			if (attachment.body == body) {
				return " with particle attachments";
			}
		}
		for (const auto& [id, field] : world.forces.fields()) {
			if (field.target == body) {
				return " with a targeted force field";
			}
		}
		return nullptr;
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
	mSupportAnchors.clear();
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
	std::vector<AnchorVolume> anchors {};
	if (mDefinition->support == SupportPolicy::Anchored) {
		const auto length = MATH::QuaternionF::Length(rotation);
		if (!std::isfinite(length) || length <= 0) {
			throw std::invalid_argument("Invalid anchored destructible orientation");
		}
		rotation = MATH::QuaternionF::Normalize(rotation);
		anchors.reserve(mDefinition->supportAnchors.size());
		for (const auto& anchor : mDefinition->supportAnchors) {
			anchors.push_back(makeWorldSupportAnchor(anchor, position, rotation));
		}
	}
	DestructiblePiece piece {};
	piece.mesh = mDefinition->mesh;
	piece.id = nextPieceId();
	piece.mass = mDefinition->settings.Mass;
	const auto& geometry = mDefinition->geometry;
	piece.volume = geometry.volume;
	piece.topologyBytes = pieceBytes(piece.mesh, geometry.shape);
	PHYSICS::BodyDef body {};
	body.position = position + rotation * geometry.center;
	body.orientation = rotation;
	body.mass = piece.mass;
	body.type = mDefinition->settings.Static ? PHYSICS::BodyType::Static : PHYSICS::BodyType::Dynamic;
	body.shape = geometry.shape;
	if (mDefinition->concave) {
		piece.compoundInertiaPerUnitMass = geometry.inertia;
		body.compoundInertiaPerUnitMass = piece.compoundInertiaPerUnitMass;
	}
	body.friction = mDefinition->settings.Friction;
	body.restitution = mDefinition->settings.Restitution;
	if (mDefinition->support == SupportPolicy::Anchored) {
		const SupportMaterial material { piece.id, supportGeometry(body) };
		const bool supported = !supportedPieces(std::span { &material, 1 }, anchors).empty();
		body.type = supported ? PHYSICS::BodyType::Static : PHYSICS::BodyType::Dynamic;
		piece.lifetimeLimited = !supported;
	}
	mPieces.reserve(mDefinition->settings.MaximumPieces);
	piece.body = world->createBody(body);
	mPieces.push_back(std::move(piece));
	mSupportAnchors = std::move(anchors);
}
SliceStatus DestructibleInstance::bind(SliceRequest& request) const {
	auto world = mWorld.lock();
	if (!world) {
		return SliceStatus::Cancelled;
	}
	if (request.cutout && (!mDefinition->concave || !request.piece || !validBoxCutout(*request.cutout))) {
		return SliceStatus::UnsupportedMesh;
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
		const COLLISION::ShapeView3D shape { body->shape };
		for (std::size_t index = 0; index < COLLISION::compoundLeafCount(shape); ++index) {
			const auto leaf = COLLISION::compoundLeafView(shape, index);
			const auto& hull = *leaf.shape.polyhedron;
			for (const auto vertex : hull.vertices) {
				const float projection = normal.dot(leaf.world(vertex) - local);
				low = std::min(low, projection);
				high = std::max(high, projection);
			}
			const auto leafPoint = leaf.local(local);
			const auto separation = COLLISION::convexSegmentSeparation(hull, leafPoint, leafPoint, 0);
			distance = std::min(distance, std::max(0.f, separation.gap));
		}
		if (low >= 0 || high <= 0) {
			continue;
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
		if (request.cutout) {
			request.cutout->orientation
				= MATH::QuaternionF::Normalize(MATH::QuaternionF::Conjugate(body->getOrientation()) * request.cutout->orientation);
		}
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
	return { found->id, mRevision, found->mesh, mDefinition->slicing, request.point, request.normal, mDefinition->concave, request.cutout };
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
	const auto count = prepared.pieces.size();
	if ((!request.cutout && count < 2) || found->depth >= mDefinition->settings.MaximumDepth
		|| mPieces.size() - 1 + count > mDefinition->settings.MaximumPieces || world->bodies().size() - 1 + count > maximumBodies) {
		return SliceStatus::BudgetExceeded;
	}
	const auto source = *world->body(found->body);
	// External attachments and targeted fields need an explicit remapping policy; never silently discard them.
	if (const auto dependency = bodyDependency(*world, source.id)) {
		mError = std::string("Cannot slice a body") + dependency;
		return SliceStatus::UnsupportedMesh;
	}
	std::vector<DestructiblePiece> candidates(count);
	std::vector<PHYSICS::BodyDef> definitions(count);
	std::vector<Vec3> forces(count);
	std::vector<Vec3> torques(count);
	std::vector<PHYSICS::Id> handles {};
	const bool hasImpulse = request.impulse != Vec3 {};
	const bool anchored = mDefinition->support == SupportPolicy::Anchored;
	try {
		const auto worldPoint = source.getPointInWorldSpace(request.point);
		if (!PHYSICS_COMMON::finite(worldPoint)) {
			throw std::invalid_argument("Slice impulse point overflow");
		}
		for (std::size_t index = 0; index < count; ++index) {
			auto& piece = candidates[index];
			auto& preparedPiece = prepared.pieces[index];
			const auto& geometry = preparedPiece.geometry;
			if (!(geometry.volume > 0)) {
				throw std::invalid_argument("Missing cooked fracture geometry");
			}
			piece.id = nextPieceId();
			piece.depth = found->depth + 1;
			piece.mesh = std::move(preparedPiece.mesh);
			piece.topologyBytes = pieceBytes(piece.mesh, geometry.shape);
			piece.lifetimeLimited = !request.cutout || found->lifetimeLimited;
			piece.age = request.cutout ? found->age : 0.f;
			piece.volume = geometry.volume;
			piece.mass = found->mass * static_cast<float>(piece.volume / found->volume);
			if (geometry.shape.kind == PHYSICS::Shape::Kind::Compound) {
				piece.compoundInertiaPerUnitMass = geometry.inertia;
			}
			auto& body = definitions[index];
			body = bodyDefinition(source, piece);
			body.shape = geometry.shape;
			body.position = source.getPosition() + body.orientation * geometry.center;
			body.velocity += body.angularVelocity.cross(body.position - source.getPosition());
			if (!anchored && source.type == PHYSICS::BodyType::Static
				&& (request.cutout || (!preparedPiece.positiveSide && request.mode == StaticCutMode::KeepNegativeStatic)
					|| (preparedPiece.positiveSide && request.mode == StaticCutMode::KeepPositiveStatic))) {
				body.type = PHYSICS::BodyType::Static;
			}
			const float weight = piece.mass / found->mass;
			forces[index] = (source.force + source.getAccumulatedForce()) * weight;
			torques[index] = (source.torque + source.getAccumulatedTorque()) * weight - (body.position - source.getPosition()).cross(forces[index]);
			if (!PHYSICS_COMMON::finite(forces[index]) || !PHYSICS_COMMON::finite(torques[index])) {
				throw std::invalid_argument("Slice force overflow");
			}
		}
		if (anchored && source.type == PHYSICS::BodyType::Static) {
			std::vector<SupportMaterial> materials {};
			materials.reserve(count);
			for (std::size_t index = 0; index < count; ++index) {
				materials.push_back({ candidates[index].id, supportGeometry(definitions[index]) });
			}
			const auto supported = supportedPieces(materials, mSupportAnchors);
			for (std::size_t index = 0; index < count; ++index) {
				const bool attached = std::binary_search(supported.begin(), supported.end(), candidates[index].id);
				definitions[index].type = attached ? PHYSICS::BodyType::Static : PHYSICS::BodyType::Dynamic;
				candidates[index].lifetimeLimited = !attached;
				candidates[index].age = 0;
			}
		}
		if (prepareRender) {
			prepareRender(candidates);
		}
		if (anchored) {
			// Render preparation must not mutate the simulation. Reject common direct-API
			// violations rather than retiring a source whose support decision is obsolete.
			const auto* current = world->body(source.id);
			if (prepared.revision != mRevision || !current || current->getPosition() != source.getPosition()
				|| !(current->getOrientation() == source.getOrientation()) || current->type != source.type
				|| current->getVelocity() != source.getVelocity() || current->getRotation() != source.getRotation()) {
				mError = "Support or source changed during fracture preparation";
				return SliceStatus::Stale;
			}
			if (const auto dependency = bodyDependency(*world, source.id)) {
				mError = std::string("Cannot slice a body") + dependency;
				return SliceStatus::UnsupportedMesh;
			}
		}
		handles = world->createBodies(definitions);
		for (std::size_t index = 0; index < count; ++index) {
			auto& piece = candidates[index];
			piece.body = handles[index];
			auto* body = world->body(piece.body);
			body->force = forces[index];
			body->torque = torques[index];
			// Reuse the physics world's validated tensor, including compound inversion and pose.
			if (body->type == PHYSICS::BodyType::Dynamic && hasImpulse) {
				const auto impulse = request.impulse * (piece.mass / found->mass);
				const auto angularImpulse = (worldPoint - body->getPosition()).cross(impulse);
				world->setVelocity(piece.body, body->getVelocity() + impulse * body->getInverseMass(),
					body->getRotation() + body->getInverseInertiaTensorWorld().transform(angularImpulse));
			}
		}
	} catch (const std::exception& error) {
		for (const auto handle : handles) {
			world->destroyBody(handle);
		}
		mError = error.what();
		return SliceStatus::UnsupportedMesh;
	}
	world->destroyBody(found->body);
	// Removing a support wakes the world; restore sleeping descendants only for an impulse-free sleeping source.
	if (!source.getAwake() && !hasImpulse && (!anchored || source.type == PHYSICS::BodyType::Dynamic)) {
		for (const auto& piece : candidates) {
			world->body(piece.body)->setAwake(false);
		}
	}
	if (candidates.empty()) {
		mPieces.erase(found);
	}
	else {
		*found = std::move(candidates.front());
		for (std::size_t index = 1; index < count; ++index) {
			mPieces.push_back(std::move(candidates[index]));
		}
	}
	++mRevision;
	return SliceStatus::Split;
}
SupportAnchorStatus DestructibleInstance::setSupportAnchorEnabled(SupportId anchor, bool enabled) {
	mError.clear();
	const auto world = mWorld.lock();
	if (!world || mSupportAnchors.empty()) {
		mError = "Support anchors require a live Anchored destructible";
		return SupportAnchorStatus::Rejected;
	}
	const auto found = std::find_if(mSupportAnchors.begin(), mSupportAnchors.end(), [anchor](const auto& volume) { return volume.id == anchor; });
	if (found == mSupportAnchors.end()) {
		mError = "Unknown support anchor ID";
		return SupportAnchorStatus::Rejected;
	}
	if (found->enabled == enabled) {
		return SupportAnchorStatus::Unchanged;
	}
	try {
		auto anchors = mSupportAnchors;
		anchors[static_cast<std::size_t>(found - mSupportAnchors.begin())].enabled = enabled;
		std::vector<SupportMaterial> materials {};
		materials.reserve(mPieces.size());
		for (const auto& piece : mPieces) {
			const auto* body = world->body(piece.body);
			if (body && body->type == PHYSICS::BodyType::Static) {
				materials.push_back({ piece.id, PHYSICS::geometry(*body) });
			}
		}
		const auto supported = supportedPieces(materials, anchors);
		struct Detachment {
			DestructiblePiece* piece = nullptr;
			Vec3 force {};
			Vec3 torque {};
		};
		std::vector<Detachment> detached {};
		std::vector<PHYSICS::BodyDef> definitions {};
		for (auto& piece : mPieces) {
			const auto* body = world->body(piece.body);
			if (!body || body->type != PHYSICS::BodyType::Static || std::binary_search(supported.begin(), supported.end(), piece.id)) {
				continue;
			}
			if (const auto dependency = bodyDependency(*world, piece.body)) {
				mError = std::string("Cannot detach a body") + dependency;
				return SupportAnchorStatus::Rejected;
			}
			const auto force = body->force + body->getAccumulatedForce();
			const auto torque = body->torque + body->getAccumulatedTorque();
			if (!PHYSICS_COMMON::finite(force) || !PHYSICS_COMMON::finite(torque)) {
				throw std::invalid_argument("Support detachment force overflow");
			}
			detached.push_back({ &piece, force, torque });
			definitions.push_back(bodyDefinition(*body, piece));
		}
		// Create and validate the entire batch before retiring any original body.
		const auto handles = world->createBodies(definitions);
		for (std::size_t index = 0; index < detached.size(); ++index) {
			auto& piece = *detached[index].piece;
			auto& body = *world->body(handles[index]);
			body.force = detached[index].force;
			body.torque = detached[index].torque;
			world->destroyBody(piece.body);
			piece.body = body.id;
			piece.lifetimeLimited = true;
			piece.age = 0;
		}
		mSupportAnchors = std::move(anchors);
		++mRevision;
		return SupportAnchorStatus::Applied;
	} catch (const std::exception& error) {
		mError = error.what();
		return SupportAnchorStatus::Rejected;
	}
}
std::span<const AnchorVolume> DestructibleInstance::supportAnchors() const {
	return mSupportAnchors;
}
void DestructibleInstance::advance(float dt) {
	auto world = mWorld.lock();
	if (!world) {
		mPieces.clear();
		mSupportAnchors.clear();
		return;
	}
	for (auto& piece : mPieces) {
		piece.age += dt;
		if (piece.lifetimeLimited && mDefinition->settings.Lifetime > 0 && piece.age >= mDefinition->settings.Lifetime) {
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
