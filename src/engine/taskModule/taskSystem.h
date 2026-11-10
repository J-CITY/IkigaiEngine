#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <vector>

#include "backgroundTicket.h"
#include "task.h"

namespace IKIGAI {
namespace TASK {
	enum class TaskSystemStatus { START, ACTIVE, SUSPEND, TERMINANED, STOPPING };

	template <typename T> struct TaskHandle {
		std::shared_ptr<ITask> mTask;
		std::shared_ptr<Future<T>> mFuture;
	};

	class TaskSystem {
	public:
		~TaskSystem();

		bool setup();
		bool initialize();
		bool update();
		bool terminate();

		[[nodiscard]] TaskSystemStatus getStatus() const;
		[[nodiscard]] bool isReady() const;
		[[nodiscard]] bool isBackgroundReady() const;
		[[nodiscard]] bool isIoReady() const;
		[[nodiscard]] bool isFrameWorkerThread() const;

		void waitSync();
		[[nodiscard]] JobBatch createJobBatch();
		[[nodiscard]] bool submit(
			JobBatch& batch, JobDescriptor job, std::size_t begin, std::size_t end, std::span<const JobBatch* const> prerequisites = {});
		void close(JobBatch& batch);
		void cancel(JobBatch& batch);
		[[nodiscard]] JobBatchResult wait(JobBatch& batch);

		// CPU background work is independent of the per-frame waitSync fence. Completion runs once for accepted work,
		// on the worker or shutdown caller. The ticket becomes ready once completion returns, which may happen before dispatch returns.
		// Running no-argument work drains. Cooperative work receives a stop request and a normal return then reports Cancelled.
		using BackgroundCompletion = std::function<void(const BackgroundResult&)>;
		BackgroundTicket dispatch(const char* name, std::function<void()> work, BackgroundCompletion completion = {});
		BackgroundTicket dispatch(const char* name, std::function<void(std::stop_token)> work, BackgroundCompletion completion = {});
		// Blocking reads use a dedicated lane. Saturation returns QueueFull immediately, so an owner may retry on a later frame.
		BackgroundTicket dispatchIo(const char* name, std::function<void(std::stop_token)> work);

		// Calls func(index) once for every index in [begin, end).
		// grainSize == 0 splits the range into about one chunk per pool thread.
		// Calls from a frame pool callback run inline to avoid waiting on the same pool.
		template <typename Func> void parallelFor(size_t begin, size_t end, Func&& func, size_t grainSize = 0);

		size_t getThreadCounts();

		template <typename Func, typename... Args>
		auto submit(const char* name, int32_t threadID, const std::shared_ptr<ITask>& upstreamTask, Func&& func, Args&&... args) {
			auto boundTask = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
			using ResultType = std::invoke_result_t<decltype(boundTask)>;
			using PackagedTask = std::packaged_task<ResultType()>;

			PackagedTask packagedTask { std::move(boundTask) };
			auto future = packagedTask.get_future();
			TaskHandle<ResultType> handle;
			auto task = std::make_unique<Task<PackagedTask>>(std::move(packagedTask), name, upstreamTask);
			handle.mFuture = std::make_shared<Future<ResultType>>(std::move(future));
			handle.mTask = addTask(std::move(task), threadID);
			return handle;
		}

		template <typename Func, typename... Args>
		auto submitImmediately(const char* name, int32_t threadID, const std::shared_ptr<ITask>& upstreamTask, Func&& func, Args&&... args) {
			auto boundTask = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
			using ResultType = std::invoke_result_t<decltype(boundTask)>;
			using PackagedTask = std::packaged_task<ResultType()>;

			PackagedTask _task { std::move(boundTask) };
			Future<ResultType> _future { _task.get_future() };
			TaskHandle<ResultType> _handle;
			_handle.mTask = addTaskFront(std::make_unique<Task<PackagedTask>>(std::move(_task), name, upstreamTask), threadID);
			_handle.mFuture = std::make_shared<Future<ResultType>>(std::move(_future));
			return _handle;
		}

