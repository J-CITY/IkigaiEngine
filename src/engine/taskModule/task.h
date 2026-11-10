#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "jobBatch.h"
#include "safeQueue.h"

namespace IKIGAI {
namespace TASK {
	class ITask;

	// Logs an exception captured from a task or future to std::cerr (no engine services needed).
	void logTaskException(const char* context, const std::exception_ptr& exception);

	// True while executing on a Thread::worker (pool threads and dedicated new-thread tasks).
	bool isPoolWorkerThread();
	bool isTaskExecutorCallback();

	struct PoolState;

	class ThreadPoolExecutor;
	class TaskSystem;

	// Counts tasks that were scheduled (including ones waiting on upstream) but have not finished executing.
	class InflightCounter {
	public:
		void increment();
		void decrement();
		void waitUntilIdle();
		size_t getCount();

	private:
		std::mutex mMutex;
		std::condition_variable mCondition;
		size_t mCount = 0;
	};

	enum class ThreadState { FREE, BUSY };

	class Thread {
	public:
		explicit Thread(int ThreadIndex, std::shared_ptr<InflightCounter> inflightCounter = nullptr, std::shared_ptr<PoolState> poolState = nullptr);
		~Thread();

		Thread(const Thread&) = delete;
		Thread& operator=(const Thread&) = delete;

		ThreadState getState() const;
		size_t getUnfinishedWorkCount();

		// stealable tasks are unbound (scheduled with threadID < 0) and may be taken from the back by another worker.
		std::shared_ptr<ITask> addTask(std::shared_ptr<ITask>&& task, bool stealable = false);
		std::shared_ptr<ITask> addTaskFront(std::shared_ptr<ITask>&& task, bool stealable = false);

	private:
		friend class ThreadPoolExecutor;
		friend struct PoolState;

		void start();
		void requestStop();
		void cancelPendingTasks();
		void join();
		void signalWork();
		bool hasPendingWork();
		std::optional<std::shared_ptr<ITask>> tryPopLocal();
		std::optional<std::shared_ptr<ITask>> trySteal();
		void runQueuedTask(std::shared_ptr<ITask> task);

		std::string getThreadID();

		void worker(uint32_t ThreadIndex);

		void executeTask(const std::shared_ptr<ITask>& task);

		std::shared_ptr<InflightCounter> mInflightCounter;
		std::shared_ptr<PoolState> mPoolState;
		std::thread* mThreadHandle = nullptr;
		std::pair<uint32_t, std::thread::id> mId;
		std::atomic<ThreadState> mThreadState { ThreadState::FREE };
		std::atomic_bool mIsDone = false;
		size_t mIndex = 0;
		// Explicit threadID tasks. Only this worker pops them.
		ThreadSafeQueue<std::shared_ptr<ITask>> mBoundQueue;
		// threadID < 0. This worker pops the front; other workers steal from the back.
		ThreadSafeQueue<std::shared_ptr<ITask>> mUnboundQueue;
	};

	class ITask {
	public:
		ITask() = default;
		virtual ~ITask() = default;
		ITask(const ITask& rhs) = delete;
		ITask& operator=(const ITask& rhs) = delete;
		ITask(ITask&& other) = delete;
		ITask& operator=(ITask&& other) = delete;

		virtual void execute() = 0;
		virtual const std::string getName() = 0;
		virtual const std::shared_ptr<ITask>& getUpstreamTask() = 0;

		bool isFinished();
		bool isCancelled();
		void wait();
		// Runs continuation once this task is finished (immediately if already finished). Never runs under the task mutex.
		void continueWith(std::function<void()> continuation);

	protected:
		// Idempotent. Marks finished, wakes waiters and runs continuations outside the lock.
		void markFinished(bool cancelled = false);

	private:
		friend class Thread;
		friend struct PoolState;
		virtual void cancel();

		std::mutex mMutex;
		std::condition_variable mCondition;
		bool mIsFinished = false;
		bool mIsCancelled = false;
		std::vector<std::function<void()>> mContinuations;
	};

