#include "task.h"

#include "profilerModule/trace.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "safeQueue.h"
using namespace IKIGAI::TASK;

namespace IKIGAI::TASK {
// All batch state is protected by the owning pool's job mutex.
struct JobBatchState {
	std::size_t mPending = 0;
	std::size_t mBlocked = 0;
	bool mClosed = false;
	bool mCancelled = false;
	bool mNeededByWait = false;
	std::exception_ptr mException = {};
};

struct ScopedJob {
	JobDescriptor mDescriptor;
	std::size_t mBegin = 0;
	std::size_t mEnd = 0;
	std::shared_ptr<JobBatchState> mBatch;
	std::vector<std::shared_ptr<JobBatchState>> mPrerequisites;
	ScopedJob* mPrevious = nullptr;
	ScopedJob* mNext = nullptr;
};

// Owns already allocated jobs. Transfers between blocked, ready and retired
// queues cannot allocate, even during cancellation or exception handling.
class ScopedJobQueue {
public:
	ScopedJobQueue() = default;
	ScopedJobQueue(const ScopedJobQueue&) = delete;
	ScopedJobQueue& operator=(const ScopedJobQueue&) = delete;
	~ScopedJobQueue() {
		while (mHead) {
			take(*mHead);
		}
	}

	ScopedJob* first() const {
		return mHead;
	}
	ScopedJob* last() const {
		return mTail;
	}

	void push(std::unique_ptr<ScopedJob> job) {
		job->mPrevious = mTail;
		if (mTail) {
			mTail->mNext = job.get();
		}
		else {
			mHead = job.get();
		}
		mTail = job.release();
	}

	std::unique_ptr<ScopedJob> take(ScopedJob& job) {
		if (job.mPrevious) {
			job.mPrevious->mNext = job.mNext;
		}
		else {
			mHead = job.mNext;
		}
		if (job.mNext) {
			job.mNext->mPrevious = job.mPrevious;
		}
		else {
			mTail = job.mPrevious;
		}
		job.mPrevious = nullptr;
		job.mNext = nullptr;
		return std::unique_ptr<ScopedJob> { &job };
	}

private:
	ScopedJob* mHead = nullptr;
	ScopedJob* mTail = nullptr;
};

struct PoolState : std::enable_shared_from_this<PoolState> {
	explicit PoolState(std::shared_ptr<InflightCounter> inflightCounter)
		: mInflightCounter { std::move(inflightCounter) } { }

	std::mutex mIdleMutex;
	std::condition_variable mIdleCv;
	// Queues of tasks scheduled with threadID < 0. Stable for the lifetime of the workers.
	std::vector<ThreadSafeQueue<std::shared_ptr<ITask>>*> mUnboundQueues;

	void notifyWork() {
		std::lock_guard<std::mutex> lock { mIdleMutex };
		mIdleCv.notify_all();
	}

	template <typename Pred> void waitForWork(Pred&& pred) {
		// Work publishers and shutdown signal under mIdleMutex after publishing state.
		std::unique_lock<std::mutex> lock { mIdleMutex };
		mIdleCv.wait(lock, std::forward<Pred>(pred));
	}

	std::shared_ptr<InflightCounter> mInflightCounter;
	std::mutex mTaskMutex;
	std::vector<Thread*> mWorkers;
	std::unordered_map<ITask*, std::shared_ptr<ITask>> mWaitingTasks;
	std::size_t mScheduleCursor = 0;
	std::atomic_bool mAcceptingTasks = true;
	std::mutex mJobMutex;
	std::condition_variable mJobCv;
	bool mAcceptingJobs = true;
	std::size_t mRunningJobs = 0;
	ScopedJobQueue mReadyJobs;
	ScopedJobQueue mBlockedJobs;

	bool submit(JobBatch& batch, JobDescriptor descriptor, std::size_t begin, std::size_t end, std::span<const JobBatch* const> prerequisites);
	void close(JobBatchState& batch);
	void cancel(JobBatchState& batch);
	void stopJobs();
	void waitForRunningJobs();
	JobBatchResult wait(JobBatchState& batch);
	bool hasReadyJobs();
	std::unique_ptr<ScopedJob> popJob();
	void runJob(const ScopedJob& job);
	std::shared_ptr<ITask> schedule(std::shared_ptr<ITask> task, int threadID, bool front);
	void closeAdmission();
	void cancelWaitingTasks();
	void cancelTask(ITask& task);

private:
	void enqueue(const std::shared_ptr<ITask>& task, std::size_t threadIndex, bool front, bool stealable);
	void finishWaitingTask(ITask* task, std::size_t threadIndex, bool front, bool stealable, bool enqueue);

