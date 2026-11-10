#include "taskSystem.h"

#include <chrono>
#include <stdexcept>

using namespace IKIGAI::TASK;

namespace {
constexpr unsigned ioWorkerCount = 2;
constexpr std::size_t ioCapacity = 16;

class BackgroundTask final : public ITask {
public:
	BackgroundTask(const char* name, std::function<void(std::stop_token)> work, TaskSystem::BackgroundCompletion completion, std::stop_token stop,
		bool cooperative)
		: mName { name ? name : "Background task" }
		, mWork { std::move(work) }
		, mCompletion { std::move(completion) }
		, mStop { std::move(stop) }
		, mCooperative { cooperative } { }

	std::shared_future<BackgroundResult> getResult() {
		return mPromise.get_future().share();
	}

	void execute() override {
		BackgroundResult result { BackgroundOutcome::Succeeded, BackgroundRejection::None, {} };
		try {
			mWork(mStop);
			if (mCooperative && mStop.stop_requested()) {
				result.Outcome = BackgroundOutcome::Cancelled;
			}
		} catch (...) {
			result.Outcome = BackgroundOutcome::Failed;
			result.Exception = std::current_exception();
		}
		finish(std::move(result));
	}

	const std::string getName() override {
		return mName;
	}

	const std::shared_ptr<ITask>& getUpstreamTask() override {
		return mUpstream;
	}

private:
	void cancel() override {
		finish({ BackgroundOutcome::Cancelled, BackgroundRejection::None, {} });
	}

	void finish(BackgroundResult result) {
		mWork = {};
		if (mCompletion) {
			try {
				mCompletion(result);
			} catch (...) {
				logTaskException("Background completion failed", std::current_exception());
			}
			mCompletion = {};
		}
		markFinished(result.Outcome == BackgroundOutcome::Cancelled);
		mPromise.set_value(std::move(result));
	}

	std::string mName;
	std::function<void(std::stop_token)> mWork;
	TaskSystem::BackgroundCompletion mCompletion;
	std::stop_token mStop;
	bool mCooperative = false;
	std::promise<BackgroundResult> mPromise;
	std::shared_ptr<ITask> mUpstream = {};
};
}

BackgroundTicket::BackgroundTicket(BackgroundResult rejection)
	: mRejection { std::move(rejection) } { }

BackgroundTicket::BackgroundTicket(std::shared_future<BackgroundResult> completion)
	: mCompletion { std::move(completion) } { }

bool BackgroundTicket::accepted() const {
	return mCompletion.valid();
}

bool BackgroundTicket::ready() const {
	return !accepted() || mCompletion.wait_for(std::chrono::seconds { 0 }) == std::future_status::ready;
}

BackgroundResult BackgroundTicket::get() const {
	if (!accepted()) {
		return mRejection;
	}
	if (!ready() && isTaskExecutorCallback()) {
		throw std::logic_error("Executor callbacks cannot block on pending background work");
	}
	return mCompletion.get();
}

TaskSystem::~TaskSystem() {
	terminate();
}

TaskSystemStatus TaskSystem::getStatus() const {
	return mStatus.load();
}

bool TaskSystem::isAccepting() const {
	const auto status = getStatus();
	return status == TaskSystemStatus::START || status == TaskSystemStatus::ACTIVE;
}

bool TaskSystem::isReady() const {
	return isAccepting() && mThreadPoolExecutor.isReady();
}

bool TaskSystem::isBackgroundReady() const {
	return isAccepting() && mBackgroundExecutor.isReady();
}

bool TaskSystem::isIoReady() const {
	return isAccepting() && mIoExecutor.isReady();
}

bool TaskSystem::isFrameWorkerThread() const {
	return mThreadPoolExecutor.isExecutingOnThisThread();
}

bool TaskSystem::setup() {
	std::lock_guard lock { mLifecycleMutex };
	if (getStatus() != TaskSystemStatus::TERMINANED) {
		return false;
	}
	try {
		mWorkerStop = std::stop_source {};
	} catch (...) {
		logTaskException("Task worker stop source startup failed", std::current_exception());
		return false;
	}
	if (!mThreadPoolExecutor.setup()) {
		return false;
	}
	if (!mBackgroundExecutor.setup(2)) {
		mThreadPoolExecutor.terminate();
		return false;
	}
	if (!mIoExecutor.setup(ioWorkerCount)) {
		mBackgroundExecutor.terminate();
		mThreadPoolExecutor.terminate();
		return false;
	}
	mStatus.store(TaskSystemStatus::START);
	return true;
}

bool TaskSystem::initialize() {
	std::lock_guard lock { mLifecycleMutex };
	if (getStatus() != TaskSystemStatus::START || !mThreadPoolExecutor.initialize() || !mBackgroundExecutor.initialize()
		|| !mIoExecutor.initialize()) {
		return false;
	}
	mStatus.store(TaskSystemStatus::ACTIVE);
	return true;
}

bool TaskSystem::update() {
	return getStatus() == TaskSystemStatus::ACTIVE && mThreadPoolExecutor.update() && mBackgroundExecutor.update() && mIoExecutor.update();
}

