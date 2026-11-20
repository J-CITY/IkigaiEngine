#include "systemBatchExecutor.h"
#include "taskModule/taskSystem.h"

#include <exception>
#include <utility>
#include <vector>

namespace IKIGAI::ECS {

SystemBatchExecutor makeTaskSystemBatchExecutor(std::weak_ptr<TASK::TaskSystem> tasks) {
#ifdef __EMSCRIPTEN__
	(void)tasks;
	return {};
#else
	return [tasks = std::move(tasks)](std::span<const char* const> names, const std::function<void(std::size_t)>& runJob) {
		const auto pool = tasks.lock();
		if (!pool || pool->isFrameWorkerThread() || !pool->isReady() || pool->getThreadCounts() == 0) {
			return false;
		}

		std::vector<TASK::TaskHandle<void>> handles;
		handles.reserve(names.size());
		std::exception_ptr failure;
		try {
			for (std::size_t index = 0; index < names.size(); ++index) {
				handles.push_back(pool->submit(names[index], -1, nullptr, [&runJob, index] { runJob(index); }));
			}
		} catch (...) {
			failure = std::current_exception();
		}

		// Drain accepted work before the caller can release its callbacks and buffers.
		for (const auto& handle : handles) {
			try {
				handle.mFuture->get();
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}
		}
		if (failure) {
			std::rethrow_exception(failure);
		}
		return true;
	};
#endif
}
}