	// These helpers run with mJobMutex held. Removed nodes are destroyed by the caller after unlocking.
	bool publishJobs(ScopedJobQueue& retired);
	void discardJobs(ScopedJobQueue& queue, ScopedJobQueue& retired, const JobBatchState* batch, bool blocked);
	std::unique_ptr<ScopedJob> takeReadyJob(ScopedJob* job);
};
}

namespace {
thread_local bool gIsPoolWorker = false;
thread_local const PoolState* gWorkerPool = nullptr;

struct ActiveJob {
	const PoolState* mPool;
	const JobBatchState* mBatch;
	const ActiveJob* mParent;
};

thread_local const ActiveJob* gActiveJob = nullptr;

bool batchFailed(const JobBatchState& batch) {
	return batch.mCancelled || static_cast<bool>(batch.mException);
}

// A prerequisite must be sealed before the dependent submission. That puts all
// prerequisite jobs before their dependents in submission order. Walking the
// blocked queue backwards marks the batches needed by a waiter in one pass.
class RequiredBatches {
public:
	RequiredBatches(JobBatchState& target, const ScopedJobQueue& blockedJobs)
		: mTarget { target }
		, mBlockedJobs { blockedJobs } {
		mTarget.mNeededByWait = true;
		if (mTarget.mBlocked != 0) {
			for (const auto* job = mBlockedJobs.last(); job; job = job->mPrevious) {
				if (job->mBatch->mNeededByWait) {
					for (const auto& prerequisite : job->mPrerequisites) {
						prerequisite->mNeededByWait = true;
					}
				}
			}
		}
	}

	~RequiredBatches() {
		mTarget.mNeededByWait = false;
		if (mTarget.mBlocked != 0) {
			for (const auto* job = mBlockedJobs.first(); job; job = job->mNext) {
				for (const auto& prerequisite : job->mPrerequisites) {
					prerequisite->mNeededByWait = false;
				}
			}
		}
	}

private:
	JobBatchState& mTarget;
	const ScopedJobQueue& mBlockedJobs;
};

bool queueHasWork(ThreadSafeQueue<std::shared_ptr<ITask>>& queue) {
	return queue.isValid() && !queue.empty();
}
}

bool PoolState::submit(
	JobBatch& batch, JobDescriptor descriptor, std::size_t begin, std::size_t end, std::span<const JobBatch* const> prerequisites) {
	auto job = std::make_unique<ScopedJob>(descriptor, begin, end, batch.mState);
	std::unique_lock lock { mJobMutex };
	if (!mAcceptingJobs || job->mBatch->mClosed) {
		return false;
	}

	bool failedPrerequisite = false;
	for (const auto* prerequisite : prerequisites) {
		if (!prerequisite || prerequisite->mPool.get() != this || !prerequisite->mState->mClosed) {
			return false;
		}
		if (prerequisite->mState->mPending != 0) {
			job->mPrerequisites.push_back(prerequisite->mState);
		}
		else if (batchFailed(*prerequisite->mState)) {
			failedPrerequisite = true;
		}
	}

	// A job with a failed prerequisite is already terminal when admitted.
	if (failedPrerequisite) {
		batch.mState->mCancelled = true;
		return true;
	}

	// Allocate the queue node before touching either completion counter.
	const bool ready = job->mPrerequisites.empty();
	if (ready) {
		mReadyJobs.push(std::move(job));
	}
	else {
		mBlockedJobs.push(std::move(job));
		++batch.mState->mBlocked;
	}
	++batch.mState->mPending;
	mInflightCounter->increment();
	lock.unlock();
	if (ready) {
		std::lock_guard idleLock { mIdleMutex };
		mIdleCv.notify_one();
	}
	return true;
}