bool TaskSystem::terminate() {
	if (mThreadPoolExecutor.isExecutingOnThisThread() || mBackgroundExecutor.isExecutingOnThisThread() || mIoExecutor.isExecutingOnThisThread()) {
		throw std::logic_error("Cannot terminate the task system from one of its callbacks");
	}
	std::unique_lock lock { mLifecycleMutex };
	if (getStatus() == TaskSystemStatus::STOPPING) {
		if (mStoppingThread == std::this_thread::get_id()) {
			throw std::logic_error("Cannot terminate the task system from its shutdown callback");
		}
		mLifecycleCondition.wait(lock, [this]() { return getStatus() != TaskSystemStatus::STOPPING; });
		return true;
	}
	if (getStatus() == TaskSystemStatus::TERMINANED) {
		return true;
	}
	mStoppingThread = std::this_thread::get_id();
	mStatus.store(TaskSystemStatus::STOPPING);
	lock.unlock();

	mBackgroundExecutor.closeAdmission();
	mIoExecutor.closeAdmission();
	mThreadPoolExecutor.closeAdmission();
	mWorkerStop.request_stop();
	mThreadPoolExecutor.cancelWaitingTasks();
	mIoExecutor.terminate();
	mBackgroundExecutor.terminate();
	mThreadPoolExecutor.terminate();

	lock.lock();
	mStoppingThread = {};
	mStatus.store(TaskSystemStatus::TERMINANED);
	lock.unlock();
	mLifecycleCondition.notify_all();
	return true;
}

void TaskSystem::waitSync() {
	mThreadPoolExecutor.waitSync();
}

JobBatch TaskSystem::createJobBatch() {
	return isAccepting() ? mThreadPoolExecutor.createJobBatch() : JobBatch {};
}

bool TaskSystem::submit(JobBatch& batch, JobDescriptor job, std::size_t begin, std::size_t end, std::span<const JobBatch* const> prerequisites) {
	return isAccepting() && mThreadPoolExecutor.submit(batch, job, begin, end, prerequisites);
}

void TaskSystem::close(JobBatch& batch) {
	mThreadPoolExecutor.close(batch);
}

void TaskSystem::cancel(JobBatch& batch) {
	mThreadPoolExecutor.cancel(batch);
}

JobBatchResult TaskSystem::wait(JobBatch& batch) {
	return mThreadPoolExecutor.wait(batch);
}

BackgroundTicket TaskSystem::dispatch(const char* name, std::function<void()> work, BackgroundCompletion completion) {
	if (!work) {
		return BackgroundTicket { BackgroundResult { BackgroundOutcome::Rejected, BackgroundRejection::InvalidWork, {} } };
	}
	try {
		return dispatchImpl(mBackgroundExecutor, name, [work = std::move(work)](std::stop_token) { work(); }, std::move(completion), false);
	} catch (...) {
		return BackgroundTicket { BackgroundResult { BackgroundOutcome::Rejected, BackgroundRejection::SchedulingFailed, std::current_exception() } };
	}
}

BackgroundTicket TaskSystem::dispatch(const char* name, std::function<void(std::stop_token)> work, BackgroundCompletion completion) {
	if (!work) {
		return BackgroundTicket { BackgroundResult { BackgroundOutcome::Rejected, BackgroundRejection::InvalidWork, {} } };
	}
	return dispatchImpl(mBackgroundExecutor, name, std::move(work), std::move(completion), true);
}

BackgroundTicket TaskSystem::dispatchIo(const char* name, std::function<void(std::stop_token)> work) {
	if (!work) {
		return BackgroundTicket { BackgroundResult { BackgroundOutcome::Rejected, BackgroundRejection::InvalidWork, {} } };
	}
	return dispatchImpl(mIoExecutor, name, std::move(work), {}, true, ioCapacity);
}

BackgroundTicket TaskSystem::dispatchImpl(ThreadPoolExecutor& executor, const char* name, std::function<void(std::stop_token)> work,
	BackgroundCompletion completion, bool cooperative, std::size_t capacity) {
	std::lock_guard lock { mLifecycleMutex };
	if (getStatus() == TaskSystemStatus::STOPPING) {
		return BackgroundTicket { BackgroundResult { BackgroundOutcome::Rejected, BackgroundRejection::Stopping, {} } };
	}
	if (!isAccepting() || !executor.isReady()) {
		return BackgroundTicket { BackgroundResult { BackgroundOutcome::Rejected, BackgroundRejection::Unavailable, {} } };
	}
	if (capacity != 0 && executor.getUnfinishedWorkCount() >= capacity) {
		return BackgroundTicket { BackgroundResult { BackgroundOutcome::Rejected, BackgroundRejection::QueueFull, {} } };
	}
	try {
		auto task = std::make_unique<BackgroundTask>(name, std::move(work), std::move(completion), mWorkerStop.get_token(), cooperative);
		auto result = task->getResult();
		if (executor.addTask(std::move(task), -1)) {
			return BackgroundTicket { std::move(result) };
		}
	} catch (...) {
		return BackgroundTicket { BackgroundResult { BackgroundOutcome::Rejected, BackgroundRejection::SchedulingFailed, std::current_exception() } };
	}
	return BackgroundTicket { BackgroundResult { BackgroundOutcome::Rejected, BackgroundRejection::SchedulingFailed, {} } };
}

size_t TaskSystem::getThreadCounts() {
	return isAccepting() ? mThreadPoolExecutor.getThreadCounts() : 0;
}

std::shared_ptr<ITask> TaskSystem::addTask(std::unique_ptr<ITask>&& task, int threadID) {
	return isAccepting() ? mThreadPoolExecutor.addTask(std::move(task), threadID) : nullptr;
}

std::shared_ptr<ITask> TaskSystem::addTaskFront(std::unique_ptr<ITask>&& task, int threadID) {
	return isAccepting() ? mThreadPoolExecutor.addTaskFront(std::move(task), threadID) : nullptr;
}

std::shared_ptr<ITask> TaskSystem::addTaskInNewThread(std::unique_ptr<ITask>&& task) {
	if (!mNewThreadExecutor) {
		throw std::logic_error("Dedicated task executor is not supported by TaskSystem");
	}
	return isAccepting() ? mNewThreadExecutor->addTask(std::move(task)) : nullptr;
}
