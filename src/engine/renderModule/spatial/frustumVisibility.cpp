#include "frustumVisibility.h"

#include <algorithm>
#include <stdexcept>

#include "profilerModule/trace.h"
#include "renderModule/frustum.h"
#include "taskModule/taskSystem.h"

namespace IKIGAI::RENDER {
namespace {
	struct CullWork {
		Frustum frustum;
		std::span<const FrustumModelQuery> models;
		std::span<const BoundingSphere> meshes;
		std::span<std::uint8_t> visibleModels;
		std::span<std::uint8_t> visibleMeshes;
	};

	void cullRange(void* payload, std::size_t begin, std::size_t end) {
		IKIGAI_PROFILE_SCOPE("Scene Frustum Cull Range");
		const auto& work = *static_cast<const CullWork*>(payload);
		for (std::size_t index = begin; index < end; ++index) {
			const auto& model = work.models[index];
			const bool visible = work.frustum.boundingSphereInFrustum(model.bounds, model.position, model.rotation, model.scale);
			work.visibleModels[index] = visible;
			if (!visible) {
				continue;
			}
			for (std::size_t mesh = model.firstMesh; mesh < model.firstMesh + model.meshCount; ++mesh) {
				work.visibleMeshes[mesh] = work.frustum.boundingSphereInFrustum(work.meshes[mesh], model.position, model.rotation, model.scale);
			}
		}
	}
}

FrustumVisibility evaluateFrustumVisibility(
	const Frustum& frustum, std::span<const FrustumModelQuery> models, std::span<const BoundingSphere> meshes, TASK::TaskSystem* tasks) {
	IKIGAI_PROFILE_SCOPE("Evaluate Scene Frustum Visibility");
	FrustumVisibility visibility { std::vector<std::uint8_t>(models.size()), std::vector<std::uint8_t>(meshes.size()) };
	evaluateFrustumVisibility(frustum, models, meshes, visibility.models, visibility.meshes, tasks);
	return visibility;
}

void evaluateFrustumVisibility(const Frustum& frustum, std::span<const FrustumModelQuery> models, std::span<const BoundingSphere> meshes,
	std::span<std::uint8_t> visibleModels, std::span<std::uint8_t> visibleMeshes, TASK::TaskSystem* tasks) {
	IKIGAI_PROFILE_SCOPE("Evaluate Scene Frustum Visibility Into");
	if (visibleModels.size() != models.size() || visibleMeshes.size() != meshes.size()) {
		throw std::invalid_argument("Frustum output spans must match input counts");
	}
	std::fill(visibleModels.begin(), visibleModels.end(), std::uint8_t {});
	std::fill(visibleMeshes.begin(), visibleMeshes.end(), std::uint8_t {});
	CullWork work { frustum, models, meshes, visibleModels, visibleMeshes };
	if (!tasks || !tasks->isReady() || models.size() < 2) {
		cullRange(&work, 0, models.size());
		return;
	}

	const auto laneCount = std::min({ models.size(), tasks->getThreadCounts() + 1, std::size_t { 8 } });
	const auto chunkSize = 1 + (models.size() - 1) / laneCount;
	auto batch = tasks->createJobBatch();
	const TASK::JobDescriptor job { &cullRange, &work, "Scene Frustum Cull Range", TASK::JobAffinity::AnyCpu };
	try {
		for (std::size_t begin = 0; begin < models.size();) {
			const auto end = begin + std::min(chunkSize, models.size() - begin);
			if (!tasks->submit(batch, job, begin, end)) {
				tasks->cancel(batch);
				break;
			}
			begin = end;
		}
		tasks->close(batch);
	} catch (...) {
		tasks->cancel(batch);
		(void)tasks->wait(batch);
		throw;
	}
	if (tasks->wait(batch).status != TASK::JobBatchStatus::Completed) {
		cullRange(&work, 0, models.size());
	}
}
}