bool PoolState::publishJobs(ScopedJobQueue& retired) {
	bool published = false;
	for (auto* next = mBlockedJobs.first(); next;) {
		auto* current = next;
		next = current->mNext;
		bool ready = true;
		bool failed = false;
		for (const auto& prerequisite : current->mPrerequisites) {
			if (prerequisite->mPending != 0) {
				ready = false;
			}
			else if (batchFailed(*prerequisite)) {
				failed = true;
				break;
			}
		}
		if (failed) {
			current->mBatch->mCancelled = true;
			--current->mBatch->mPending;
			--current->mBatch->mBlocked;
			retired.push(mBlockedJobs.take(*current));
			mInflightCounter->decrement();
		}
		else if (ready) {
			--current->mBatch->mBlocked;
			mReadyJobs.push(mBlockedJobs.take(*current));
			published = true;
		}
	}
	return published;
}

void PoolState::discardJobs(ScopedJobQueue& queue, ScopedJobQueue& retired, const JobBatchState* batch, bool blocked) {
	for (auto* next = queue.first(); next;) {
		auto* current = next;
		next = current->mNext;
		if (!batch || current->mBatch.get() == batch) {
			current->mBatch->mCancelled = true;
			--current->mBatch->mPending;
			if (blocked) {
				--current->mBatch->mBlocked;
			}
			retired.push(queue.take(*current));
			mInflightCounter->decrement();
		}
	}
}

void PoolState::close(JobBatchState& batch) {
	std::lock_guard lock { mJobMutex };
	batch.mClosed = true;
}

void PoolState::cancel(JobBatchState& batch) {
	ScopedJobQueue retired;
	bool published = false;
	{
		std::lock_guard lock { mJobMutex };
		if (batch.mClosed && batch.mPending == 0) {
			return;
		}
		batch.mClosed = true;
		batch.mCancelled = true;
		discardJobs(mReadyJobs, retired, &batch, false);
		discardJobs(mBlockedJobs, retired, &batch, true);
		if (batch.mPending == 0) {
			published = publishJobs(retired);
		}
	}
	mJobCv.notify_all();
	if (published) {
		notifyWork();
	}
}

void PoolState::stopJobs() {
	ScopedJobQueue retired;
	{
		std::lock_guard lock { mJobMutex };
		mAcceptingJobs = false;
		discardJobs(mReadyJobs, retired, nullptr, false);
		discardJobs(mBlockedJobs, retired, nullptr, true);
	}
	mJobCv.notify_all();
}

void PoolState::waitForRunningJobs() {
	std::unique_lock lock { mJobMutex };
	mJobCv.wait(lock, [this] { return mRunningJobs == 0; });
}

JobBatchResult PoolState::wait(JobBatchState& batch) {
	IKIGAI_PROFILE_SCOPE("Wait Job Batch");
	const bool samePoolWorker = gWorkerPool == this;
	std::unique_lock lock { mJobMutex };
	if (gActiveJob) {
		RequiredBatches required { batch, mBlockedJobs };
		for (const auto* active = gActiveJob; active; active = active->mParent) {
			if (active->mPool == this && active->mBatch && active->mBatch->mNeededByWait) {
				throw std::logic_error("Job batch wait depends on a callback executing on this thread");
			}
		}
	}
	batch.mClosed = true;
	while (batch.mPending != 0) {
		ScopedJob* ready = nullptr;
		{
			RequiredBatches required { batch, mBlockedJobs };
			for (auto* job = mReadyJobs.first(); job; job = job->mNext) {
				if (job->mBatch->mNeededByWait && (samePoolWorker || job->mDescriptor.affinity == JobAffinity::AnyCpu)) {
					ready = job;
					break;
				}
			}
		}
		if (auto job = takeReadyJob(ready)) {
			lock.unlock();
			{
				IKIGAI_PROFILE_SCOPE("Help Scoped Job");
				runJob(*job);
			}
			job.reset();
			lock.lock();
		}
		else {
			IKIGAI_PROFILE_SCOPE("Park Job Batch");
			mJobCv.wait(lock);
		}
	}
	if (batch.mException) {
		return { JobBatchStatus::Failed, batch.mException };
	}
	return { batch.mCancelled ? JobBatchStatus::Cancelled : JobBatchStatus::Completed, {} };
}

bool PoolState::hasReadyJobs() {
	std::lock_guard lock { mJobMutex };
	return mReadyJobs.first() != nullptr;
}

std::unique_ptr<ScopedJob> PoolState::takeReadyJob(ScopedJob* job) {
	if (!job) {
		return nullptr;
	}
	++mRunningJobs;
	return mReadyJobs.take(*job);
}

std::unique_ptr<ScopedJob> PoolState::popJob() {
	std::lock_guard lock { mJobMutex };
	return takeReadyJob(mReadyJobs.first());
}

