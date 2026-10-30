#include "destructionSystem.h"
#if USE_PHYSICS_3D
#include "collisionModule/convexCollision.h"
#include "coreModule/ecs/components/destructibleMeshComponent.h"
#include "coreModule/ecs/components/materialRenderer.h"
#include "coreModule/ecs/components/modelRenderer.h"
#include "coreModule/ecs/components/physics3dComponent.h"
#include "coreModule/ecs/components/physicsComponent.h"
#include "coreModule/ecs/components/ragdollComponent.h"
#include "coreModule/ecs/components/skeletalComponent.h"
#include "coreModule/ecs/object.h"
#include "profilerModule/profiler.h"
#include "profilerModule/profilerSpy.h"
#include "renderModule/backends/meshFactory.h"
#include "sceneModule/scene.h"
#include <algorithm>
#include <map>

namespace IKIGAI::ECS {
namespace {
	std::shared_ptr<Object> renderObject(SCENE_SYSTEM::Scene& scene, const DESTRUCTION::DestructiblePiece& piece,
		const std::vector<std::shared_ptr<RENDER::MaterialInterface>>& materials) {
		auto profile = PROFILER::Profiler::IsEnabled() ? std::make_unique<PROFILER::ProfilerSpy>("Destruction::Upload") : nullptr;
		auto model = RENDER::CreateEmptyModel();
		if (!model) {
			throw std::runtime_error("Cannot allocate fracture model");
		}
		std::map<unsigned, std::vector<unsigned>> indices {};
		for (size_t triangle = 0; triangle < piece.mesh.materials.size(); ++triangle) {
			auto& group = indices[piece.mesh.materials[triangle]];
			for (int corner = 0; corner < 3; ++corner) {
				group.push_back(piece.mesh.indices[triangle * 3 + corner]);
			}
		}
		for (const auto& [material, triangles] : indices) {
			auto mesh = RENDER::CreateMesh(piece.mesh.vertices, triangles, material);
			if (!mesh) {
				throw std::runtime_error("Cannot upload fracture mesh");
			}
			model->getMeshes().push_back(std::move(mesh));
		}
		model->computeBoundingSphere();
		// Instance-owned entities participate in normal ECS draw queries but never serialize as scene content.
		auto object = std::make_shared<Object>(scene.getWorld().createEntity(), "Destructible piece", "", &scene.getWorld());
		object->setActive(false);
		auto renderer = object->addComponent<ModelRenderer>();
		renderer->setModel(model);
		auto materialRenderer = object->addComponent<MaterialRenderer>();
		for (unsigned index = 0; index < materials.size(); ++index) {
			materialRenderer->setMaterial(index, materials[index]);
		}
		return object;
	}
	void dispose(SCENE_SYSTEM::Scene& scene, const std::shared_ptr<Object>& object) {
		if (auto renderer = object->getComponent<ModelRenderer>()) {
			scene.removeFromBVH(renderer.get());
		}
		object->dispose();
	}
}
void DestructionSystem::beforePhysics(SCENE_SYSTEM::Scene& scene, float) {
	auto profile = PROFILER::Profiler::IsEnabled() ? std::make_unique<PROFILER::ProfilerSpy>("Destruction::beforePhysics") : nullptr;
	auto world = scene.physics3dShared();
	unsigned cutsRemaining = 4;
	for (auto& object : scene.getObjects()) {
		auto component = object->getComponent<DestructibleMeshComponent>();
		if (!component) {
			continue;
		}
		auto& c = *component;
		if (!object->getIsActive()) {
			c.release();
			continue;
		}
		c.mScene = &scene;
		c.mSceneLifetime = scene.lifetime;
		try {
			if (c.mReset || (c.mInstance && (c.mPath != c.settings.Definition || !c.mInstance->valid()))) {
				c.release();
			}
			if (!c.mInstance && !c.settings.Definition.empty()) {
				if (object->hasParent() || (object->mTransform->getWorldScale() - MATH::Vector3f(1)).magnitude() > 1e-5f) {
					throw std::runtime_error("Destructible requires a root with unit scale");
				}
				if (object->getComponent<Physics3DComponent>() || object->getComponent<PhysicsComponent>() || object->getComponent<RagdollComponent>()
					|| object->getComponent<SoftBody3DComponent>() || object->getComponent<Cloth3DComponent>()
					|| object->getComponent<Fluid3DComponent>() || object->getComponent<SkeletalComponent>() || object->getComponent<ModelRenderer>()
					|| object->getComponent<ModelLODRenderer>()) {
					throw std::runtime_error("Destructible owns its render/physics pieces; use a root without other model or physics components");
				}
				c.mResource = RESOURCES::ServiceManager::Get<RESOURCES::DestructibleLoader>().loadResource(c.settings.Definition);
				c.mMaterials = c.mResource->materials;
				c.mInstance = std::make_shared<DESTRUCTION::DestructibleInstance>(c.mResource->definition);
				c.mInstance->initialize(world, object->mTransform->getWorldPosition(), object->mTransform->getWorldRotation());
				const auto& piece = c.mInstance->pieces().front();
				c.mRenderPieces.push_back({ piece.id, renderObject(scene, piece, c.mMaterials) });
				c.mPath = c.settings.Definition;
				c.mError.clear();
			}
			if (!c.mInstance || c.mRequests.empty() || !cutsRemaining) {
				continue;
			}
			auto request = c.mRequests.front();
			c.mRequests.erase(c.mRequests.begin());
			--cutsRemaining;
			if (request.piece == 0) {
				float best = std::numeric_limits<float>::max();
				for (const auto& piece : c.mInstance->pieces()) {
					const auto* body = world->body(piece.body);
					float distance = (body->getPosition() - request.point).squareMagnitude();
					if (distance < best) {
						best = distance;
						request.piece = piece.id;
					}
				}
			}
			std::vector<DestructibleMeshComponent::RenderPiece> candidates {};
			c.mRenderPieces.reserve(c.mInstance->definition().settings.MaximumPieces);
			c.mStatus = c.mInstance->slice(request, [&](std::span<const DESTRUCTION::DestructiblePiece> pieces) {
				for (const auto& piece : pieces) {
					candidates.push_back({ piece.id, renderObject(scene, piece, c.mMaterials) });
				}
			});
			c.mError = c.mInstance->lastError();
			if (c.mStatus == DESTRUCTION::SliceStatus::Split) {
				std::erase_if(c.mRenderPieces, [&](auto& render) {
					if (render.id != request.piece) {
						return false;
					}
					dispose(scene, render.object);
					return true;
				});
				for (auto& render : candidates) {
					c.mRenderPieces.push_back(std::move(render));
				}
			}
			else {
				for (auto& render : candidates) {
					dispose(scene, render.object);
				}
			}
		} catch (const std::exception& error) {
			c.mError = error.what();
			if (c.mRenderPieces.empty()) {
				c.release();
			}
		}
	}
}
void DestructionSystem::afterPhysics(SCENE_SYSTEM::Scene& scene, float dt) {
	auto profile = PROFILER::Profiler::IsEnabled() ? std::make_unique<PROFILER::ProfilerSpy>("Destruction::afterPhysics") : nullptr;
	auto world = scene.physics3dShared();
	for (auto& object : scene.getObjects()) {
		auto component = object->getComponent<DestructibleMeshComponent>();
		if (!component || !component->mInstance) {
			continue;
		}
		auto& c = *component;
		c.mInstance->advance(dt);
		c.mImpactCooldown = std::max(0.f, c.mImpactCooldown - dt);
		std::erase_if(c.mRenderPieces, [&](auto& render) {
			auto found = std::find_if(c.pieces().begin(), c.pieces().end(), [&](const auto& piece) { return piece.id == render.id; });
			if (found == c.pieces().end()) {
				dispose(scene, render.object);
				return true;
			}
			const auto* body = world->body(found->body);
			render.object->mTransform->setLocalPosition(body->getPosition());
			render.object->mTransform->setLocalRotation(body->getOrientation());
			render.object->setActive(true);
			render.object->setVisible(object->getIsVisible());
			scene.updateInBVH(render.object->getComponent<ModelRenderer>().get());
			return false;
		});
		if (!c.settings.ImpactDamage || c.mImpactCooldown > 0) {
			continue;
		}
		const auto& settings = c.mInstance->definition().settings;
		for (const auto& impact : world->impacts()) {
			if (impact.closingSpeed < settings.ImpactSpeed || impact.normalImpulse < settings.ImpactImpulse) {
				continue;
			}
			auto found = std::find_if(
				c.pieces().begin(), c.pieces().end(), [&](const auto& piece) { return piece.body == impact.bodyA || piece.body == impact.bodyB; });
			if (found == c.pieces().end()) {
				continue;
			}
			// Cut through an interior point. A tangent plane through the hit alone would often produce NoSplit.
			auto* body = world->body(found->body);
			c.slicePiece(found->id, (impact.point + body->getPosition()) * .5f, impact.normal);
			c.mImpactCooldown = .2f;
			break;
		}
	}
}
void DestructionSystem::radialDamage(SCENE_SYSTEM::Scene& scene, MATH::Vector3f center, float radius, float damage, MATH::Vector3f normal) {
	if (!PHYSICS_COMMON::finite(center) || !std::isfinite(radius) || radius <= 0 || !std::isfinite(damage) || damage < 0) {
		throw std::invalid_argument("Invalid radial damage");
	}
	for (auto& object : scene.getObjects()) {
		if (auto c = object->getComponent<DestructibleMeshComponent>(); c && c->mInstance) {
			float nearest = radius;
			for (const auto& piece : c->pieces()) {
				auto* body = scene.physics3d().body(piece.body);
				if (!body) {
					continue;
				}
				auto local = MATH::QuaternionF::Conjugate(body->getOrientation()) * (center - body->getPosition());
				float distance = COLLISION::convexSegmentSeparation(*body->shape.polyhedron, local, local, 0).gap;
				nearest = std::min(nearest, std::max(0.f, distance));
			}
			if (nearest < radius) {
				c->applyDamage(damage * (1 - nearest / radius), center, normal);
			}
		}
	}
}
}
#endif
