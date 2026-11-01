#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "task.h"

namespace IKIGAI {
namespace TASK {
	enum class TaskSystemStatus { START, ACTIVE, SUSPEND, TERMINANED };

	template <typename T> struct TaskHandle {
		std::shared_ptr<ITask> mTask;
		std::shared_ptr<Future<T>> mFuture;
	};

	class TaskSystem {
	public:
		bool setup();
		bool initialize();
		bool update();
		bool terminate();

		TaskSystemStatus getStatus();
		bool isReady() const {
			return static_cast<bool>(mThreadPoolExecutor);
		}

		void waitSync();
		// CPU background work is independent of the per-frame waitSync fence.
		void dispatch(const char* name, std::function<void()> work);

		// Calls func(index) once for every index in [begin, end).
		// grainSize == 0 splits the range into about one chunk per pool thread.
		// A call made on a pool worker runs the whole range inline so the worker does not wait on the pool it belongs to.
		template <typename Func> void parallelFor(size_t begin, size_t end, Func&& func, size_t grainSize = 0);

		size_t getThreadCounts();

		template <typename Func, typename... Args>
		auto submit(const char* name, int32_t threadID, const std::shared_ptr<ITask>& upstreamTask, Func&& func, Args&&... args) {
			auto boundTask = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
			using ResultType = std::invoke_result_t<decltype(boundTask)>;
			using PackagedTask = std::packaged_task<ResultType()>;

			PackagedTask _task { std::move(boundTask) };
			Future<ResultType> _future { _task.get_future() };
			TaskHandle<ResultType> _handle;
			_handle.mTask = addTask(std::make_unique<Task<PackagedTask>>(std::move(_task), name, upstreamTask), threadID);
			_handle.mFuture = std::make_shared<Future<ResultType>>(std::move(_future));
			return _handle;
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
			using PackagedTask = std::function<ResultType()>;

			PackagedTask _task { std::move(boundTask) };
			Future<ResultType> _future { _task.get_future() };
			TaskHandle<ResultType> _handle;
			_handle.mTask = addTask(std::make_unique<Task<PackagedTask>>(std::move(_task), name, upstreamTask));
			_handle.mFuture = std::make_shared<Future<ResultType>>(std::move(_future));
			return _handle;
		}

	protected:
		std::unique_ptr<ThreadPoolExecutor> mThreadPoolExecutor;
		std::unique_ptr<NewThreadExecutor> mNewThreadExecutor;
		std::unique_ptr<ThreadPoolExecutor> mBackgroundExecutor = {};

		std::shared_ptr<ITask> addTask(std::unique_ptr<ITask>&& task, int threadID);
		std::shared_ptr<ITask> addTaskFront(std::unique_ptr<ITask>&& task, int threadID);
		std::shared_ptr<ITask> addTaskInNewThread(std::unique_ptr<ITask>&& task);
	};

	template <typename Func> void TaskSystem::parallelFor(size_t begin, size_t end, Func&& func, size_t grainSize) {
		if (begin >= end) {
			return;
		}

		const bool runInline = isPoolWorkerThread() || !mThreadPoolExecutor || mThreadPoolExecutor->getThreadCounts() == 0;
		if (runInline) {
			for (size_t index = begin; index < end; ++index) {
				func(index);
			}
			return;
		}

		const size_t range = end - begin;
		const size_t workers = std::max<size_t>(1, mThreadPoolExecutor->getThreadCounts());
		const size_t chunkSize = grainSize == 0 ? std::max<size_t>(1, (range + workers - 1) / workers) : std::max<size_t>(1, grainSize);

		using Fn = std::decay_t<Func>;
		auto sharedFunc = std::make_shared<Fn>(std::forward<Func>(func));
		std::vector<std::shared_ptr<ITask>> chunks;
		chunks.reserve((range + chunkSize - 1) / chunkSize);

		for (size_t offset = 0; offset < range; offset += chunkSize) {
			const size_t chunkBegin = begin + offset;
			const size_t chunkEnd = std::min(end, chunkBegin + chunkSize);
			auto work = [sharedFunc, chunkBegin, chunkEnd]() {
				for (size_t index = chunkBegin; index < chunkEnd; ++index) {
					(*sharedFunc)(index);
				}
			};
			auto task = addTask(std::make_unique<Task<decltype(work)>>(std::move(work), "parallelFor", nullptr), -1);
			if (!task) {
				for (size_t index = chunkBegin; index < chunkEnd; ++index) {
					(*sharedFunc)(index);
				}
				continue;
			}
			chunks.push_back(std::move(task));
		}

		for (auto& chunk : chunks) {
			chunk->wait();
		}
	}
}
}