void PoolState::runJob(const ScopedJob& job) {
	std::exception_ptr exception;
	const ActiveJob active { this, job.mBatch.get(), gActiveJob };
	gActiveJob = &active;
	try {
		IKIGAI_PROFILE_SCOPE("Execute Scoped Job");
		if (job.mDescriptor.zoneName) {
			IKIGAI_PROFILE_ZONE_TEXT(job.mDescriptor.zoneName, std::strlen(job.mDescriptor.zoneName));
		}
		job.mDescriptor.function(job.mDescriptor.payload, job.mBegin, job.mEnd);
	} catch (...) {
		exception = std::current_exception();
	}
	gActiveJob = active.mParent;

	ScopedJobQueue retired;
	bool finished = false;
	bool published = false;
	bool drained = false;
	{
		std::lock_guard lock { mJobMutex };
		if (exception && !job.mBatch->mException) {
			job.mBatch->mException = std::move(exception);
		}
		finished = --job.mBatch->mPending == 0;
		drained = --mRunningJobs == 0;
		if (finished && job.mBatch->mClosed) {
			published = publishJobs(retired);
		}
		mInflightCounter->decrement();
	}
	if (finished || drained) {
		mJobCv.notify_all();
	}
	if (published) {
		notifyWork();
	}
}

bool IKIGAI::TASK::isPoolWorkerThread() {
	return gIsPoolWorker;
}

bool IKIGAI::TASK::isTaskExecutorCallback() {
	return gIsPoolWorker || gActiveJob;
}

void IKIGAI::TASK::logTaskException(const char* context, const std::exception_ptr& exception) {
	try {
		if (exception) {
			std::rethrow_exception(exception);
		}
		std::cerr << "[Task] " << context << ": no exception" << std::endl;
	} catch (const std::exception& e) {
		std::cerr << "[Task] " << context << ": " << e.what() << std::endl;
	} catch (...) {
		std::cerr << "[Task] " << context << ": unknown exception" << std::endl;
	}
}

//--------------------- InflightCounter

void InflightCounter::increment() {
	std::lock_guard<std::mutex> lock { mMutex };
	++mCount;
}

void InflightCounter::decrement() {
	std::lock_guard<std::mutex> lock { mMutex };
	if (mCount > 0) {
		--mCount;
	}
	if (mCount == 0) {
		mCondition.notify_all();
	}
}

void InflightCounter::waitUntilIdle() {
	std::unique_lock<std::mutex> lock { mMutex };
	mCondition.wait(lock, [this]() { return mCount == 0; });
}

size_t InflightCounter::getCount() {
	std::lock_guard<std::mutex> lock { mMutex };
	return mCount;
}

//--------------------- ITask

bool ITask::isFinished() {
	std::lock_guard<std::mutex> lock { mMutex };
	return mIsFinished;
}

bool ITask::isCancelled() {
	std::lock_guard<std::mutex> lock { mMutex };
	return mIsCancelled;
}

void ITask::cancel() {
	markFinished(true);
}

void ITask::wait() {
	std::unique_lock<std::mutex> lock { mMutex };
	mCondition.wait(lock, [this]() { return mIsFinished; });
}

void ITask::continueWith(std::function<void()> continuation) {
	{
		std::lock_guard<std::mutex> lock { mMutex };
		if (!mIsFinished) {
			mContinuations.push_back(std::move(continuation));
			return;
		}
	}
	try {
		continuation();
	} catch (...) {
		logTaskException("Continuation failed", std::current_exception());
	}
}

void ITask::markFinished(bool cancelled) {
	std::vector<std::function<void()>> continuations;
	{
		std::lock_guard<std::mutex> lock { mMutex };
		if (mIsFinished) {
			return;
		}
		mIsFinished = true;
		mIsCancelled = cancelled;
		continuations.swap(mContinuations);
	}
	mCondition.notify_all();
	for (auto& continuation : continuations) {
		try {
			continuation();
		} catch (...) {
			logTaskException("Continuation failed", std::current_exception());
		}
	}
}

//--------------------- Thread pool executor

ThreadPoolExecutor::ThreadPoolExecutor(int threadCount) {
	if (threadCount > 0) {
		setup(static_cast<unsigned>(threadCount));
	}
}

