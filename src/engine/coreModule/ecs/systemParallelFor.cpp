#include "systemParallelFor.h"

#include "ecsModule/commandBuffer.h"
#include "ecsModule/componentManager.h"
#include "ecsModule/systemAccessScope.h"
#include "taskModule/taskSystem.h"

#include <algorithm>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace IKIGAI::ECS {
struct SystemRangeExecutor {
	struct RangeWork {
		const SystemAccessScope& parentScope;
		const SystemRangeCallback& runRange;
		std::size_t begin = 0;
		std::size_t end = 0;
		std::size_t grainSize = 0;
		std::vector<CommandBuffer> commands;
		std::mutex failureMutex {};
		std::exception_ptr failure {};
		std::size_t failureIndex = 0;
	};

	static void executeRanges(void* payload, std::size_t first, std::size_t last) {
		auto& work = *static_cast<RangeWork*>(payload);
		for (std::size_t index = first; index < last; ++index) {
			try {
				auto& commands = work.commands[index];
				const SystemAccessScope scope(work.parentScope, commands);
				const auto begin = work.begin + index * work.grainSize;
				const auto end = begin + std::min(work.grainSize, work.end - begin);
				work.runRange(begin, end, commands);
			} catch (...) {
				const std::lock_guard lock { work.failureMutex };
				if (!work.failure || index < work.failureIndex) {
					work.failure = std::current_exception();
					work.failureIndex = index;
				}
			}
		}
	}

	static bool runOnPool(TASK::TaskSystem& tasks, RangeWork& work) {
		if (work.commands.size() < 2 || work.parentScope.mManager.hasStructuralChangeOnThisThread() || !tasks.isReady()
			|| tasks.getThreadCounts() == 0) {
			return false;
		}

		auto batch = tasks.createJobBatch();
		const TASK::JobDescriptor job { SystemRangeExecutor::executeRanges, &work, "ECS System Range", TASK::JobAffinity::AnyCpu };
		TASK::JobBatchResult result {};
		try {
			for (std::size_t index = 0; index < work.commands.size(); ++index) {
				if (!tasks.submit(batch, job, index, index + 1)) {
					if (index == 0) {
						return false;
					}
					throw std::runtime_error("Task system rejected part of an ECS range batch");
				}
			}
			result = tasks.wait(batch);
		} catch (...) {
			tasks.cancel(batch);
			(void)tasks.wait(batch);
			throw;
		}
		if (result.status == TASK::JobBatchStatus::Failed) {
			std::rethrow_exception(result.exception);
		}
		if (result.status == TASK::JobBatchStatus::Cancelled) {
			throw std::runtime_error("Task system cancelled an ECS range batch");
		}
		return true;
	}
};

void parallelForSystem(TASK::TaskSystem& tasks, CommandBuffer& parentCommands, std::size_t begin, std::size_t end, std::size_t grainSize,
	const SystemRangeCallback& runRange) {
	if (begin >= end) {
		return;
	}
	if (grainSize == 0) {
		throw std::invalid_argument("ECS parallel ranges require a positive grain size");
	}

	const auto& parentScope = SystemAccessScope::current();
	SystemAccessScope::requireCommandBuffer(parentCommands);
	const auto rangeCount = 1 + (end - begin - 1) / grainSize;
	SystemRangeExecutor::RangeWork work { parentScope, runRange, begin, end, grainSize, std::vector<CommandBuffer>(rangeCount) };
	if (!SystemRangeExecutor::runOnPool(tasks, work)) {
		SystemRangeExecutor::executeRanges(&work, 0, rangeCount);
	}
	if (work.failure) {
		std::rethrow_exception(work.failure);
	}
	parentCommands.appendRanges(work.commands);
}
}
