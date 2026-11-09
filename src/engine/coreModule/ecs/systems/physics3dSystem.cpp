#include "physics3dSystem.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/components/physics3dComponent.h"
#include "coreModule/ecs/components/physicsComponent.h"
#include "sceneModule/scene.h"
#include "utilsModule/log/logger.h"
#include <iostream>
#include <set>
#ifdef USE_EDITOR
#include <imgui.h>
#endif

using namespace IKIGAI;
using namespace IKIGAI::ECS;
namespace P = IKIGAI::PHYSICS;
namespace {
template <class C> void release(Object& o) {
	if (auto c = o.getComponent<C>()) {
		c->runtime.reset();
	}
}
template <class C, class Create> void synchronize(C& c, const std::shared_ptr<P::World>& world, Physics3DLease::Kind kind, Create create) {
	auto signature = serde::serialize<nlohmann::json>(c.getDescriptor()).dump();
	bool valid = c.runtime && c.runtime->world.lock() == world && c.runtime->signature == signature;
	if (valid) {
		switch (kind) {
		case Physics3DLease::Kind::Body:
			valid = world->body(c.runtime->id) != nullptr;
			break;
		case Physics3DLease::Kind::Soft:
			valid = world->softBody(c.runtime->id) != nullptr;
			break;
		case Physics3DLease::Kind::Fluid:
			valid = world->fluid(c.runtime->id) != nullptr;
			break;
		case Physics3DLease::Kind::Field:
			valid = world->forces.field(c.runtime->id) != nullptr;
			break;
		}
	}
	if (valid) {
		return;
	}
	c.runtime.reset();
	auto lease = std::make_shared<Physics3DLease>();
	lease->world = world;
	lease->kind = kind;
	lease->signature = std::move(signature);
	lease->id = create();
	c.runtime = std::move(lease);
}
void gridDimensions(int w, int h, int d, float spacing) {
	if (w < 1 || h < 1 || d < 1 || w > 64 || h > 64 || d > 64 || !std::isfinite(spacing) || spacing <= 0) {
		throw std::invalid_argument("3D particle grid requires 1..64 rows/columns/layers and positive spacing");
	}
}
P::SoftBodyDef softDefinition(const SoftBody3DComponent::Descriptor& s, const Object& o) {
	gridDimensions(s.Width, s.Height, s.Depth, s.Spacing);
	if (!std::isfinite(s.ParticleMass) || s.ParticleMass <= 0) {
		throw std::invalid_argument("Invalid soft particle mass");
	}
	P::SoftBodyDef d;
	d.radius = s.Radius;
	d.compliance = s.Compliance;
	d.volumeCompliance = s.VolumeCompliance;
	d.damping = s.Damping;
	d.selfCollision = s.SelfCollision;
	auto transform = MATH::QuaternionF::ToMatrix3(o.tryGetTransform()->getLocalRotation());
	auto origin = o.tryGetTransform()->getLocalPosition();
	auto point = [&](P::Vec3 p) { return origin + transform.transform(p); };
	if (!s.Vertices.empty()) {
		if (s.Tetrahedra.empty() || s.Tetrahedra.size() % 4) {
			throw std::invalid_argument("Soft mesh requires tetrahedron index groups of four");
		}
		for (auto p : s.Vertices) {
			d.particles.push_back({ point(p), {}, 1 / s.ParticleMass });
		}
		for (std::size_t i = 0; i < s.Tetrahedra.size(); i += 4) {
			P::Tetrahedron t;
			for (int j = 0; j < 4; ++j) {
				int n = s.Tetrahedra[i + j];
				if (n < 0 || std::size_t(n) >= d.particles.size()) {
					throw std::invalid_argument("Soft mesh index out of range");
				}
				t[j] = std::size_t(n);
			}
			d.tetrahedra.push_back(t);
		}
	}
	else {
		if (s.Width < 2 || s.Height < 2 || s.Depth < 2) {
			throw std::invalid_argument("Soft volume needs at least 2x2x2 particles");
		}
		for (int z = 0; z < s.Depth; ++z) {
			for (int y = 0; y < s.Height; ++y) {
				for (int x = 0; x < s.Width; ++x) {
					d.particles.push_back(
						{ point({ x * s.Spacing, y * s.Spacing, z * s.Spacing }), {}, s.PinTop && y == s.Height - 1 ? 0 : 1 / s.ParticleMass });
				}
			}
		}
		auto index = [&](int x, int y, int z) { return std::size_t((z * s.Height + y) * s.Width + x); };
		std::array<int, 3> axes { 0, 1, 2 };
		for (int z = 0; z + 1 < s.Depth; ++z) {
			for (int y = 0; y + 1 < s.Height; ++y) {
				for (int x = 0; x + 1 < s.Width; ++x) {
					do {
						std::array<int, 3> v { x, y, z };
						P::Tetrahedron t;
						t[0] = index(x, y, z);
						++v[axes[0]];
						t[1] = index(v[0], v[1], v[2]);
						++v[axes[1]];
						t[2] = index(v[0], v[1], v[2]);
						t[3] = index(x + 1, y + 1, z + 1);
						d.tetrahedra.push_back(t);
					} while (std::next_permutation(axes.begin(), axes.end()));
				}
			}
		}
	}
	std::set<std::pair<std::size_t, std::size_t>> links;
	std::map<P::Triangle, std::pair<int, P::Triangle>> faces;
	for (auto& t : d.tetrahedra) {
		auto a = d.particles[t[0]].position, b = d.particles[t[1]].position, c = d.particles[t[2]].position, e = d.particles[t[3]].position;
		if ((b - a).dot((c - a).cross(e - a)) < 0) {
			std::swap(t[1], t[2]);
		}
		for (int i = 0; i < 4; ++i) {
			for (int j = i + 1; j < 4; ++j) {
				links.emplace(std::min(t[i], t[j]), std::max(t[i], t[j]));
			}
		}
		for (auto face : std::array<P::Triangle, 4> { { { t[0], t[2], t[1] }, { t[0], t[1], t[3] }, { t[0], t[3], t[2] }, { t[1], t[2], t[3] } } }) {
			auto key = face;
			std::sort(key.begin(), key.end());
			auto& f = faces[key];
			++f.first;
			f.second = face;
		}
	}
	for (auto [a, b] : links) {
		d.links.push_back({ a, b, (d.particles[b].position - d.particles[a].position).magnitude(), 0 });
	}
	for (auto& [key, f] : faces) {
		if (f.first == 1) {
			d.triangles.push_back(f.second);
		}
	}
	return d;
}
}
Physics3DSystem::Physics3DSystem(SCENE_SYSTEM::Scene& scene, std::shared_ptr<P::World> simulation)
	: mScene(scene)
	, mSimulation(std::move(simulation)) {
	if (!mSimulation) {
		throw std::invalid_argument("Physics3D factory returned null");
	}
	mName = "Physics3DSystem";
}
void Physics3DSystem::onFixedUpdate(ECS::World&, ECS::CommandBuffer&, std::chrono::duration<double> dt) {
	mLastStepSucceeded = false;
	auto objects = mScene.getObjects();
	for (auto& object : objects) {
		auto& o = *object;
		if (!o.isAlive() || !o.getIsActive()) {
			release<Physics3DComponent>(o);
			release<SoftBody3DComponent>(o);
			release<Cloth3DComponent>(o);
			release<Fluid3DComponent>(o);
			release<ForceField3DComponent>(o);
			release<PhysicsComponent>(o);
			continue;
		}
		auto rigid = o.getComponent<Physics3DComponent>();
		auto soft = o.getComponent<SoftBody3DComponent>();
		auto cloth = o.getComponent<Cloth3DComponent>();
		auto fluid = o.getComponent<Fluid3DComponent>();
		auto legacy = o.getComponent<PhysicsComponent>();
		if (legacy && legacy->body && legacy->collider) {
			if (rigid || soft || cloth || fluid) {
				throw std::invalid_argument("Legacy and new 3D physics components cannot share an object");
			}
			auto& c = *legacy;
			if (c.GetDirty()) {
				c.runtime.reset();
			}
			synchronize(c, mSimulation, Physics3DLease::Kind::Body, [&] {
				P::BodyDef d;
				d.type = c.body->hasFiniteMass() ? P::BodyType::Dynamic : P::BodyType::Static;
				if (c.collisionType == CollisionType::SPHERE) {
					d.shape = P::Shape::sphere(static_cast<P::CollisionSphere*>(c.collider.get())->radius);
				}
				else if (c.collisionType == CollisionType::AABB || c.collisionType == CollisionType::OBB) {
					d.shape = P::Shape::box(static_cast<P::CollisionBox*>(c.collider.get())->halfSize);
				}
				else {
					throw std::invalid_argument("Unsupported legacy collider");
				}
				d.position = c.body->getPosition();
				d.orientation = c.body->getOrientation();
				d.velocity = c.body->getVelocity();
				d.angularVelocity = c.body->getRotation();
				d.mass = d.type == P::BodyType::Dynamic ? c.body->getMass() : 1;
				d.gravityScale = 0;
				d.acceleration = c.body->getAcceleration();
				return mSimulation->createBody(d);
			});
			auto* b = mSimulation->body(c.runtime->id);
			static_cast<P::RigidBody&>(*b) = *c.body;
			b->clearAccumulators();
			b->extraAcceleration = c.body->getAcceleration();
			b->calculateDerivedData();
			auto force = c.body->getAccumulatedForce(), torque = c.body->getAccumulatedTorque();
			if (force.squareMagnitude() > 0) {
				mSimulation->applyForce(b->id, force, b->getPosition());
			}
			b->torque += torque;
			c.body->clearAccumulators();
			c.ResetDirty();
			if (b->type == P::BodyType::Static) {
				auto p = o.tryGetTransform()->getLocalPosition();
				auto q = o.tryGetTransform()->getLocalRotation();
				if (p != b->getPosition() || q != b->getOrientation()) {
					mSimulation->teleport(b->id, p, q);
				}
			}
		}
		else if (legacy) {
			legacy->runtime.reset();
		}
		if (int(bool(rigid)) + int(bool(soft)) + int(bool(cloth)) + int(bool(fluid)) > 1) {
			throw std::invalid_argument("Only one 3D rigid/soft/cloth/fluid component is allowed per object");
		}
		if (rigid || soft || cloth || fluid) {
			auto scale = o.tryGetTransform()->getLocalScale();
			if (o.hasParent() || std::abs(scale.x - 1) > 1e-4f || std::abs(scale.y - 1) > 1e-4f || std::abs(scale.z - 1) > 1e-4f) {
				throw std::invalid_argument("3D physics requires root objects with unit scale; size belongs to the descriptor");
			}
		}
		if (auto field = o.getComponent<ForceField3DComponent>()) {
			auto& c = *field;
			synchronize(c, mSimulation, Physics3DLease::Kind::Field, [&] {
				auto& s = c.settings;
				P::ForceField f;
				f.kind = static_cast<P::ForceField::Kind>(s.Kind);
				f.vector = s.Vector;
				f.center = o.tryGetTransform()->getWorldPosition();
				f.radius = s.Radius;
				f.linear = s.Linear;
				f.quadratic = s.Quadratic;
				f.area = s.Area;
				f.density = s.Density;
				f.waterHeight = s.WaterHeight;
				f.maxDepth = s.MaxDepth;
				f.volume = s.Volume;
				f.gustAmplitude = s.GustAmplitude;
				f.gustFrequency = s.GustFrequency;
				f.seed = static_cast<std::uint32_t>(s.Seed);
				f.targets = static_cast<unsigned>(s.Targets);
				return mSimulation->forces.add(f);
			});
			mSimulation->forces.field(c.simulationId())->center = o.tryGetTransform()->getWorldPosition();
		}
		if (rigid) {
			auto& c = *rigid;
			auto& s = c.settings;
			synchronize(c, mSimulation, Physics3DLease::Kind::Body, [&] {
				if (s.BodyMode < 0 || s.BodyMode > 1 || s.ShapeKind < 0 || s.ShapeKind > 1) {
					throw std::invalid_argument("Invalid 3D body/shape kind");
				}
				P::BodyDef d;
				d.type = s.BodyMode ? P::BodyType::Dynamic : P::BodyType::Static;
				d.shape = s.ShapeKind ? P::Shape::box(s.HalfSize) : P::Shape::sphere(s.Radius);
				d.position = o.tryGetTransform()->getLocalPosition();
				d.orientation = o.tryGetTransform()->getLocalRotation();
				d.mass = s.Mass;
				d.friction = s.Friction;
				d.restitution = s.Restitution;
				d.gravityScale = s.GravityScale;
				d.velocity = s.Velocity;
				d.angularVelocity = s.AngularVelocity;
				d.linearDamping = s.LinearDamping;
				d.angularDamping = s.AngularDamping;
				d.allowSleep = s.AllowSleep;
				return mSimulation->createBody(d);
			});
			auto* b = mSimulation->body(c.simulationId());
			if (c.pendingForce.squareMagnitude() > 0) {
				mSimulation->applyForce(b->id, c.pendingForce, b->getPosition());
			}
			if (c.pendingImpulse.squareMagnitude() > 0) {
				mSimulation->applyImpulse(b->id, c.pendingImpulse, b->getPosition());
			}
			c.pendingForce = {};
			c.pendingImpulse = {};
			if (b->type == P::BodyType::Static) {
				auto p = o.tryGetTransform()->getLocalPosition();
				auto q = o.tryGetTransform()->getLocalRotation();
				if (p != b->getPosition() || q != b->getOrientation()) {
					mSimulation->teleport(b->id, p, q);
				}
			}
		}
		if (soft) {
			auto& c = *soft;
			synchronize(c, mSimulation, Physics3DLease::Kind::Soft, [&] { return mSimulation->createSoftBody(softDefinition(c.settings, o)); });
		}
		if (cloth) {
			auto& c = *cloth;
			synchronize(c, mSimulation, Physics3DLease::Kind::Soft, [&] {
				auto& s = c.settings;
				P::ClothDef d;
				d.width = s.Width;
				d.height = s.Height;
				d.spacing = s.Spacing;
				d.radius = s.Radius;
				d.particleMass = s.ParticleMass;
				d.stretchCompliance = s.StretchCompliance;
				d.shearCompliance = s.ShearCompliance;
				d.bendCompliance = s.BendCompliance;
				d.damping = s.Damping;
				d.pinTop = s.PinTop;
				d.selfCollision = s.SelfCollision;
				d.impermeable = s.Impermeable;
				d.position = o.tryGetTransform()->getLocalPosition();
				auto r = MATH::QuaternionF::ToMatrix3(o.tryGetTransform()->getLocalRotation());
				d.axisU = r.transform({ 1, 0, 0 });
				d.axisV = r.transform({ 0, -1, 0 });
				return mSimulation->createCloth(d);
			});
		}
		if (fluid) {
			auto& c = *fluid;
			synchronize(c, mSimulation, Physics3DLease::Kind::Fluid, [&] {
				auto& s = c.settings;
				gridDimensions(s.Width, s.Height, s.Depth, s.Spacing);
				P::FluidDef d;
				d.radius = s.Radius;
				d.smoothingRadius = s.SmoothingRadius;
				d.particleMass = s.ParticleMass;
				d.restDensity = s.RestDensity;
				d.viscosity = s.Viscosity;
				auto origin = o.tryGetTransform()->getLocalPosition();
				auto r = MATH::QuaternionF::ToMatrix3(o.tryGetTransform()->getLocalRotation());
				for (int z = 0; z < s.Depth; ++z) {
					for (int y = 0; y < s.Height; ++y) {
						for (int x = 0; x < s.Width; ++x) {
							d.particles.push_back({ origin + r.transform({ x * s.Spacing, y * s.Spacing, z * s.Spacing }), {}, 1 });
						}
					}
				}
				return mSimulation->createFluid(d);
			});
		}
	}
	if (!paused || singleStep) {
		try {
			mSimulation->step(static_cast<float>(dt.count()));
			mLastStepSucceeded = true;
		} catch (const P::ContactBudgetExceeded& error) {
			// The kernel restored the last committed tick, including pending forces.
			// Stop retries until the caller adjusts the scene/budget and resumes.
			paused = true;
			singleStep = false;
			LOG_ERROR << "3D physics paused after rejected tick: " << error.what() << " (required at least " << error.requiredContacts << ", limit "
					  << error.maximumContacts << "). Adjust maximumContacts or scene density before resuming.";
			return;
		}
	}
	singleStep = false;
	for (auto& object : objects) {
		if (auto c = object->getComponent<Physics3DComponent>(); c && c->runtime) {
			if (auto* b = mSimulation->body(c->simulationId()); b && b->type == P::BodyType::Dynamic) {
				object->tryGetTransform()->setLocalPosition(b->getPosition());
				object->tryGetTransform()->setLocalRotation(b->getOrientation());
			}
		}
	}
	for (auto& object : objects) {
		if (auto c = object->getComponent<PhysicsComponent>(); c && c->runtime && c->body && c->collider) {
			if (auto* b = mSimulation->body(c->runtime->id)) {
				*c->body = *b;
				c->collider->body = c->body.get();
				c->collider->calculateInternals();
				if (b->type == P::BodyType::Dynamic) {
					object->tryGetTransform()->setLocalPosition(b->getPosition());
					object->tryGetTransform()->setLocalRotation(b->getOrientation());
				}
			}
		}
	}
}
#ifdef USE_EDITOR
void Physics3DSystem::drawDebug() {
	if (mSimulation->bodies().empty() && mSimulation->softBodies().empty() && mSimulation->fluids().empty() && mSimulation->forces.fields().empty()) {
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(480, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("3D Physics")) {
		ImGui::End();
		return;
	}
	ImGui::Checkbox("Paused", &paused);
	ImGui::SameLine();
	if (ImGui::Button("Step")) {
		singleStep = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset")) {
		mSimulation->clear();
	}
	ImGui::Text("Bodies: %zu  Soft/cloth: %zu  Fluids: %zu  Fields: %zu", mSimulation->bodies().size(), mSimulation->softBodies().size(),
		mSimulation->fluids().size(), mSimulation->forces.fields().size());
	// An orthographic preview makes the simulation inspectable with any renderer.
	static float scale = 80;
	ImGui::SliderFloat("Preview scale", &scale, 10, 250);
	auto size = ImGui::GetContentRegionAvail();
	size.x = std::max(size.x, 100.0f);
	size.y = std::max(size.y, 200.0f);
	auto origin = ImGui::GetCursorScreenPos();
	auto* draw = ImGui::GetWindowDrawList();
	ImGui::InvisibleButton("physics3d_canvas", size);
	draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
	auto project = [&](P::Vec3 p) {
		return ImVec2(origin.x + size.x * 0.3f + (p.x + p.z * 0.35f) * scale, origin.y + size.y * 0.8f - (p.y + p.z * 0.2f) * scale);
	};
	mSimulation->debugDraw({ [&](auto a, auto b, std::uint32_t) { draw->AddLine(project(a), project(b), IM_COL32(210, 150, 240, 255)); },
		[&](auto p, float r, std::uint32_t) { draw->AddCircle(project(p), std::max(1.0f, r * scale), IM_COL32(80, 170, 240, 255), 12); }, {} });
	draw->PopClipRect();
	ImGui::End();
}
#endif

#endif // USE_PHYSICS_3D