bool ThreadPoolExecutor::setup(std::optional<unsigned> threadCount) {
	std::lock_guard lock { mLifecycleMutex };
	if (mPoolState.load() || mStoppingThread != std::thread::id {}) {
		return false;
	}
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
	(void)threadCount;
	return false;
#else
	const auto workers = threadCount.value_or(std::max(std::thread::hardware_concurrency(), 2u));
	if (workers == 0) {
		return false;
	}
	std::vector<std::unique_ptr<Thread>> threads;
	try {
		auto pool = std::make_shared<PoolState>(mInflightCounter);
		pool->mUnboundQueues.reserve(workers);
		pool->mWorkers.reserve(workers);
		threads.reserve(workers);
		for (unsigned index = 0; index < workers; ++index) {
			threads.push_back(std::make_unique<Thread>(static_cast<int>(index), mInflightCounter, pool));
			pool->mWorkers.push_back(threads.back().get());
		}
		for (auto& thread : threads) {
			thread->start();
		}
		mThreads = std::move(threads);
		mPoolState.store(std::move(pool));
		return true;
	} catch (...) {
		for (auto& thread : threads) {
			thread->requestStop();
		}
		for (auto& thread : threads) {
			thread->join();
		}
		logTaskException("Task pool startup failed", std::current_exception());
		return false;
	}
#endif
}

bool ThreadPoolExecutor::initialize() {
	return isReady();
}

bool ThreadPoolExecutor::update() {
	return isReady();
}

ThreadPoolExecutor::~ThreadPoolExecutor() {
	terminate();
}

bool ThreadPoolExecutor::isExecutingOnThisThread() const {
	if (const auto pool = mPoolState.load()) {
		if (gWorkerPool == pool.get()) {
			return true;
		}
		for (const auto* active = gActiveJob; active; active = active->mParent) {
			if (active->mPool == pool.get()) {
				return true;
			}
		}
	}
	return false;
}

bool ThreadPoolExecutor::isReady() const {
	const auto pool = mPoolState.load();
	return pool && pool->mAcceptingTasks.load();
}

void ThreadPoolExecutor::closeAdmission() {
	if (auto pool = mPoolState.load()) {
		pool->closeAdmission();
	}
}

void ThreadPoolExecutor::cancelWaitingTasks() {
	if (auto pool = mPoolState.load()) {
		pool->cancelWaitingTasks();
	}
}

bool ThreadPoolExecutor::terminate() {
	if (isExecutingOnThisThread()) {
		throw std::logic_error("Cannot terminate the task pool from one of its callbacks");
	}
	std::unique_lock lock { mLifecycleMutex };
	if (mStoppingThread != std::thread::id {}) {
		if (mStoppingThread == std::this_thread::get_id()) {
			throw std::logic_error("Cannot terminate the task pool from its shutdown callback");
		}
		mLifecycleCondition.wait(lock, [this]() { return mStoppingThread == std::thread::id {}; });
		return true;
	}
	const auto pool = mPoolState.load();
	if (!pool) {
		return true;
	}
	mStoppingThread = std::this_thread::get_id();
	lock.unlock();
	pool->closeAdmission();
	// Join every worker before destroying any queue so an in-flight steal cannot touch a dead Thread.
	for (auto& thread : mThreads) {
		thread->requestStop();
	}
	pool->cancelWaitingTasks();
	for (auto& thread : mThreads) {
		thread->cancelPendingTasks();
	}
	for (auto& thread : mThreads) {
		thread->join();
	}
	pool->waitForRunningJobs();
	mInflightCounter->waitUntilIdle();
	mThreads.clear();
	lock.lock();
	mPoolState.store(nullptr);
	mStoppingThread = {};
	lock.unlock();
	mLifecycleCondition.notify_all();
	return true;
}

void ThreadPoolExecutor::waitSync() {
	if (!mPoolState.load()) {
		return;
	}
	if (isExecutingOnThisThread()) {
		throw std::logic_error("Cannot wait for all tasks from a callback in the same pool");
	}
	mInflightCounter->waitUntilIdle();
}

JobBatch ThreadPoolExecutor::createJobBatch() {
	JobBatch batch;
	if (auto pool = mPoolState.load()) {
		std::lock_guard lock { pool->mJobMutex };
		if (pool->mAcceptingJobs) {
			batch.mState = std::make_shared<JobBatchState>();
			batch.mPool = std::move(pool);
		}
	}
	return batch;
}

