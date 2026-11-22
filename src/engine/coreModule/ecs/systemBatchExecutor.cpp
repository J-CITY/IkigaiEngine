#include "systemBatchExecutor.h"
#include "taskModule/taskSystem.h"
#include "traceModule/trace.h"

#include <cstring>
#include <exception>
#include <stdexcept>
#include <utility>

namespace IKIGAI::ECS {

#ifndef __EMSCRIPTEN__
namespace {
	struct SystemBatchWork {
		std::span<const char* const> names;
		const std::function<void(std::size_t)>& runJob;
	};

	void executeSystemJobs(void* payload, std::size_t begin, std::size_t end) {
		auto& work = *static_cast<SystemBatchWork*>(payload);
		for (std::size_t index = begin; index < end; ++index) {
			IKIGAI_PROFILE_SCOPE("Execute ECS System");
			[[maybe_unused]] const char* name = work.names[index] ? work.names[index] : "";
			IKIGAI_PROFILE_ZONE_TEXT(name, std::strlen(name));
			work.runJob(index);
		}
	}
}
#endif

SystemBatchExecutor makeTaskSystemBatchExecutor(std::weak_ptr<TASK::TaskSystem> tasks) {
#ifdef __EMSCRIPTEN__
	(void)tasks;
	return {};
#else
	return [tasks = std::move(tasks)](std::span<const char* const> names, const std::function<void(std::size_t)>& runJob) {
		if (names.size() < 2) {
			return false;
		}
		const auto pool = tasks.lock();
		if (!pool || pool->isFrameWorkerThread() || !pool->isReady() || pool->getThreadCounts() == 0) {
			return false;
		}

		SystemBatchWork work { names, runJob };
		const TASK::JobDescriptor job { &executeSystemJobs, &work, "ECS System", TASK::JobAffinity::AnyCpu };
		auto batch = pool->createJobBatch();
		TASK::JobBatchResult result {};
		try {
			for (std::size_t index = 0; index < names.size(); ++index) {
				if (!pool->submit(batch, job, index, index + 1)) {
					if (index == 0) {
						return false;
					}
					throw std::runtime_error("Task system rejected part of an ECS system batch");
				}
			}
			result = pool->wait(batch);
		} catch (...) {
			// Accepted jobs borrow the callback and may have already changed the world.
			pool->cancel(batch);
			(void)pool->wait(batch);
			throw;
		}
		if (result.status == TASK::JobBatchStatus::Failed) {
			std::rethrow_exception(result.exception);
		}
		if (result.status == TASK::JobBatchStatus::Cancelled) {
			throw std::runtime_error("Task system cancelled an ECS system batch");
		}
		return true;
	};
#endif
}
}
