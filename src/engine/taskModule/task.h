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

#include "safeQueue.h"

namespace IKIGAI {
namespace TASK {
	class ITask;

	// Logs an exception captured from a task or future to std::cerr (no engine services needed).
	void logTaskException(const char* context, const std::exception_ptr& exception);

	// True while executing on a Thread::worker (pool threads and dedicated new-thread tasks).
	bool isPoolWorkerThread();

	struct PoolState;

	class ThreadPoolExecutor;

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

		void start();
		void requestStop();
		void join();
		void signalWork();
		bool hasPendingWork();
		std::optional<std::shared_ptr<ITask>> tryPopLocal();
		std::optional<std::shared_ptr<ITask>> trySteal();
		void runQueuedTask(std::shared_ptr<ITask> task);

		std::string getThreadID();

		void worker(uint32_t ThreadIndex);

		void executeTask(std::shared_ptr<ITask>&& task);

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
		void wait();
		// Runs continuation once this task is finished (immediately if already finished). Never runs under the task mutex.
		void continueWith(std::function<void()> continuation);

	protected:
		// Idempotent. Marks finished, wakes waiters and runs continuations outside the lock.
		void markFinished();

	private:
		std::mutex mMutex;
		std::condition_variable mCondition;
		bool mIsFinished = false;
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
			mFunctor();
		}

		const std::string getName() override {
			return mName;
		}

		const std::shared_ptr<ITask>& getUpstreamTask() override {
			return mUpstreamTask;
		}

	private:
		Functor mFunctor;
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
		std::atomic_size_t mNumThreads = 0;
		std::shared_ptr<InflightCounter> mInflightCounter = std::make_shared<InflightCounter>();
		std::vector<std::unique_ptr<Thread>> mThreads;
		std::shared_ptr<PoolState> mPoolState;
		std::atomic<size_t> mScheduleCursor { 0 };

	public:
		ThreadPoolExecutor() {
			setup(std::nullopt);
		};
		ThreadPoolExecutor(int threadCount) {
			setup(threadCount);
		};
		~ThreadPoolExecutor();
		bool setup(std::optional<unsigned> threadCount = std::nullopt);
		bool initialize();
		bool update();
		bool terminate();

		// Blocks until every scheduled task (including ones waiting on upstream) has finished executing.
		void waitSync();

		std::shared_ptr<ITask> addTask(std::unique_ptr<ITask>&& task, int threadID);
		std::shared_ptr<ITask> addTaskFront(std::unique_ptr<ITask>&& task, int threadID);
		size_t getThreadCounts();

	private:
		// Single schedule path. threadID < 0 assigns a thread by atomic round-robin and the task is stealable.
		// An explicit threadID is bound to that thread. Returns nullptr when the pool has no threads.
		std::shared_ptr<ITask> schedule(std::shared_ptr<ITask> task, int threadID, bool front);
		void enqueue(const std::shared_ptr<ITask>& task, size_t threadIndex, bool front, bool stealable);
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
