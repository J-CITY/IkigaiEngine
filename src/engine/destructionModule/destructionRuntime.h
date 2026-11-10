#pragma once
#include "fracturePreparation.h"
#include "taskModule/backgroundTicket.h"
#include <atomic>
#include <deque>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <stop_token>

namespace IKIGAI::DESTRUCTION {
struct RuntimeSettings {
	unsigned MaximumPendingPerObject = 32;
	unsigned MaximumResults = 64;
	unsigned MaximumJobs = 2;
	unsigned MaximumOperationsPerFrame = 4;
	unsigned MaximumBodies = 2048;
	std::size_t MaximumWorkingBytes = 64 * 1024 * 1024;
	std::size_t MaximumGpuBytes = 64 * 1024 * 1024;
	std::size_t MaximumUploadBytesPerFrame = 1024 * 1024;
	double MaximumMainThreadMilliseconds = 2;
	float ImpactCooldown = .2f;
	void validate() const;
	template <class C> static auto serde(C& context, RuntimeSettings& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&RuntimeSettings::MaximumPendingPerObject, "MaximumPendingPerObject", default_ { 32u })
			.field(&RuntimeSettings::MaximumResults, "MaximumResults", default_ { 64u })
			.field(&RuntimeSettings::MaximumJobs, "MaximumJobs", default_ { 2u })
			.field(&RuntimeSettings::MaximumOperationsPerFrame, "MaximumOperationsPerFrame", default_ { 4u })
			.field(&RuntimeSettings::MaximumBodies, "MaximumBodies", default_ { 2048u })
			.field(&RuntimeSettings::MaximumWorkingBytes, "MaximumWorkingBytes", default_ { std::size_t(64 * 1024 * 1024) })
			.field(&RuntimeSettings::MaximumGpuBytes, "MaximumGpuBytes", default_ { std::size_t(64 * 1024 * 1024) })
			.field(&RuntimeSettings::MaximumUploadBytesPerFrame, "MaximumUploadBytesPerFrame", default_ { std::size_t(1024 * 1024) })
			.field(&RuntimeSettings::MaximumMainThreadMilliseconds, "MaximumMainThreadMilliseconds", default_ { 2.0 })
			.field(&RuntimeSettings::ImpactCooldown, "ImpactCooldown", default_ { .2f });
	}
};
enum class RequestStage { Queued, Slicing, Cooking, Upload, Ready, Completed };
const char* requestStageName(RequestStage stage);
struct RequestResult {
	std::uint64_t id = 0;
	std::uint64_t piece = 0;
	std::uint64_t generation = 0;
	RequestStage stage = RequestStage::Queued;
	SliceStatus status = SliceStatus::NoSplit;
	std::string error {};
	double latencyMilliseconds = 0;
	double slicingMilliseconds = 0;
	double cookingMilliseconds = 0;
};
struct RuntimeStatistics {
	std::uint64_t submitted = 0;
	std::uint64_t committed = 0;
	std::uint64_t rejected = 0;
	std::size_t workingBytes = 0;
	std::size_t topologyBytes = 0;
	std::size_t gpuBytes = 0;
	std::size_t gpuReservedBytes = 0;
	std::size_t uploadedBytes = 0;
	unsigned activeJobs = 0;
	double uploadMilliseconds = 0;
	double commitMilliseconds = 0;
	double mainThreadMilliseconds = 0;
};
struct FractureBudget {
	std::atomic<std::size_t> bytes { 0 };
	std::atomic<unsigned> jobs { 0 };
};
struct FractureJob {
	explicit FractureJob(std::shared_ptr<FractureBudget> account, std::size_t reservation);
	~FractureJob();
	std::string errorMessage() const;
	FractureSnapshot snapshot {};
	PreparedFracture result {};
	TASK::BackgroundResult completion {};
	std::atomic<RequestStage> stage { RequestStage::Queued };
	std::atomic<bool> cancelled { false };
	std::atomic<bool> done { false };
	std::shared_ptr<FractureBudget> budget {};
	std::size_t reservedBytes = 0;
};
// Bounded scene-local jobs on the engine's background executor; work owns no scene/component/world/GPU pointers.
class DestructionRuntime {
public:
	// The executor completes rejected work synchronously and accepted work exactly once.
	using Executor = std::function<void(std::function<void(std::stop_token)>, std::function<void(const TASK::BackgroundResult&)>)>;
	enum class Admission { Ready, Wait, ExceedsBudget };
	explicit DestructionRuntime(Executor executor);
	~DestructionRuntime();
	Admission admission(const FractureMesh& mesh, unsigned maximumTriangles, const RuntimeSettings& settings, bool concave = false) const;
	std::shared_ptr<FractureJob> submit(FractureSnapshot snapshot, const RuntimeSettings& settings);
	bool canPublish(const FractureJob& job) const;
	bool canFitGpuBytes(std::size_t bytes, const RuntimeSettings& settings) const;
	void beginFrame();
	std::size_t cursor = 0;
	unsigned operations = 0;
	RuntimeStatistics statistics {};
	std::shared_ptr<FractureBudget> budget = std::make_shared<FractureBudget>();

private:
	Executor mExecutor {};
	std::vector<std::weak_ptr<FractureJob>> mJobs {};
};
}
