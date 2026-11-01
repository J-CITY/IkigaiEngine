#include "task.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <utility>

#include "safeQueue.h"
using namespace IKIGAI::TASK;

namespace IKIGAI::TASK {
struct PoolState {
	std::mutex mIdleMutex;
	std::condition_variable mIdleCv;
	// Queues of tasks scheduled with threadID < 0. Stable for the lifetime of the workers.
	std::vector<ThreadSafeQueue<std::shared_ptr<ITask>>*> mUnboundQueues;

	void notifyWork() {
		std::lock_guard<std::mutex> lock { mIdleMutex };
		mIdleCv.notify_all();
	}

	template <typename Pred> void waitForWork(Pred&& pred) {
		std::unique_lock<std::mutex> lock { mIdleMutex };
		mIdleCv.wait_for(lock, std::chrono::milliseconds(2), std::forward<Pred>(pred));
	}
};
}

namespace {
thread_local bool gIsPoolWorker = false;

bool queueHasWork(ThreadSafeQueue<std::shared_ptr<ITask>>& queue) {
	return queue.isValid() && !queue.empty();
}
}

bool IKIGAI::TASK::isPoolWorkerThread() {
	return gIsPoolWorker;
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

void ITask::markFinished() {
	std::vector<std::function<void()>> continuations;
	{
		std::lock_guard<std::mutex> lock { mMutex };
		if (mIsFinished) {
			return;
		}
		mIsFinished = true;
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

bool ThreadPoolExecutor::setup(std::optional<unsigned> threadCount) {
	// TaskSystem::setup constructs the pool (which starts threads) and then calls setup again.
	// Stop the previous generation before publishing a new steal list.
	if (!mThreads.empty()) {
		terminate();
	}

	if (threadCount) {
		mNumThreads = threadCount.value();
	}
	else {
		mNumThreads = std::max<size_t>(std::thread::hardware_concurrency(), 2u);
	}
	mScheduleCursor.store(0, std::memory_order_relaxed);

	mPoolState = std::make_shared<PoolState>();
	mPoolState->mUnboundQueues.reserve(mNumThreads);
	mThreads.resize(mNumThreads);

	try {
		for (std::uint32_t i = 0u; i < mNumThreads; ++i) {
			mThreads[i] = std::make_unique<Thread>(static_cast<int>(i), mInflightCounter, mPoolState);
		}
		for (std::uint32_t i = 0u; i < mNumThreads; ++i) {
			mThreads[i]->start();
		}
	} catch (...) {
		terminate();
		std::cout << "PROBLEM INIT THREAD" << std::endl;
		return false;
	}

	return true;
}

bool ThreadPoolExecutor::initialize() {
	return true;
}

bool ThreadPoolExecutor::update() {
	return true;
}

ThreadPoolExecutor::~ThreadPoolExecutor() {
	terminate();
}

bool ThreadPoolExecutor::terminate() {
	// Join every worker before destroying any queue so an in-flight steal cannot touch a dead Thread.
	for (auto& thread : mThreads) {
		if (thread) {
			thread->requestStop();
		}
	}
	if (mPoolState) {
		mPoolState->notifyWork();
	}
	for (auto& thread : mThreads) {
		if (thread) {
			thread->join();
		}
	}
	mThreads.clear();
	mPoolState.reset();
	return true;
}

void ThreadPoolExecutor::waitSync() {
	if (mThreads.empty()) {
		return;
	}
	mInflightCounter->waitUntilIdle();
}

std::shared_ptr<ITask> ThreadPoolExecutor::addTask(std::unique_ptr<ITask>&& task, int32_t threadID) {
	return schedule(std::shared_ptr<ITask> { std::move(task) }, threadID, false);
}

std::shared_ptr<ITask> ThreadPoolExecutor::addTaskFront(std::unique_ptr<ITask>&& task, int32_t threadID) {
	return schedule(std::shared_ptr<ITask> { std::move(task) }, threadID, true);
}

std::shared_ptr<ITask> ThreadPoolExecutor::schedule(std::shared_ptr<ITask> task, int threadID, bool front) {
	if (mThreads.empty() || !task) {
		return nullptr;
	}

	// Explicit thread ids stay bound. Anything else is unbound and may be stolen later.
	const bool stealable = threadID < 0;
	size_t threadIndex = 0;
	if (!stealable) {
		threadIndex = static_cast<size_t>(threadID) % mThreads.size();
	}
	else {
		threadIndex = mScheduleCursor.fetch_add(1, std::memory_order_relaxed) % mThreads.size();
	}

	// Counted from schedule time (also while waiting on upstream); decremented once by whichever thread executes it.
	mInflightCounter->increment();

	std::shared_ptr<ITask> upstreamTask = task->getUpstreamTask();
	if (upstreamTask) {
		// Not queued until upstream finishes (finished or failed). Runs immediately if already finished.
		// Continuations keep the original binding: explicit threadID stays non-stealable, -1 stays stealable.
		upstreamTask->continueWith([this, task, threadIndex, front, stealable]() { enqueue(task, threadIndex, front, stealable); });
	}
	else {
		enqueue(task, threadIndex, front, stealable);
	}
	return task;
}

void ThreadPoolExecutor::enqueue(const std::shared_ptr<ITask>& task, size_t threadIndex, bool front, bool stealable) {
	if (threadIndex >= mThreads.size() || !mThreads[threadIndex]) {
		// Pool terminated while task was waiting on upstream: drop it but keep inflight balanced.
		mInflightCounter->decrement();
		return;
	}
	std::shared_ptr<ITask> queuedTask = task;
	if (front) {
		mThreads[threadIndex]->addTaskFront(std::move(queuedTask), stealable);
	}
	else {
		mThreads[threadIndex]->addTask(std::move(queuedTask), stealable);
	}
}

size_t ThreadPoolExecutor::getThreadCounts() {
	return mNumThreads;
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

void Thread::executeTask(std::shared_ptr<ITask>&& task) {
	task->execute();
}

bool Thread::hasPendingWork() {
	if (queueHasWork(mBoundQueue) || queueHasWork(mUnboundQueue)) {
		return true;
	}
	if (!mPoolState) {
		return false;
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
	mThreadState = ThreadState::BUSY;
	const std::string taskName = task->getName();
	try {
		executeTask(std::move(task));
	} catch (...) {
		std::string context = "Task '" + taskName + "' failed";
		logTaskException(context.c_str(), std::current_exception());
	}
	mThreadState = ThreadState::FREE;
	if (mInflightCounter) {
		mInflightCounter->decrement();
	}
}

void Thread::worker(uint32_t ThreadIndex) {
	struct PoolWorkerScope {
		PoolWorkerScope() {
			gIsPoolWorker = true;
		}
		~PoolWorkerScope() {
			gIsPoolWorker = false;
		}
	} poolWorkerScope;

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