	template <typename Functor> class Task : public ITask {
	public:
		Task(Functor&& functor, const char* name, const std::shared_ptr<ITask>& upstreamTask)
			: mFunctor { std::move(functor) }
			, mName { name }
			, mUpstreamTask { upstreamTask } { }

		~Task() override = default;

		void execute() override {
			// Marks finished on normal return and on exception.
			struct FinishGuard {
				Task* mOwner;
				~FinishGuard() {
					mOwner->markFinished();
				}
			};
			FinishGuard guard { this };
			(*mFunctor)();
		}

		const std::string getName() override {
			return mName;
		}

		const std::shared_ptr<ITask>& getUpstreamTask() override {
			return mUpstreamTask;
		}

	private:
		void cancel() override {
			mFunctor.reset();
			markFinished(true);
		}

		std::optional<Functor> mFunctor;
		std::string mName;
		std::shared_ptr<ITask> mUpstreamTask;
	};

	template <typename T> class Future {
	public:
		Future(std::future<T>&& future)
			: mFuture { std::move(future) } { }

		~Future() {
			if (mFuture.valid()) {
				try {
					mFuture.get();
				} catch (...) {
					logTaskException("Future destroyed with unobserved task exception", std::current_exception());
				}
			}
		}

		Future(const Future&) = delete;
		Future& operator=(const Future&) = delete;
		Future(Future&& other) = default;
		Future& operator=(Future&& other) = default;

		auto get() {
			return mFuture.get();
		}

		void wait() {
			mFuture.wait();
		}

	private:
		std::future<T> mFuture;
	};

	class ThreadPoolExecutor {
		std::shared_ptr<InflightCounter> mInflightCounter = std::make_shared<InflightCounter>();
		std::vector<std::unique_ptr<Thread>> mThreads;
		std::atomic<std::shared_ptr<PoolState>> mPoolState;
		std::mutex mLifecycleMutex;
		std::condition_variable mLifecycleCondition;
		std::thread::id mStoppingThread = {};

	public:
		ThreadPoolExecutor() = default;
		// Existing stand-alone pools use the constructor that starts a fixed number of workers.
		explicit ThreadPoolExecutor(int threadCount);
		~ThreadPoolExecutor();
		bool setup(std::optional<unsigned> threadCount = std::nullopt);
		bool initialize();
		bool update();
		bool terminate();
		[[nodiscard]] bool isReady() const;
		[[nodiscard]] bool isExecutingOnThisThread() const;

		// Blocks until every scheduled task (including ones waiting on upstream) has finished executing.
		void waitSync();

		[[nodiscard]] JobBatch createJobBatch();
		// Each prerequisite must be a closed batch from this pool generation.
		// If any prerequisite fails or is cancelled, this job is cancelled.
		// Returns false for rejected submissions. No work was admitted in that case.
		[[nodiscard]] bool submit(
			JobBatch& batch, JobDescriptor job, std::size_t begin, std::size_t end, std::span<const JobBatch* const> prerequisites = {});
		void close(JobBatch& batch);
		// Discards queued work and lets running callbacks return. Call wait before releasing payloads.
		void cancel(JobBatch& batch);
		// Seals the batch and helps eligible jobs in it and its prerequisites.
		// Throws std::logic_error if this thread is executing a callback the batch needs to complete.
		[[nodiscard]] JobBatchResult wait(JobBatch& batch);

		std::shared_ptr<ITask> addTask(std::unique_ptr<ITask>&& task, int threadID);
		std::shared_ptr<ITask> addTaskFront(std::unique_ptr<ITask>&& task, int threadID);
		size_t getThreadCounts() const;

	private:
		friend class TaskSystem;
		void closeAdmission();
		void cancelWaitingTasks();
		size_t getUnfinishedWorkCount() const;
	};

	class NewThreadExecutor {
		std::shared_ptr<InflightCounter> mInflightCounter = std::make_shared<InflightCounter>();
		std::vector<std::unique_ptr<Thread>> mThreads;

	public:
		NewThreadExecutor() {
			setup();
		};
		bool setup();
		bool initialize();
		bool update();
		bool terminate();

		void waitSync();

		std::shared_ptr<ITask> addTask(std::unique_ptr<ITask>&& task);
		size_t getThreadCounts();
	};
}
}
