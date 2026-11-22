#include "particleSimulationSystem.h"

#include "coreModule/ecs/components/particleSystemComponent.h"
#include "coreModule/ecs/components/transform.h"
#include "coreModule/gui/components/spriteComponent.h"
#include "sceneModule/scene.h"
#include "utilsModule/containers/smallVector.h"

#include <algorithm>
#include <utility>
#include <variant>

#ifndef __EMSCRIPTEN__
#include "taskModule/taskSystem.h"
#include "utilsModule/serviceManager.h"

#include <array>
#include <cmath>
#include <exception>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>
#endif

namespace IKIGAI::ECS {
namespace {
	// Variant order is the tick order for multiple components on the same object.
	using ParticleTick = std::variant<ParticleSystem2DComponent*, ParticleSystem3DComponent*, SpriteParticleComponent*>;
	struct OrderedParticleTick {
		std::size_t objectIndex = 0;
		ParticleTick component = {};
	};
	constexpr std::size_t MinSimulatingInstances = 8;

#ifndef __EMSCRIPTEN__
	// Job submission and joining have a fixed cost. Require both work that can be
	// spread across several instances and enough currently live particles.
	constexpr std::size_t MinLiveParticles = 1024;
	constexpr std::size_t MaxParallelJobs = 8;

	struct ParticleUpdate {
		std::variant<PARTICLES::Runtime<2>*, PARTICLES::Runtime<3>*> runtime = {};
		MATH::Matrix4f transform = MATH::Matrix4f::Identity;
	};

	struct ParticleBatchWork {
		std::span<const ParticleUpdate> updates = {};
		std::size_t chunkSize = 0;
		double dt = 0.;
		std::array<std::exception_ptr, MaxParallelJobs> exceptions = {};
	};

	void executeParticleChunk(void* payload, std::size_t chunk, std::size_t) {
		auto& work = *static_cast<ParticleBatchWork*>(payload);
		const auto begin = chunk * work.chunkSize;
		const auto count = std::min(work.chunkSize, work.updates.size() - begin);
		try {
			for (const auto& update : work.updates.subspan(begin, count)) {
				std::visit([&](auto* runtime) { runtime->update(work.dt, update.transform); }, update.runtime);
			}
		} catch (...) {
			work.exceptions[chunk] = std::current_exception();
		}
	}

	bool runParticleBatch(TASK::TaskSystem& tasks, std::size_t workerCount, std::span<const ParticleUpdate> updates, double dt) {
		const auto laneCount = std::min(workerCount + 1, MaxParallelJobs);
		const auto chunkSize = 1 + (updates.size() - 1) / laneCount;
		const auto chunkCount = 1 + (updates.size() - 1) / chunkSize;
		ParticleBatchWork work { updates, chunkSize, dt };
		const TASK::JobDescriptor job { &executeParticleChunk, &work, "Update Scene Particles", TASK::JobAffinity::AnyCpu };
		auto batch = tasks.createJobBatch();
		TASK::JobBatchResult result = {};
		try {
			for (std::size_t chunk = 0; chunk < chunkCount; ++chunk) {
				if (!tasks.submit(batch, job, chunk, chunk + 1)) {
					if (chunk == 0) {
						return false;
					}
					throw std::runtime_error("Task system rejected part of a particle batch");
				}
			}
			tasks.close(batch);
			result = tasks.wait(batch);
		} catch (...) {
			// An accepted chunk may already have advanced its instances. Join before
			// the borrowed work goes out of scope and propagate the submission failure.
			tasks.cancel(batch);
			(void)tasks.wait(batch);
			throw;
		}

		// Contiguous chunks retain scene order for reporting simulation failures.
		for (const auto& exception : work.exceptions) {
			if (exception) {
				std::rethrow_exception(exception);
			}
		}
		if (result.status == TASK::JobBatchStatus::Failed) {
			std::rethrow_exception(result.exception);
		}
		if (result.status == TASK::JobBatchStatus::Cancelled) {
			throw std::runtime_error("Task system cancelled a particle batch");
		}
		return true;
	}
#endif
}

void ParticleSimulationSystem::update(SCENE_SYSTEM::Scene& scene, double dt) {
	if (scene.getObjects().empty()) {
		return;
	}

	UTILS::SmallVector<OrderedParticleTick, MinSimulatingInstances> ticks;
	auto& components = *scene.getWorld().getComponentManager();
	const auto gather = [&](Entity, auto& component) {
		const auto& owner = component.obj.get();
		if (const auto index = scene.getObjectIndex(owner); index && component.runtime() && owner.getIsActive()) {
			ticks.push_back({ *index, &component });
		}
	};
	components.forEach<ParticleSystem2DComponent>(gather);
	components.forEach<ParticleSystem3DComponent>(gather);
	components.forEach<SpriteParticleComponent>(gather);
	// Pool order can change after an archetype transition or row removal.
	std::ranges::sort(ticks, {}, [](const OrderedParticleTick& tick) { return std::pair { tick.objectIndex, tick.component.index() }; });

#ifndef __EMSCRIPTEN__
	const auto tryParallelUpdate = [&] {
		if (ticks.size() < MinSimulatingInstances || !std::isfinite(dt) || dt < 0. || !UTILS::ServiceManager::Check<TASK::TaskSystem>()) {
			return false;
		}
		auto& tasks = UTILS::ServiceManager::Get<TASK::TaskSystem>();
		const auto workerCount = tasks.getThreadCounts();
		if (workerCount == 0 || !tasks.isReady() || tasks.isFrameWorkerThread()) {
			return false;
		}

		std::size_t simulatingInstances = 0;
		std::size_t liveParticles = 0;
		for (const auto& tick : ticks) {
			// Legacy sprite particles rebuild their public mirror on the caller.
			if (std::holds_alternative<SpriteParticleComponent*>(tick.component)) {
				return false;
			}
			std::visit(
				[&](const auto* component) {
					const auto* runtime = component->runtime();
					if (runtime->state() == PARTICLES::State::Playing || runtime->state() == PARTICLES::State::Draining) {
						++simulatingInstances;
						for (const auto& emitter : runtime->emitters()) {
							liveParticles += std::min(emitter.particles.size(), MinLiveParticles - liveParticles);
						}
					}
				},
				tick.component);
		}
		if (simulatingInstances < MinSimulatingInstances || liveParticles < MinLiveParticles) {
			return false;
		}

		std::vector<ParticleUpdate> updates;
		updates.reserve(ticks.size());
		for (const auto& tick : ticks) {
			std::visit(
				[&](auto* component) {
					using Component = std::remove_pointer_t<decltype(component)>;
					if constexpr (!std::is_same_v<Component, SpriteParticleComponent>) {
						updates.push_back({ component->mRuntime.get(), component->obj->getTransform()->getWorldMatrix() });
					}
				},
				tick.component);
		}
		// The phase owns the scene until the join. Jobs borrow independent
		// runtimes and read the matrices copied on the caller.
		return runParticleBatch(tasks, workerCount, updates, dt);
	};
	if (tryParallelUpdate()) {
		return;
	}
#endif
	for (const auto& tick : ticks) {
		std::visit([dt](auto* component) { component->tick(dt); }, tick.component);
	}
}
}
