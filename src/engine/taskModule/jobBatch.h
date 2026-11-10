#pragma once

#include <cstddef>
#include <exception>
#include <memory>
#include <span>

namespace IKIGAI::TASK {
struct JobBatchState;
struct PoolState;
class ThreadPoolExecutor;

enum class JobAffinity { AnyCpu, PoolWorker };

struct JobDescriptor {
	using Function = void (*)(void* payload, std::size_t begin, std::size_t end);

	Function function = nullptr;
	void* payload = nullptr;
	const char* zoneName = nullptr;
	JobAffinity affinity = JobAffinity::AnyCpu;
};

enum class JobBatchStatus { Completed, Failed, Cancelled };

struct JobBatchResult {
	JobBatchStatus status = JobBatchStatus::Cancelled;
	std::exception_ptr exception = {};
};

// A move-only completion handle. Destruction does not wait or cancel.
// Jobs borrow their payloads. The caller must keep them alive until wait returns.
// A non-null zoneName must point to a null-terminated string with static lifetime.
class JobBatch {
public:
	JobBatch() = default;
	JobBatch(JobBatch&&) noexcept = default;
	JobBatch& operator=(JobBatch&&) noexcept = default;
	JobBatch(const JobBatch&) = delete;
	JobBatch& operator=(const JobBatch&) = delete;

private:
	friend class ThreadPoolExecutor;
	friend struct PoolState;

	std::shared_ptr<PoolState> mPool = {};
	std::shared_ptr<JobBatchState> mState = {};
};
}