		template <typename Func, typename... Args>
		auto submitInNewThread(const char* name, const std::shared_ptr<ITask>& upstreamTask, Func&& func, Args&&... args) {
			auto boundTask = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
			using ResultType = std::invoke_result_t<decltype(boundTask)>;
			using PackagedTask = std::packaged_task<ResultType()>;

			PackagedTask _task { std::move(boundTask) };
			auto _future = _task.get_future();
			TaskHandle<ResultType> _handle;
			_handle.mTask = addTaskInNewThread(std::make_unique<Task<PackagedTask>>(std::move(_task), name, upstreamTask));
			_handle.mFuture = std::make_shared<Future<ResultType>>(std::move(_future));
			return _handle;
		}

	private:
		ThreadPoolExecutor mThreadPoolExecutor;
		ThreadPoolExecutor mBackgroundExecutor;
		ThreadPoolExecutor mIoExecutor;
		std::unique_ptr<NewThreadExecutor> mNewThreadExecutor = {};
		std::atomic<TaskSystemStatus> mStatus = TaskSystemStatus::TERMINANED;
		std::mutex mLifecycleMutex;
		std::condition_variable mLifecycleCondition;
		std::thread::id mStoppingThread = {};
		std::stop_source mWorkerStop { std::nostopstate };

		[[nodiscard]] bool isAccepting() const;
		BackgroundTicket dispatchImpl(ThreadPoolExecutor& executor, const char* name, std::function<void(std::stop_token)> work,
			BackgroundCompletion completion, bool cooperative, std::size_t capacity = 0);

		std::shared_ptr<ITask> addTask(std::unique_ptr<ITask>&& task, int threadID);
		std::shared_ptr<ITask> addTaskFront(std::unique_ptr<ITask>&& task, int threadID);
		std::shared_ptr<ITask> addTaskInNewThread(std::unique_ptr<ITask>&& task);
	};

	template <typename Func> void TaskSystem::parallelFor(size_t begin, size_t end, Func&& func, size_t grainSize) {
		if (begin >= end) {
			return;
		}

		const size_t workers = getThreadCounts();
		if (isFrameWorkerThread() || workers == 0) {
			for (size_t index = begin; index < end; ++index) {
				func(index);
			}
			return;
		}

		const size_t range = end - begin;
		const size_t chunkSize = grainSize == 0 ? 1 + (range - 1) / workers : grainSize;

		using Fn = std::decay_t<Func>;
		auto sharedFunc = std::make_shared<Fn>(std::forward<Func>(func));
		struct Chunk {
			size_t Begin;
			size_t End;
			std::shared_ptr<ITask> Task;
			std::exception_ptr Exception;
		};
		std::vector<Chunk> chunks;
		chunks.reserve(1 + (range - 1) / chunkSize);
		const auto runChunk = [sharedFunc](Chunk& chunk) {
			try {
				for (size_t index = chunk.Begin; index < chunk.End; ++index) {
					(*sharedFunc)(index);
				}
			} catch (...) {
				chunk.Exception = std::current_exception();
			}
		};
		const auto waitForChunks = [&chunks]() {
			for (const auto& chunk : chunks) {
				if (chunk.Task) {
					chunk.Task->wait();
				}
			}
		};
		try {
			for (size_t chunkBegin = begin; chunkBegin < end;) {
				const size_t chunkEnd = chunkBegin + std::min(chunkSize, end - chunkBegin);
				auto& chunk = chunks.emplace_back(chunkBegin, chunkEnd, nullptr, nullptr);
				auto work = [runChunk, &chunk]() { runChunk(chunk); };
				chunk.Task = addTask(std::make_unique<Task<decltype(work)>>(std::move(work), "parallelFor", nullptr), -1);
				chunkBegin = chunkEnd;
			}
		} catch (...) {
			waitForChunks();
			throw;
		}
		waitForChunks();
		std::exception_ptr exception;
		for (auto& chunk : chunks) {
			if (!chunk.Task || chunk.Task->isCancelled()) {
				runChunk(chunk);
			}
			if (!exception && chunk.Exception) {
				exception = std::move(chunk.Exception);
			}
		}
		if (exception) {
			std::rethrow_exception(exception);
		}
	}
}
}
