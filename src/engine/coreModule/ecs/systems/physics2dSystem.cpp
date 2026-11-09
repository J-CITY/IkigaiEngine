#include "physics2dSystem.h"
#if USE_PHYSICS_2D
#include "coreModule/ecs/components/cloth2dComponent.h"
#include "coreModule/ecs/components/forceField2dComponent.h"
#include "coreModule/ecs/components/physics2dComponent.h"
#include "sceneModule/scene.h"
#include <stdexcept>

#ifdef USE_EDITOR
#include <imgui.h>
#endif

using namespace IKIGAI;
using namespace IKIGAI::ECS;
namespace P = IKIGAI::PHYSICS2D;
namespace {
P::Vec2 vec(MATH::Vector2f v) {
	return { v.x, v.y };
}
float angle(const Object& o) {
	return MATH::QuaternionF::ToEulerAngles(o.tryGetTransform()->getLocalRotation()).z;
}
P::Vec2 position(const Object& o) {
	auto p = o.tryGetTransform()->getLocalPosition();
	return { p.x, p.y };
}
void gridDimensions(int w, int h, float spacing) {
	if (w < 1 || h < 1 || w > 128 || h > 128 || !std::isfinite(spacing) || spacing <= 0) {
		throw std::invalid_argument("Particle grid requires 1..128 rows/columns and positive spacing");
	}
}
template <class C, class Create> void synchronize(C& c, const std::shared_ptr<P::World>& world, Physics2DLease::Kind kind, Create create) {
	auto signature = serde::serialize<nlohmann::json>(c.getDescriptor()).dump();
	bool valid = c.runtime && c.runtime->world.lock() == world && c.runtime->signature == signature;
	if (valid) {
		switch (kind) {
		case Physics2DLease::Kind::Field:
			valid = world->forces.field(c.runtime->id) != nullptr;
			break;
		case Physics2DLease::Kind::Body:
			valid = world->body(c.runtime->id) != nullptr;
			break;
		case Physics2DLease::Kind::Soft:
			valid = world->softBody(c.runtime->id) != nullptr;
			break;
		case Physics2DLease::Kind::Fluid:
			valid = world->fluid(c.runtime->id) != nullptr;
			break;
		case Physics2DLease::Kind::Joint:
			valid = world->joints().contains(c.runtime->id);
			break;
		}
	}
	if (valid) {
		return;
	}
	c.runtime.reset();
	auto lease = std::make_shared<Physics2DLease>();
	lease->world = world;
	lease->kind = kind;
	lease->signature = std::move(signature);
	lease->id = create();
	c.runtime = std::move(lease);
}
template <class C> void release(Object& o) {
	if (auto c = o.getComponent<C>()) {
		c->runtime.reset();
	}
}
}
Physics2DSystem::Physics2DSystem(SCENE_SYSTEM::Scene& scene, std::shared_ptr<P::World> simulation)
	: mScene(scene)
	, mSimulation(std::move(simulation)) {
	if (!mSimulation) {
		throw std::invalid_argument("Physics2D factory returned null");
	}
	mName = "Physics2DSystem";
}
void Physics2DSystem::onFixedUpdate(ECS::World&, ECS::CommandBuffer&, std::chrono::duration<double> dt) {
	auto objects = mScene.getObjects();
	// Enumerate this scene's objects, not the global ECS arrays: a loading scene
	// and an active scene may temporarily share ECS storage, but never physics.
	for (auto& object : objects) {
		auto& o = *object;
		if (!o.isAlive() || !o.getIsActive()) {
			release<Joint2DComponent>(o);
			release<Physics2DComponent>(o);
			release<SoftBody2DComponent>(o);
			release<Fluid2DComponent>(o);
			release<ForceField2DComponent>(o);
			release<Cloth2DComponent>(o);
			continue;
		}
		auto rigid = o.getComponent<Physics2DComponent>();
		auto soft = o.getComponent<SoftBody2DComponent>();
		auto fluid = o.getComponent<Fluid2DComponent>();
		auto cloth = o.getComponent<Cloth2DComponent>();
		if (auto field = o.getComponent<ForceField2DComponent>()) {
			auto& c = *field;
			synchronize(c, mSimulation, Physics2DLease::Kind::Field, [&] {
				auto& s = c.settings;
				P::ForceField f;
				f.kind = static_cast<P::ForceField::Kind>(s.Kind);
				auto center = o.tryGetTransform()->getWorldPosition();
				f.vector = vec(s.Vector);
				f.center = { center.x, center.y };
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
			auto center = o.tryGetTransform()->getWorldPosition();
			mSimulation->forces.field(c.simulationId())->center = { center.x, center.y };
		}
		if ((bool(rigid) + bool(soft) + bool(fluid) + bool(cloth)) > 1) {
			throw std::invalid_argument("An object may have only one of rigid, soft or fluid 2D components");
		}
		if (!rigid && !soft && !fluid && !cloth) {
			continue;
		}
		// Parent transforms and non-unit scale are explicit errors rather than
		// silently mismatching rendered and physical geometry. Collider sizes
		// are world units and independent of sprite pixel/anchor dimensions.
		auto scale = o.tryGetTransform()->getLocalScale();
		auto rotation = o.tryGetTransform()->getLocalRotation();
		if (o.hasParent() || std::abs(scale.x - 1) > 1e-4f || std::abs(scale.y - 1) > 1e-4f || std::abs(rotation.x) > 1e-4f
			|| std::abs(rotation.y) > 1e-4f) {
			throw std::invalid_argument("2D physics requires root objects, unit XY scale and rotation about Z only");
		}
		if (rigid) {
			auto& c = *rigid;
			auto& s = c.settings;
			synchronize(c, mSimulation, Physics2DLease::Kind::Body, [&] {
				if (s.BodyMode < 0 || s.BodyMode > 1 || s.ShapeKind < 0 || s.ShapeKind > 2) {
					throw std::invalid_argument("Invalid 2D body/shape kind");
				}
				P::BodyDef def;
				def.type = s.BodyMode == 0 ? P::BodyType::Static : P::BodyType::Dynamic;
				if (s.ShapeKind == 0) {
					def.shape = P::Shape::circle(s.Radius);
				}
				else if (s.ShapeKind == 1) {
					def.shape = P::Shape::box(vec(s.HalfSize));
				}
				else {
					std::vector<P::Vec2> vertices;
					for (auto p : s.Vertices) {
						vertices.push_back(vec(p));
					}
					def.shape = P::Shape::polygon(std::move(vertices));
				}
				def.position = position(o);
				def.angle = angle(o);
				def.mass = s.Mass;
				def.friction = s.Friction;
				def.restitution = s.Restitution;
				def.gravityScale = s.GravityScale;
				def.velocity = vec(s.Velocity);
				def.angularVelocity = s.AngularVelocity;
				def.allowSleep = s.AllowSleep;
				return mSimulation->createBody(def);
			});
			auto* b = mSimulation->body(c.simulationId());
			if (P::dot(c.pendingForce, c.pendingForce) > 0) {
				mSimulation->applyForce(b->id, c.pendingForce, b->position);
			}
			if (P::dot(c.pendingImpulse, c.pendingImpulse) > 0) {
				mSimulation->applyImpulse(b->id, c.pendingImpulse, b->position);
			}
			c.pendingForce = {};
			c.pendingImpulse = {};
			if (b->type == P::BodyType::Static) {
				auto p = position(o);
				float r = angle(o);
				if (P::length(p - b->position) > 1e-6f || std::abs(r - b->angle) > 1e-6f) {
					mSimulation->teleport(b->id, p, r);
				}
			}
		}
		if (cloth) {
			auto& c = *cloth;
			synchronize(c, mSimulation, Physics2DLease::Kind::Soft, [&] {
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
				d.pinTop = s.PinTop;
				d.selfCollision = s.SelfCollision;
				auto id = mSimulation->createCloth(d);
				auto* sim = mSimulation->softBody(id);
				for (auto& p : sim->particles) {
					p.position = position(o) + P::rotate(p.position, angle(o));
				}
				return id;
			});
		}
		auto spawn = [&](int x, int y, float spacing) { return position(o) + P::rotate({ x * spacing, y * spacing }, angle(o)); };
		if (soft) {
			auto& c = *soft;
			auto& s = c.settings;
			synchronize(c, mSimulation, Physics2DLease::Kind::Soft, [&] {
				gridDimensions(s.Width, s.Height, s.Spacing);
				if (!std::isfinite(s.ParticleMass) || s.ParticleMass <= 0) {
					throw std::invalid_argument("Soft particle mass must be positive");
				}
				P::SoftBodyDef def;
				def.radius = s.Radius;
				def.stiffness = s.Stiffness;
				def.areaStiffness = s.AreaStiffness;
				def.selfCollision = s.SelfCollision;
				for (int y = 0; y < s.Height; ++y) {
					for (int x = 0; x < s.Width; ++x) {
						def.particles.push_back({ spawn(x, y, s.Spacing), {}, s.PinTop && y == s.Height - 1 ? 0.0f : 1 / s.ParticleMass });
					}
				}
				auto link = [&](int a, int b) {
					def.links.push_back({ std::size_t(a), std::size_t(b), P::length(def.particles[a].position - def.particles[b].position) });
				};
				for (int y = 0; y < s.Height; ++y) {
					for (int x = 0; x < s.Width; ++x) {
						int i = y * s.Width + x;
						if (x + 1 < s.Width) {
							link(i, i + 1);
						}
						if (y + 1 < s.Height) {
							link(i, i + s.Width);
						}
						if (x + 1 < s.Width && y + 1 < s.Height) {
							link(i, i + s.Width + 1);
							link(i + 1, i + s.Width);
						}
					}
				}
				if (s.PreserveArea && s.Width > 1 && s.Height > 1) {
					for (int x = 0; x < s.Width; ++x) {
						def.boundary.push_back(x);
					}
					for (int y = 1; y < s.Height; ++y) {
						def.boundary.push_back(y * s.Width + s.Width - 1);
					}
					for (int x = s.Width - 2; x >= 0; --x) {
						def.boundary.push_back((s.Height - 1) * s.Width + x);
					}
					for (int y = s.Height - 2; y > 0; --y) {
						def.boundary.push_back(y * s.Width);
					}
				}
				return mSimulation->createSoftBody(def);
			});
		}
		if (fluid) {
			auto& c = *fluid;
			auto& s = c.settings;
			synchronize(c, mSimulation, Physics2DLease::Kind::Fluid, [&] {
				gridDimensions(s.Width, s.Height, s.Spacing);
				P::FluidDef def;
				def.radius = s.Radius;
				def.smoothingRadius = s.SmoothingRadius;
				def.particleMass = s.ParticleMass;
				def.restDensity = s.RestDensity;
				def.viscosity = s.Viscosity;
				for (int y = 0; y < s.Height; ++y) {
					for (int x = 0; x < s.Width; ++x) {
						def.particles.push_back({ spawn(x, y, s.Spacing) });
					}
				}
				return mSimulation->createFluid(def);
			});
		}
	}
	for (auto& object : objects) {
		if (auto joint = object->getComponent<Joint2DComponent>()) {
			auto owner = object->getComponent<Physics2DComponent>();
			auto target = mScene.findObjectByID(ECS::Entity(ECS::Entity::ID(joint->settings.PhysicsTargetId)));
			auto other = target ? target->getComponent<Physics2DComponent>() : UTILS::WeakPtr<Physics2DComponent> {};
			if (!object->getIsActive() || !owner || !owner->runtime || !other || !other->runtime || !target->getIsActive()) {
				joint->runtime.reset();
				continue;
			}
			// A body can have been recreated due to a collider edit. Its previous
			// joints were destroyed by World, so synchronize recreates their leases.
			auto& s = joint->settings;
			synchronize(*joint, mSimulation, Physics2DLease::Kind::Joint, [&] {
				if (s.JointKind < 0 || s.JointKind > 1) {
					throw std::invalid_argument("Invalid joint kind");
				}
				P::JointDef def;
				def.a = owner->simulationId();
				def.b = other->simulationId();
				def.kind = s.JointKind == 0 ? P::JointDef::Kind::Distance : P::JointDef::Kind::Revolute;
				def.localAnchorA = vec(s.AnchorA);
				def.localAnchorB = vec(s.AnchorB);
				def.distance = s.Distance;
				def.collideConnected = s.CollideConnected;
				return mSimulation->createJoint(def);
			});
		}
	}
	if (!paused || singleStep) {
		mSimulation->step(static_cast<float>(dt.count()));
	}
	singleStep = false;
	for (auto& o : objects) {
		if (auto c = o->getComponent<Physics2DComponent>(); c && c->runtime) {
			if (auto b = mSimulation->body(c->simulationId()); b && b->type == P::BodyType::Dynamic) {
				o->tryGetTransform()->setLocalPosition({ b->position.x, b->position.y, o->tryGetTransform()->getLocalPosition().z });
				o->tryGetTransform()->setLocalRotation(MATH::QuaternionF(0, 0, std::sin(b->angle / 2), std::cos(b->angle / 2)));
			}
		}
	}
}
#ifdef USE_EDITOR
void Physics2DSystem::drawDebug() {
	auto& world = *mSimulation;
	if (world.bodies().empty() && world.softBodies().empty() && world.fluids().empty()) {
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(850, 620), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("2D Physics")) {
		ImGui::End();
		return;
	}
	ImGui::Checkbox("Pause", &paused);
	ImGui::SameLine();
	if (ImGui::Button("Step")) {
		paused = true;
		singleStep = true;
	}
	ImGui::SameLine();
	ImGui::Checkbox("Sleeping", &world.settings.sleeping);
	ImGui::Text("Bodies: %zu  Joints: %zu  Soft: %zu  Fluids: %zu", world.bodies().size(), world.joints().size(), world.softBodies().size(),
		world.fluids().size());
	static float zoom = 55;
	static ImVec2 pan(0, 3);
	static bool showGrid = true;
	ImGui::SliderFloat("Pixels/unit", &zoom, 10, 160);
	ImGui::Checkbox("Spatial grid", &showGrid);
	ImGui::TextUnformatted("Drag with right mouse to pan. Red lines are contact normals.");
	ImVec2 size = ImGui::GetContentRegionAvail();
	size.x = std::max(size.x, 100.0f);
	size.y = std::max(size.y, 200.0f);
	ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("physics_canvas", size, ImGuiButtonFlags_MouseButtonRight);
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
		auto delta = ImGui::GetIO().MouseDelta;
		pan.x -= delta.x / zoom;
		pan.y += delta.y / zoom;
	}
	auto* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
	draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(22, 27, 36, 255));
	auto screen = [&](P::Vec2 p) { return ImVec2(origin.x + size.x / 2 + (p.x - pan.x) * zoom, origin.y + size.y / 2 - (p.y - pan.y) * zoom); };
	if (showGrid) {
		float cell = world.settings.cellSize;
		float left = pan.x - size.x / (2 * zoom), right = pan.x + size.x / (2 * zoom), bottom = pan.y - size.y / (2 * zoom),
			  top = pan.y + size.y / (2 * zoom);
		for (float x = std::floor(left / cell) * cell; x <= right; x += cell) {
			draw->AddLine(screen({ x, bottom }), screen({ x, top }), IM_COL32(42, 49, 62, 255));
		}
		for (float y = std::floor(bottom / cell) * cell; y <= top; y += cell) {
			draw->AddLine(screen({ left, y }), screen({ right, y }), IM_COL32(42, 49, 62, 255));
		}
	}
	auto colour = [](std::uint32_t c) { return IM_COL32(c >> 24, (c >> 16) & 255, (c >> 8) & 255, c & 255); };
	world.debugDraw({ [&](P::Vec2 a, P::Vec2 b, std::uint32_t c) { draw->AddLine(screen(a), screen(b), colour(c), 1.5f); },
		[&](P::Vec2 p, float radius, std::uint32_t c) { draw->AddCircle(screen(p), radius * zoom, colour(c), 20, 1.5f); } });
	draw->PopClipRect();
	ImGui::End();
}
#endif

#endif
