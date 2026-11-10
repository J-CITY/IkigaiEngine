#include "destructionRuntime.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace IKIGAI::DESTRUCTION {
namespace {
	std::size_t reservationBytes(const FractureMesh& mesh, unsigned maximumTriangles, bool concave) {
		// Includes source/output/cooked hulls, seams, cap and clipping scratch.
		// Concave work caps total output triangles and total convex child triangles across the whole result.
		const auto scratchFactor = concave ? 48 : 16;
		return meshBytes(mesh) + std::size_t(maximumTriangles) * 3 * (sizeof(Vertex) + 64) * scratchFactor;
	}
}
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
std::string FractureJob::errorMessage() const {
	switch (completion.Outcome) {
	case TASK::BackgroundOutcome::Rejected:
		return TASK::backgroundRejectionMessage(completion.Rejection);
	case TASK::BackgroundOutcome::Failed:
		if (completion.Exception) {
			try {
				std::rethrow_exception(completion.Exception);
			} catch (const std::exception& error) {
				return error.what();
			} catch (...) { }
		}
		return "Fracture preparation failed";
	case TASK::BackgroundOutcome::Cancelled:
		return "Fracture preparation cancelled";
	case TASK::BackgroundOutcome::Succeeded:
		return result.error;
	}
	return {};
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
DestructionRuntime::Admission DestructionRuntime::admission(
	const FractureMesh& mesh, unsigned maximumTriangles, const RuntimeSettings& settings, bool concave) const {
	const auto reservation = reservationBytes(mesh, maximumTriangles, concave);
	if (reservation > settings.MaximumWorkingBytes || statistics.topologyBytes > settings.MaximumWorkingBytes - reservation) {
		return Admission::ExceedsBudget;
	}
	if (budget->jobs.load() >= settings.MaximumJobs || budget->bytes.load() > settings.MaximumWorkingBytes - reservation - statistics.topologyBytes) {
		return Admission::Wait;
	}
	return Admission::Ready;
}
std::shared_ptr<FractureJob> DestructionRuntime::submit(FractureSnapshot snapshot, const RuntimeSettings& settings) {
	const auto triangles = snapshot.concave ? snapshot.concave->slicing.maximumTriangles : snapshot.settings.maximumTriangles;
	if (admission(snapshot.mesh, triangles, settings, snapshot.concave.has_value()) != Admission::Ready) {
		return {};
	}
	auto job = std::make_shared<FractureJob>(budget, reservationBytes(snapshot.mesh, triangles, snapshot.concave.has_value()));
	job->snapshot = std::move(snapshot);
	std::erase_if(mJobs, [](const auto& weak) { return weak.expired(); });
	mJobs.push_back(job);
	mExecutor(
		[job](std::stop_token stopToken) {
			const std::stop_callback stopCallback(stopToken, [&] { job->cancelled.store(true); });
			if (!job->cancelled.load()) {
				job->stage.store(RequestStage::Slicing);
				job->result = prepareFracture(job->snapshot, &job->cancelled, [&] { job->stage.store(RequestStage::Cooking); });
			}
		},
		[job](const TASK::BackgroundResult& completion) {
			job->completion = completion;
			switch (completion.Outcome) {
			case TASK::BackgroundOutcome::Rejected:
				job->result.status = SliceStatus::Busy;
				break;
			case TASK::BackgroundOutcome::Failed:
				job->result.status = SliceStatus::UnsupportedMesh;
				break;
			case TASK::BackgroundOutcome::Cancelled:
				job->result.status = SliceStatus::Cancelled;
				break;
			case TASK::BackgroundOutcome::Succeeded:
				if (job->cancelled.load()) {
					job->completion.Outcome = TASK::BackgroundOutcome::Cancelled;
					job->result.status = SliceStatus::Cancelled;
				}
				break;
			}
			if (job->completion.Outcome != TASK::BackgroundOutcome::Succeeded) {
				job->result.pieces.clear();
				job->stage.store(RequestStage::Completed);
			}
			job->snapshot = {};
			job->done.store(true, std::memory_order_release);
		});
	if (!job->done.load(std::memory_order_acquire) || job->completion.Outcome != TASK::BackgroundOutcome::Rejected) {
		++statistics.submitted;
	}
	return job;
}
bool DestructionRuntime::canPublish(const FractureJob& job) const {
	if (!job.done.load(std::memory_order_acquire)) {
		return false;
	}
	// Dispatch failures do not mutate the scene and can complete ahead of earlier work.
	if (job.completion.Outcome != TASK::BackgroundOutcome::Succeeded) {
		return true;
	}
	// Workers prepare in parallel, but upload and commit follow scene admission order.
	// A slow head can delay publication to a later fixed step.
	for (const auto& weak : mJobs) {
		const auto candidate = weak.lock();
		if (candidate && !candidate->cancelled.load() && candidate->stage.load() != RequestStage::Completed) {
			return candidate.get() == &job;
		}
	}
	return false;
}
bool DestructionRuntime::canFitGpuBytes(std::size_t bytes, const RuntimeSettings& settings) const {
	return bytes <= settings.MaximumGpuBytes && statistics.gpuBytes <= settings.MaximumGpuBytes - bytes
		&& statistics.gpuReservedBytes <= settings.MaximumGpuBytes - bytes - statistics.gpuBytes;
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