bool ThreadPoolExecutor::submit(
	JobBatch& batch, JobDescriptor job, std::size_t begin, std::size_t end, std::span<const JobBatch* const> prerequisites) {
	if (!batch.mPool || batch.mPool->mInflightCounter != mInflightCounter || !job.function || begin >= end) {
		return false;
	}
	if (job.affinity != JobAffinity::AnyCpu && job.affinity != JobAffinity::PoolWorker) {
		return false;
	}
	return batch.mPool->submit(batch, job, begin, end, prerequisites);
}

void ThreadPoolExecutor::close(JobBatch& batch) {
	if (batch.mPool && batch.mPool->mInflightCounter == mInflightCounter) {
		batch.mPool->close(*batch.mState);
	}
}

void ThreadPoolExecutor::cancel(JobBatch& batch) {
	if (batch.mPool && batch.mPool->mInflightCounter == mInflightCounter) {
		batch.mPool->cancel(*batch.mState);
	}
}

JobBatchResult ThreadPoolExecutor::wait(JobBatch& batch) {
	if (batch.mPool && batch.mPool->mInflightCounter == mInflightCounter) {
		return batch.mPool->wait(*batch.mState);
	}
	return {};
}

std::shared_ptr<ITask> ThreadPoolExecutor::addTask(std::unique_ptr<ITask>&& task, int32_t threadID) {
	auto pool = mPoolState.load();
	return pool ? pool->schedule(std::move(task), threadID, false) : nullptr;
}

std::shared_ptr<ITask> ThreadPoolExecutor::addTaskFront(std::unique_ptr<ITask>&& task, int32_t threadID) {
	auto pool = mPoolState.load();
	return pool ? pool->schedule(std::move(task), threadID, true) : nullptr;
}

std::shared_ptr<ITask> PoolState::schedule(std::shared_ptr<ITask> task, int threadID, bool front) {
	if (!task) {
		return nullptr;
	}
	const auto upstream = task->getUpstreamTask();
	const bool stealable = threadID < 0;
	std::size_t threadIndex = 0;
	{
		std::lock_guard lock { mTaskMutex };
		if (!mAcceptingTasks.load()) {
			return nullptr;
		}
		threadIndex = (stealable ? mScheduleCursor++ : static_cast<std::size_t>(threadID)) % mWorkers.size();
		mInflightCounter->increment();
		try {
			if (upstream) {
				mWaitingTasks.emplace(task.get(), task);
			}
			else {
				enqueue(task, threadIndex, front, stealable);
			}
		} catch (...) {
			mInflightCounter->decrement();
			throw;
		}
	}
	if (upstream) {
		try {
			upstream->continueWith([pool = weak_from_this(), weakTask = std::weak_ptr<ITask> { task }, threadIndex, front, stealable]() {
				if (const auto owner = pool.lock()) {
					if (const auto dependent = weakTask.lock()) {
						owner->finishWaitingTask(dependent.get(), threadIndex, front, stealable, true);
					}
				}
			});
		} catch (...) {
			finishWaitingTask(task.get(), threadIndex, front, stealable, false);
			logTaskException("Could not register task continuation", std::current_exception());
		}
	}
	return task;
}

void PoolState::enqueue(const std::shared_ptr<ITask>& task, std::size_t threadIndex, bool front, bool stealable) {
	if (front) {
		mWorkers[threadIndex]->addTaskFront(std::shared_ptr<ITask> { task }, stealable);
	}
	else {
		mWorkers[threadIndex]->addTask(std::shared_ptr<ITask> { task }, stealable);
	}
}

void PoolState::finishWaitingTask(ITask* task, std::size_t threadIndex, bool front, bool stealable, bool shouldEnqueue) {
	std::shared_ptr<ITask> waiting;
	std::exception_ptr exception;
	{
		std::lock_guard lock { mTaskMutex };
		auto entry = mWaitingTasks.extract(task);
		if (entry.empty()) {
			return;
		}
		waiting = std::move(entry.mapped());
		if (shouldEnqueue && mAcceptingTasks.load()) {
			try {
				enqueue(waiting, threadIndex, front, stealable);
				return;
			} catch (...) {
				exception = std::current_exception();
			}
		}
	}
	cancelTask(*waiting);
	if (exception) {
		logTaskException("Could not enqueue task continuation", exception);
	}
}

void PoolState::closeAdmission() {
	{
		std::lock_guard lock { mTaskMutex };
		mAcceptingTasks.store(false);
	}
	stopJobs();
}

