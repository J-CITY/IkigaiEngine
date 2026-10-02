#pragma once
#include <memory>

#include "task.h"

namespace IKIGAI {
	namespace TASK {
		enum class TaskSystemStatus {
			START,
			ACTIVE,
			SUSPEND,
			TERMINANED
		};

		template <typename T>
		struct TaskHandle {
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
			bool isReady() const { return static_cast<bool>(mThreadPoolExecutor); }

			void waitSync();

			size_t getThreadCounts();

			template <typename Func, typename... Args>
			auto submit(const char* name, int32_t threadID, const std::shared_ptr<ITask>& upstreamTask, Func&& func, Args&&... args) {
				auto boundTask = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
				using ResultType = std::invoke_result_t<decltype(boundTask)>;
				using PackagedTask = std::packaged_task<ResultType()>;

				PackagedTask _task{std::move(boundTask)};
				Future<ResultType> _future{_task.get_future()};
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

				PackagedTask _task{ std::move(boundTask) };
				Future<ResultType> _future{ _task.get_future() };
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
			
				PackagedTask _task{ std::move(boundTask) };
				Future<ResultType> _future{ _task.get_future() };
				TaskHandle<ResultType> _handle;
				_handle.mTask = addTask(std::make_unique<Task<PackagedTask>>(std::move(_task), name, upstreamTask));
				_handle.mFuture = std::make_shared<Future<ResultType>>(std::move(_future));
				return _handle;
			}
		protected:
			std::unique_ptr<ThreadPoolExecutor> mThreadPoolExecutor;
			std::unique_ptr<NewThreadExecutor> mNewThreadExecutor;

			std::shared_ptr<ITask> addTask(std::unique_ptr<ITask>&& task, int threadID);
			std::shared_ptr<ITask> addTaskFront(std::unique_ptr<ITask>&& task, int threadID);
			std::shared_ptr<ITask> addTaskInNewThread(std::unique_ptr<ITask>&& task);
		};
	}
}

