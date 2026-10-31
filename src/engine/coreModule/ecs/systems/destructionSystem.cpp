#include "destructionSystem.h"
#if USE_PHYSICS_3D
#include "collisionModule/convexCollision.h"
#include "coreModule/ecs/components/chunkModelRenderer.h"
#include "coreModule/ecs/components/destructibleMeshComponent.h"
#include "coreModule/ecs/components/materialRenderer.h"
#include "coreModule/ecs/components/modelRenderer.h"
#include "coreModule/ecs/components/physics3dComponent.h"
#include "coreModule/ecs/components/physicsComponent.h"
#include "coreModule/ecs/components/ragdollComponent.h"
#include "coreModule/ecs/components/skeletalComponent.h"
#include "coreModule/ecs/components/surfaceComponent.h"
#include "coreModule/ecs/object.h"
#include "profilerModule/profiler.h"
#include "profilerModule/profilerSpy.h"
#include "renderModule/backends/meshFactory.h"
#include "sceneModule/scene.h"
#include "taskModule/taskSystem.h"
#include <algorithm>
#include <map>

namespace IKIGAI::ECS {
namespace {
	std::shared_ptr<Object> renderObject(SCENE_SYSTEM::Scene& scene, const std::vector<std::shared_ptr<RENDER::MaterialInterface>>& materials) {
		auto model = RENDER::CreateEmptyModel();
		if (!model) {
			throw std::runtime_error("Cannot allocate fracture model");
		}
		auto object = std::make_shared<Object>(scene.getWorld().createEntity(), "Destructible piece", "", &scene.getWorld());
		object->setActive(false);
		try {
			object->addComponent<ModelRenderer>()->setModel(model);
			auto materialRenderer = object->addComponent<MaterialRenderer>();
			for (unsigned index = 0; index < materials.size(); ++index) {
				materialRenderer->setMaterial(index, materials[index]);
			}
		} catch (...) {
			object->dispose();
			throw;
		}
		return object;
	}
	std::size_t batchBytes(const DESTRUCTION::PreparedPiece::UploadBatch& batch) {
		return batch.vertices.size() * sizeof(Vertex) + batch.indices.size() * sizeof(unsigned);
	}
	void upload(Object& object, const DESTRUCTION::PreparedPiece::UploadBatch& batch) {
		auto mesh = RENDER::CreateMesh(batch.vertices, batch.indices, batch.material);
		if (!mesh) {
			throw std::runtime_error("Cannot upload fracture mesh");
		}
		object.getComponent<ModelRenderer>()->getModel()->getMeshes().push_back(std::move(mesh));
	}
	double milliseconds(std::chrono::steady_clock::time_point start) {
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	}
	void dispose(SCENE_SYSTEM::Scene& scene, const std::shared_ptr<Object>& object) {
		if (auto renderer = object->getComponent<ModelRenderer>()) {
			scene.removeFromBVH(renderer.get());
		}
		object->dispose();
	}
}
void DestructionSystem::beginFrame(SCENE_SYSTEM::Scene& scene) {
	if (scene.destructionRuntime) {
		scene.destructionRuntime->beginFrame();
	}
}
void DestructionSystem::beforePhysics(SCENE_SYSTEM::Scene& scene, float) {
	auto profile = PROFILER::Profiler::IsEnabled() ? std::make_unique<PROFILER::ProfilerSpy>("Destruction::beforePhysics") : nullptr;
	auto world = scene.physics3dShared();
	const auto objects = scene.getObjects();
	if (objects.empty()) {
		return;
	}
	if (!scene.destructionRuntime) {
		bool any = false;
		for (auto& object : objects) {
			any = any || bool(object->getComponent<DestructibleMeshComponent>());
		}
		if (!any) {
			return;
		}
		scene.destructionSettings.validate();
		scene.destructionRuntime = std::make_shared<DESTRUCTION::DestructionRuntime>([](std::function<void()> work) {
			auto& tasks = RESOURCES::ServiceManager::Get<TASK::TaskSystem>();
			if (!tasks.isReady()) {
				throw std::runtime_error("Background executor is not initialized");
			}
			tasks.dispatch("Destruction::Prepare", std::move(work));
		});
	}
	auto& runtime = *scene.destructionRuntime;
	auto& statistics = runtime.statistics;
	const auto& limits = scene.destructionSettings;
	const auto started = std::chrono::steady_clock::now();
	const auto available = [&] {
		return runtime.operations < limits.MaximumOperationsPerFrame
			&& (runtime.operations == 0 || statistics.mainThreadMilliseconds + milliseconds(started) < limits.MaximumMainThreadMilliseconds);
	};
	const auto startIndex = runtime.cursor % objects.size();
	for (std::size_t offset = 0; offset < objects.size(); ++offset) {
		const auto index = (startIndex + offset) % objects.size();
		auto& object = objects[index];
		auto component = object->getComponent<DestructibleMeshComponent>();
		if (!component) {
			continue;
		}
		auto& c = *component;
		c.mScene = &scene;
		c.mSceneLifetime = scene.lifetime;
		c.mRuntime = scene.destructionRuntime;
		if (!object->getIsActive()) {
			if (c.mInstance || c.pendingRequests()) {
				c.release();
			}
			continue;
		}
		if (c.mReset || c.mPath != c.settings.Definition || (c.mInstance && !c.mInstance->valid())) {
			// Preserve requests accepted before the first initialization; subsequent lifecycle changes cancel them.
			if (!c.mPath.empty() || c.mReset || c.mInstance) {
				c.release();
			}
			c.mFailed = false;
			c.mPath = c.settings.Definition;
		}
		for (auto& render : c.mRenderPieces) {
			const auto found = std::find_if(c.pieces().begin(), c.pieces().end(), [&](const auto& piece) { return piece.id == render.id; });
			if (found == c.pieces().end()) {
				continue;
			}
			auto* body = world->body(found->body);
			render.previousPosition = body->getPosition();
			render.previousRotation = body->getOrientation();
			render.currentPosition = render.previousPosition;
			render.currentRotation = render.previousRotation;
		}

		if (c.mInstance
			&& (object->hasParent() || (object->mTransform->getWorldScale() - MATH::Vector3f(1)).magnitude() > 1e-5f
				|| object->getComponent<SurfaceComponent>() || object->getComponent<ChunkModelRenderer>() || object->getComponent<ModelRenderer>()
				|| object->getComponent<ModelLODRenderer>() || object->getComponent<SkeletalComponent>() || object->getComponent<Physics3DComponent>()
				|| object->getComponent<PhysicsComponent>() || object->getComponent<RagdollComponent>() || object->getComponent<SoftBody3DComponent>()
				|| object->getComponent<Cloth3DComponent>() || object->getComponent<Fluid3DComponent>())) {
			c.release(false);
			c.mFailed = true;
			c.mStatus = DESTRUCTION::SliceStatus::UnsupportedMesh;
			c.mError = "Destructible requires a root with unit scale and exclusive render/physics ownership";
		}
		if (c.mFailed || c.settings.Definition.empty()) {
			continue;
		}
		if (!available()) {
			continue;
		}
		struct AdvanceCursor {
			DESTRUCTION::DestructionRuntime& runtime;
			std::size_t next = 0;
			unsigned previousOperations = 0;
			~AdvanceCursor() {
				if (runtime.operations != previousOperations) {
					runtime.cursor = next;
				}
			}
		} advance { runtime, (index + 1) % objects.size(), runtime.operations };
		try {
			if (!c.mInstance) {
				++runtime.operations;
				if (object->hasParent() || (object->mTransform->getWorldScale() - MATH::Vector3f(1)).magnitude() > 1e-5f) {
					throw std::runtime_error("Destructible requires a root with unit scale");
				}
				if (object->getComponent<Physics3DComponent>() || object->getComponent<PhysicsComponent>() || object->getComponent<RagdollComponent>()
					|| object->getComponent<SoftBody3DComponent>() || object->getComponent<Cloth3DComponent>()
					|| object->getComponent<Fluid3DComponent>() || object->getComponent<SkeletalComponent>() || object->getComponent<ModelRenderer>()
					|| object->getComponent<ModelLODRenderer>() || object->getComponent<SurfaceComponent>()
					|| object->getComponent<ChunkModelRenderer>()) {
					throw std::runtime_error("Destructible requires exclusive ownership of root render and physics representation");
				}
				if (world->bodies().size() >= limits.MaximumBodies) {
					throw std::runtime_error("Scene destruction body budget exceeded");
				}
				c.mResource = RESOURCES::ServiceManager::Get<RESOURCES::DestructibleLoader>().loadResource(c.settings.Definition);
				c.mMaterials = c.mResource->materials;
				auto instance = std::make_shared<DESTRUCTION::DestructibleInstance>(c.mResource->definition);
				const auto topologyBytes = DESTRUCTION::meshBytes(c.mResource->definition->mesh);
				if (topologyBytes > limits.MaximumWorkingBytes || statistics.topologyBytes > limits.MaximumWorkingBytes - topologyBytes
					|| runtime.budget->bytes.load() > limits.MaximumWorkingBytes - topologyBytes - statistics.topologyBytes) {
					throw std::runtime_error("Initial mesh exceeds scene CPU budget");
				}
				auto batches = DESTRUCTION::prepareUploads(c.mResource->definition->mesh);
				std::size_t bytes = 0;
				for (const auto& batch : batches) {
					bytes += batchBytes(batch);
				}
				if (bytes > limits.MaximumGpuBytes || statistics.gpuBytes > limits.MaximumGpuBytes - bytes) {
					throw std::runtime_error("Initial destructible exceeds scene GPU budget");
				}
				// Initial assets are preloaded; root upload is bounded by the same per-frame byte cap.
				if (bytes > limits.MaximumUploadBytesPerFrame) {
					throw std::runtime_error("Initial mesh exceeds per-frame upload budget");
				}
				if (statistics.uploadedBytes > limits.MaximumUploadBytesPerFrame - bytes) {
					continue;
				}
				c.mRenderPieces.reserve(instance->definition().settings.MaximumPieces);
				DestructibleMeshComponent::RenderPiece render {};
				render.object = renderObject(scene, c.mMaterials);
				try {
					const auto uploadStart = std::chrono::steady_clock::now();
					for (const auto& batch : batches) {
						upload(*render.object, batch);
					}
					render.object->getComponent<ModelRenderer>()->getModel()->computeBoundingSphere();
					statistics.uploadMilliseconds += milliseconds(uploadStart);
					instance->initialize(world, object->mTransform->getWorldPosition(), object->mTransform->getWorldRotation(), limits.MaximumBodies);
					render.id = instance->pieces().front().id;
				} catch (...) {
					dispose(scene, render.object);
					throw;
				}
				render.gpuBytes = bytes;
				statistics.gpuBytes += bytes;
				statistics.uploadedBytes += bytes;
				c.mRenderPieces.push_back(std::move(render));
				c.mInstance = std::move(instance);
				c.mTopologyBytes = topologyBytes;
				statistics.topologyBytes += topologyBytes;
				++scene.renderTopologyRevision;
				for (auto& pending : c.mRequests) {
					if (pending.rootSpace) {
						pending.slice.piece = c.pieces().front().id;
						pending.slice.point -= c.mInstance->definition().center;
						pending.slice.localSpace = true;
						pending.rootSpace = false;
					}
				}
				auto* body = world->body(c.mInstance->pieces().front().body);
				auto& initialRender = c.mRenderPieces.back();
				initialRender.previousPosition = initialRender.currentPosition = body->getPosition();
				initialRender.previousRotation = initialRender.currentRotation = body->getOrientation();
				c.mError.clear();
			}
			if (!c.mJob && !c.mRequests.empty() && available()) {
				auto& pending = c.mRequests.front();
				auto& request = pending.slice;
				if (pending.rootSpace) {
					const auto& piece = c.pieces().front();
					request.piece = piece.id;
					request.point -= c.mInstance->definition().center;
					request.localSpace = true;
					pending.rootSpace = false;
				}
				const auto status = c.mInstance->bind(request);
				if (status != DESTRUCTION::SliceStatus::Split) {
					c.complete(pending, status);
					c.mRequests.pop_front();
					++runtime.operations;
				}
				else {
					const auto found
						= std::find_if(c.pieces().begin(), c.pieces().end(), [&](const auto& piece) { return piece.id == request.piece; });
					const auto& settings = c.mInstance->definition().settings;
					if (c.pieces().size() >= settings.MaximumPieces || found->depth >= settings.MaximumDepth
						|| world->bodies().size() >= limits.MaximumBodies) {
						c.complete(pending, DESTRUCTION::SliceStatus::BudgetExceeded);
						c.mRequests.pop_front();
						++runtime.operations;
					}
					else if (runtime.budget->jobs.load() < limits.MaximumJobs) {
						auto snapshot = c.mInstance->snapshot(request);
						const auto reservation = DESTRUCTION::meshBytes(snapshot.mesh)
							+ std::size_t(snapshot.settings.maximumTriangles) * 3 * (sizeof(Vertex) + 64) * 16;
						if (reservation > limits.MaximumWorkingBytes || statistics.topologyBytes > limits.MaximumWorkingBytes - reservation) {
							c.complete(pending, DESTRUCTION::SliceStatus::BudgetExceeded, "Fracture exceeds CPU working budget");
							c.mRequests.pop_front();
							++runtime.operations;
						}
						else if (auto job = runtime.submit(std::move(snapshot), limits)) {
							c.mActive = pending;
							c.mRequests.pop_front();
							c.mJob = std::move(job);
							c.mUploadPiece = 0;
							c.mUploadBatch = 0;
							++runtime.operations;
						}
					}
				}
			}
			if (!c.mJob || !c.mJob->done.load(std::memory_order_acquire) || !available()) {
				continue;
			}
			auto& prepared = c.mJob->result;
			if (prepared.status != DESTRUCTION::SliceStatus::Split) {
				c.complete(*c.mActive, prepared.status, prepared.error);
				++statistics.rejected;
				c.mActive.reset();
				c.mJob.reset();
				++runtime.operations;
				continue;
			}
			c.mJob->stage.store(DESTRUCTION::RequestStage::Upload);
			if (c.mCandidates.empty()) {
				std::size_t bytes = 0;
				for (const auto& piece : prepared.pieces) {
					for (const auto& batch : piece.uploads) {
						bytes += batchBytes(batch);
					}
				}
				if (bytes > limits.MaximumGpuBytes || statistics.gpuBytes > limits.MaximumGpuBytes - bytes) {
					throw std::runtime_error("Fracture candidates exceed scene GPU budget");
				}
				c.mCandidates.reserve(2);
				for (unsigned side = 0; side < 2; ++side) {
					DestructibleMeshComponent::RenderPiece render {};
					render.object = renderObject(scene, c.mMaterials);
					c.mCandidates.push_back(std::move(render));
				}
			}
			while (c.mUploadPiece < 2 && available()) {
				auto& piece = prepared.pieces[c.mUploadPiece];
				if (c.mUploadBatch == piece.uploads.size()) {
					c.mCandidates[c.mUploadPiece].object->getComponent<ModelRenderer>()->getModel()->computeBoundingSphere();
					++c.mUploadPiece;
					c.mUploadBatch = 0;
					continue;
				}
				const auto& batch = piece.uploads[c.mUploadBatch];
				const auto bytes = batchBytes(batch);
				if (bytes > limits.MaximumUploadBytesPerFrame - statistics.uploadedBytes) {
					break;
				}
				if (bytes > limits.MaximumGpuBytes - statistics.gpuBytes) {
					throw std::runtime_error("Scene GPU budget exhausted during upload");
				}
				const auto uploadStart = std::chrono::steady_clock::now();
				upload(*c.mCandidates[c.mUploadPiece].object, batch);
				statistics.uploadMilliseconds += milliseconds(uploadStart);
				statistics.uploadedBytes += bytes;
				statistics.gpuBytes += bytes;
				c.mCandidates[c.mUploadPiece].gpuBytes += bytes;
				++c.mUploadBatch;
				++runtime.operations;
			}
			if (c.mUploadPiece != 2 || !available()) {
				continue;
			}
			c.mJob->stage.store(DESTRUCTION::RequestStage::Ready);
			const auto commitStart = std::chrono::steady_clock::now();
			const auto status = c.mInstance->commit(
				std::move(prepared), c.mActive->slice,
				[&](std::span<const DESTRUCTION::DestructiblePiece> pieces) {
					for (unsigned side = 0; side < 2; ++side) {
						c.mCandidates[side].id = pieces[side].id;
					}
				},
				limits.MaximumBodies);
			statistics.commitMilliseconds += milliseconds(commitStart);
			++runtime.operations;
			if (status == DESTRUCTION::SliceStatus::Split) {
				const auto source = c.mActive->slice.piece;
				std::erase_if(c.mRenderPieces, [&](auto& render) {
					if (render.id != source) {
						return false;
					}
					statistics.gpuBytes -= render.gpuBytes;
					dispose(scene, render.object);
					return true;
				});
				for (auto& candidate : c.mCandidates) {
					c.mRenderPieces.push_back(std::move(candidate));
				}
				c.mCandidates.clear();
				statistics.topologyBytes -= c.mTopologyBytes;
				c.mTopologyBytes = 0;
				for (const auto& piece : c.pieces()) {
					c.mTopologyBytes += DESTRUCTION::meshBytes(piece.mesh);
				}
				statistics.topologyBytes += c.mTopologyBytes;
				++statistics.committed;
				++scene.renderTopologyRevision;
			}
			else {
				++statistics.rejected;
			}
			c.complete(*c.mActive, status, c.mInstance->lastError());
			c.mActive.reset();
			c.mJob.reset();
			for (auto& candidate : c.mCandidates) {
				statistics.gpuBytes -= candidate.gpuBytes;
				dispose(scene, candidate.object);
			}
			c.mCandidates.clear();
			for (auto& render : c.mRenderPieces) {
				if (!render.poseReady) {
					const auto found = std::find_if(c.pieces().begin(), c.pieces().end(), [&](const auto& piece) { return piece.id == render.id; });
					auto* body = world->body(found->body);
					render.previousPosition = render.currentPosition = body->getPosition();
					render.previousRotation = render.currentRotation = body->getOrientation();
				}
			}
		} catch (const std::exception& error) {
			const std::string message = error.what();
			if (c.mActive) {
				c.complete(*c.mActive, DESTRUCTION::SliceStatus::UnsupportedMesh, message);
				c.mActive.reset();
				if (c.mJob) {
					c.mJob->cancelled.store(true);
				}
				c.mJob.reset();
				for (auto& candidate : c.mCandidates) {
					statistics.gpuBytes -= candidate.gpuBytes;
					dispose(scene, candidate.object);
				}
				c.mCandidates.clear();
				++statistics.rejected;
			}
			else {
				for (const auto& pending : c.mRequests) {
					c.complete(pending, DESTRUCTION::SliceStatus::UnsupportedMesh, message);
				}
				c.mRequests.clear();
				c.release(false);
				c.mFailed = true;
				c.mError = message;
				c.mStatus = DESTRUCTION::SliceStatus::UnsupportedMesh;
			}
		}
	}
	statistics.mainThreadMilliseconds += milliseconds(started);
	statistics.workingBytes = runtime.budget->bytes.load();
	statistics.activeJobs = runtime.budget->jobs.load();
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
		if (scene.destructionRuntime) {
			auto& statistics = scene.destructionRuntime->statistics;
			statistics.topologyBytes -= c.mTopologyBytes;
			c.mTopologyBytes = 0;
			for (const auto& piece : c.pieces()) {
				c.mTopologyBytes += DESTRUCTION::meshBytes(piece.mesh);
			}
			statistics.topologyBytes += c.mTopologyBytes;
		}
		c.mImpactCooldown = std::max(0.f, c.mImpactCooldown - dt);
		std::erase_if(c.mRenderPieces, [&](auto& render) {
			auto found = std::find_if(c.pieces().begin(), c.pieces().end(), [&](const auto& piece) { return piece.id == render.id; });
			if (found == c.pieces().end()) {
				if (scene.destructionRuntime) {
					scene.destructionRuntime->statistics.gpuBytes -= render.gpuBytes;
				}
				++scene.renderTopologyRevision;
				dispose(scene, render.object);
				return true;
			}
			const auto* body = world->body(found->body);
			render.currentPosition = body->getPosition();
			render.currentRotation = body->getOrientation();
			render.object->setActive(true);
			render.object->setVisible(object->getIsVisible());
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
			c.mImpactCooldown = scene.destructionSettings.ImpactCooldown;
			break;
		}
	}
}
void DestructionSystem::present(SCENE_SYSTEM::Scene& scene, float alpha) {
	alpha = std::clamp(alpha, 0.f, 1.f);
	for (auto& object : scene.getObjects()) {
		auto c = object->getComponent<DestructibleMeshComponent>();
		if (!c || !c->mInstance || !object->getIsActive()) {
			continue;
		}
		for (auto& render : c->mRenderPieces) {

			auto& transform = *render.object->mTransform;
			const auto previous = transform.getWorldMatrix();
			const auto position = render.previousPosition * (1 - alpha) + render.currentPosition * alpha;
			const auto rotation = MATH::QuaternionF::Slerp(render.previousRotation, render.currentRotation, alpha);
			const auto oldPosition = transform.getWorldPosition();
			const auto oldRotation = transform.getWorldRotation();
			if ((oldPosition - position).squareMagnitude() > 1e-16f || oldRotation.x != rotation.x || oldRotation.y != rotation.y
				|| oldRotation.z != rotation.z || oldRotation.w != rotation.w) {
				transform.setLocalPosition(position);
				transform.setLocalRotation(rotation);
				scene.updateInBVH(render.object->getComponent<ModelRenderer>().get());
			}
			transform.getTransform().setPrevWorldMatrix(render.poseReady ? previous : transform.getWorldMatrix());
			render.poseReady = true;
			render.object->setVisible(object->getIsVisible());
		}
	}
}
void DestructionSystem::radialDamage(SCENE_SYSTEM::Scene& scene, MATH::Vector3f center, float radius, float damage, MATH::Vector3f normal) {
	if (!PHYSICS_COMMON::finite(center) || !std::isfinite(radius) || radius <= 0 || !std::isfinite(damage) || damage < 0
		|| !PHYSICS_COMMON::finite(normal) || normal.squareMagnitude() < 1e-12f) {
		throw std::invalid_argument("Invalid radial damage");
	}
	MATH::GEOMETRY::Aabb<3> bounds {};
	for (unsigned axis = 0; axis < 3; ++axis) {
		bounds.min[axis] = center[axis] - radius;
		bounds.max[axis] = center[axis] + radius;
	}
	const auto targets = scene.physics3d().queryBodies(bounds);
	for (auto& object : scene.getObjects()) {
		if (auto c = object->getComponent<DestructibleMeshComponent>(); c && c->mInstance && object->getIsActive() && c->mInstance->valid()) {
			float nearest = radius;
			std::uint64_t nearestPiece = 0;
			MATH::Vector3f cutPoint {};
			for (const auto& piece : c->pieces()) {
				if (!std::binary_search(targets.begin(), targets.end(), piece.body)) {
					continue;
				}
				auto* body = scene.physics3d().body(piece.body);
				if (!body) {
					continue;
				}
				auto local = MATH::QuaternionF::Conjugate(body->getOrientation()) * (center - body->getPosition());
				const auto separation = COLLISION::convexSegmentSeparation(*body->shape.polyhedron, local, local, 0);
				const float distance = std::max(0.f, separation.gap);
				if (distance < nearest || (distance == nearest && nearestPiece && piece.id < nearestPiece)) {
					nearest = distance;
					nearestPiece = piece.id;
					cutPoint = (body->getPosition() + body->getPointInWorldSpace(separation.point)) * .5f;
				}
			}
			if (nearest < radius) {
				c->applyDamagePiece(nearestPiece, damage * (1 - nearest / radius), cutPoint, normal);
			}
		}
	}
}
}
#endif