void PoolState::cancelWaitingTasks() {
	decltype(mWaitingTasks) waiting;
	{
		std::lock_guard lock { mTaskMutex };
		waiting.swap(mWaitingTasks);
	}
	for (auto& entry : waiting) {
		cancelTask(*entry.second);
	}
}

void PoolState::cancelTask(ITask& task) {
	const ActiveJob cancellation { this, nullptr, gActiveJob };
	gActiveJob = &cancellation;
	task.cancel();
	gActiveJob = cancellation.mParent;
	mInflightCounter->decrement();
}

size_t ThreadPoolExecutor::getThreadCounts() const {
	const auto pool = mPoolState.load();
	return pool && pool->mAcceptingTasks.load() ? pool->mWorkers.size() : 0;
}

size_t ThreadPoolExecutor::getUnfinishedWorkCount() const {
	return mInflightCounter->getCount();
}

//--------------------- New thread executor

bool NewThreadExecutor::setup() {
	return true;
}

bool NewThreadExecutor::initialize() {
	return true;
}

// run in own thread
bool NewThreadExecutor::update() {
	std::erase_if(mThreads, [](const auto& e) { return e->getUnfinishedWorkCount() == 0; });
	return true;
}

bool NewThreadExecutor::terminate() {
	for (size_t i = 0; i < mThreads.size(); i++) {
		mThreads[i].reset();
	}
	return true;
}

void NewThreadExecutor::waitSync() {
	mInflightCounter->waitUntilIdle();
}

std::shared_ptr<ITask> NewThreadExecutor::addTask(std::unique_ptr<ITask>&& task) {
	static int id = 0;
	auto thread = std::make_unique<Thread>(id, mInflightCounter);
	++id;
	mThreads.push_back(std::move(thread));
	mInflightCounter->increment();
	return mThreads.back()->addTask(std::move(task));
}

size_t NewThreadExecutor::getThreadCounts() {
	return mThreads.size();
}

//----------------------End

Thread::Thread(int ThreadIndex, std::shared_ptr<InflightCounter> inflightCounter, std::shared_ptr<PoolState> poolState)
	: mInflightCounter { std::move(inflightCounter) }
	, mPoolState { std::move(poolState) }
	, mIndex { static_cast<size_t>(ThreadIndex) } {
	if (mPoolState) {
		mPoolState->mUnboundQueues.push_back(&mUnboundQueue);
	}
	else {
		// Dedicated threads have no siblings to steal from; start immediately so addTask can race the worker.
		start();
	}
}

Thread::~Thread() {
	requestStop();
	cancelPendingTasks();
	join();
	if (mPoolState) {
		std::erase(mPoolState->mUnboundQueues, &mUnboundQueue);
	}
	delete mThreadHandle;
	mThreadHandle = nullptr;
}

void Thread::start() {
	if (mThreadHandle != nullptr) {
		return;
	}
	mThreadHandle = new std::thread(&Thread::worker, this, static_cast<uint32_t>(mIndex));
}

void Thread::requestStop() {
	mIsDone = true;
	mBoundQueue.invalidate();
	mUnboundQueue.invalidate();
	signalWork();
}

void Thread::cancelPendingTasks() {
	for (auto* queue : { &mBoundQueue, &mUnboundQueue }) {
		while (auto task = queue->tryTakeInvalidated()) {
			if (mPoolState) {
				mPoolState->cancelTask(**task);
			}
			else {
				(*task)->cancel();
				if (mInflightCounter) {
					mInflightCounter->decrement();
				}
			}
		}
	}
}

void Thread::join() {
	if (mThreadHandle != nullptr && mThreadHandle->joinable()) {
		mThreadHandle->join();
	}
}

void Thread::signalWork() {
	if (mPoolState) {
		mPoolState->notifyWork();
	}
}

inline ThreadState Thread::getState() const {
	return mThreadState;
}

size_t Thread::getUnfinishedWorkCount() {
	return mBoundQueue.size() + mUnboundQueue.size();
}

std::shared_ptr<ITask> Thread::addTask(std::shared_ptr<ITask>&& task, bool stealable) {
	std::shared_ptr<ITask> l_result { task };
	if (stealable) {
		mUnboundQueue.push(std::move(task));
	}
	else {
		mBoundQueue.push(std::move(task));
	}
	signalWork();
	return l_result;
}

