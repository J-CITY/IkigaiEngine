#include "destructionRuntime.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace IKIGAI::DESTRUCTION {
void RuntimeSettings::validate() const {
	if (!MaximumPendingPerObject || MaximumPendingPerObject > 1024 || !MaximumResults || MaximumResults > 4096 || !MaximumJobs || MaximumJobs > 16
		|| !MaximumOperationsPerFrame || MaximumOperationsPerFrame > 1024 || MaximumBodies < 2 || MaximumBodies > 1048576
		|| MaximumWorkingBytes < 65536 || MaximumWorkingBytes > std::size_t(1024) * 1024 * 1024 || MaximumGpuBytes < 65536
		|| MaximumGpuBytes > std::size_t(1024) * 1024 * 1024 || MaximumUploadBytesPerFrame < 65536 || MaximumUploadBytesPerFrame > MaximumGpuBytes
		|| !std::isfinite(MaximumMainThreadMilliseconds) || MaximumMainThreadMilliseconds <= 0 || MaximumMainThreadMilliseconds > 100
		|| !std::isfinite(ImpactCooldown) || ImpactCooldown < 0) {
		throw std::invalid_argument("Invalid scene Destruction runtime budgets");
	}
}
const char* requestStageName(RequestStage stage) {
	switch (stage) {
	case RequestStage::Queued:
		return "Queued";
	case RequestStage::Slicing:
		return "Slicing";
	case RequestStage::Cooking:
		return "Cooking";
	case RequestStage::Upload:
		return "Upload";
	case RequestStage::Ready:
		return "Ready";
	case RequestStage::Completed:
		return "Completed";
	}
	return "Unknown";
}
FractureJob::FractureJob(std::shared_ptr<FractureBudget> account, std::size_t reservation)
	: budget(std::move(account))
	, reservedBytes(reservation) {
	budget->bytes.fetch_add(reservedBytes);
	budget->jobs.fetch_add(1);
}
FractureJob::~FractureJob() {
	budget->bytes.fetch_sub(reservedBytes);
	budget->jobs.fetch_sub(1);
}
DestructionRuntime::DestructionRuntime(Executor executor)
	: mExecutor(std::move(executor)) {
	if (!mExecutor) {
		throw std::invalid_argument("Destruction requires a CPU executor");
	}
}
DestructionRuntime::~DestructionRuntime() {
	for (auto& weak : mJobs) {
		if (auto job = weak.lock()) {
			job->cancelled.store(true);
		}
	}
}
std::shared_ptr<FractureJob> DestructionRuntime::submit(FractureSnapshot snapshot, const RuntimeSettings& settings) {
	// Conservative reservation includes source/output/cooked hulls, seams, cap and clipping scratch.
	const std::size_t reservation = meshBytes(snapshot.mesh) + std::size_t(snapshot.settings.maximumTriangles) * 3 * (sizeof(Vertex) + 64) * 16;
	if (budget->jobs.load() >= settings.MaximumJobs || reservation > settings.MaximumWorkingBytes
		|| statistics.topologyBytes > settings.MaximumWorkingBytes - reservation
		|| budget->bytes.load() > settings.MaximumWorkingBytes - reservation - statistics.topologyBytes) {
		return {};
	}
	auto job = std::make_shared<FractureJob>(budget, reservation);
	job->snapshot = std::move(snapshot);
	std::erase_if(mJobs, [](const auto& weak) { return weak.expired(); });
	mJobs.push_back(job);
	mExecutor([job] {
		if (!job->cancelled.load()) {
			job->stage.store(RequestStage::Slicing);
			job->result = prepareFracture(job->snapshot, &job->cancelled, [&] { job->stage.store(RequestStage::Cooking); });
		}
		if (job->cancelled.load()) {
			job->result.status = SliceStatus::Cancelled;
		}
		job->snapshot = {};
		job->done.store(true, std::memory_order_release);
	});
	++statistics.submitted;
	return job;
}
void DestructionRuntime::beginFrame() {
	operations = 0;
	statistics.uploadedBytes = 0;
	statistics.uploadMilliseconds = 0;
	statistics.commitMilliseconds = 0;
	statistics.mainThreadMilliseconds = 0;
	statistics.workingBytes = budget->bytes.load();
	statistics.activeJobs = budget->jobs.load();
}
}