std::shared_ptr<ITask> Thread::addTaskFront(std::shared_ptr<ITask>&& task, bool stealable) {
	std::shared_ptr<ITask> l_result { task };
	if (stealable) {
		mUnboundQueue.pushFront(std::move(task));
	}
	else {
		mBoundQueue.pushFront(std::move(task));
	}
	signalWork();
	return l_result;
}

std::string Thread::getThreadID() {
	std::stringstream ss;
	ss << mId.second;
	return ss.str();
}

void Thread::executeTask(const std::shared_ptr<ITask>& task) {
	task->execute();
}

bool Thread::hasPendingWork() {
	if (queueHasWork(mBoundQueue) || queueHasWork(mUnboundQueue)) {
		return true;
	}
	if (!mPoolState) {
		return false;
	}
	if (mPoolState->hasReadyJobs()) {
		return true;
	}
	for (auto* queue : mPoolState->mUnboundQueues) {
		if (queue != nullptr && queue != &mUnboundQueue && queueHasWork(*queue)) {
			return true;
		}
	}
	return false;
}

std::optional<std::shared_ptr<ITask>> Thread::tryPopLocal() {
	if (auto task = mBoundQueue.tryPop()) {
		return task;
	}
	if (auto task = mUnboundQueue.tryPop()) {
		return task;
	}
	return std::nullopt;
}

std::optional<std::shared_ptr<ITask>> Thread::trySteal() {
	// Inflight was incremented in schedule and is decremented by the worker that executes the task.
	if (!mPoolState) {
		return std::nullopt;
	}
	const size_t count = mPoolState->mUnboundQueues.size();
	if (count <= 1 || mIndex >= count) {
		return std::nullopt;
	}
	for (size_t step = 1; step < count; ++step) {
		auto* queue = mPoolState->mUnboundQueues[(mIndex + step) % count];
		if (queue == nullptr || queue == &mUnboundQueue) {
			continue;
		}
		if (auto task = queue->tryPopBack()) {
			return task;
		}
	}
	return std::nullopt;
}

void Thread::runQueuedTask(std::shared_ptr<ITask> task) {
	if (mPoolState && !mPoolState->mAcceptingTasks.load()) {
		mPoolState->cancelTask(*task);
		return;
	}
	mThreadState = ThreadState::BUSY;
	try {
		const std::string taskName = task->getName();
		IKIGAI_PROFILE_SCOPE("Execute Task");
		IKIGAI_PROFILE_ZONE_TEXT(taskName.data(), taskName.size());
		executeTask(task);
	} catch (...) {
		// A failure before execute (for example allocating the profiling name) still settles the handle.
		if (!task->isFinished()) {
			task->cancel();
		}
		logTaskException("Task failed", std::current_exception());
	}
	mThreadState = ThreadState::FREE;
	if (mInflightCounter) {
		mInflightCounter->decrement();
	}
}

void Thread::worker(uint32_t ThreadIndex) {
	IKIGAI::PROFILER::TRACE::setThreadName(mPoolState ? "Task Pool Worker" : "Dedicated Task Worker");
	struct PoolWorkerScope {
		explicit PoolWorkerScope(const PoolState* pool) {
			gIsPoolWorker = true;
			gWorkerPool = pool;
		}
		~PoolWorkerScope() {
			gIsPoolWorker = false;
			gWorkerPool = nullptr;
		}
	} poolWorkerScope { mPoolState.get() };

	auto _id = std::this_thread::get_id();
	mId = std::make_pair(ThreadIndex, _id);
	mThreadState = ThreadState::FREE;

	if (mPoolState) {
		// Do not block forever on this thread's queue: an idle worker has to wake up and steal.
		while (!mIsDone.load()) {
			if (auto task = tryPopLocal()) {
				runQueuedTask(std::move(*task));
				continue;
			}
			if (auto job = mPoolState->popJob()) {
				mThreadState = ThreadState::BUSY;
				mPoolState->runJob(*job);
				mThreadState = ThreadState::FREE;
				continue;
			}
			if (auto task = trySteal()) {
				runQueuedTask(std::move(*task));
				continue;
			}
			mPoolState->waitForWork([this]() { return mIsDone.load() || hasPendingWork(); });
		}
	}
	else {
		while (!mIsDone.load()) {
			if (auto task = mBoundQueue.waitPop()) {
				runQueuedTask(std::move(*task));
			}
		}
	}

	mThreadState = ThreadState::FREE;
}
