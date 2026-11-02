#pragma once
#include "entityManager.h"
#include <functional>
#include <mutex>
#include <vector>

namespace IKIGAI::ECS {
class CommandBuffer {
public:
	void addCommand(std::function<void()>&& cmd) {
		std::lock_guard guard(mMutex);
		mCommands.push_back(std::move(cmd));
	}

	void execute() {
		for (auto& cmd : mCommands) {
			cmd();
		}
		mCommands.clear();
	}

private:
	std::vector<std::function<void()>> mCommands;

	std::mutex mMutex;
};

inline CommandBuffer*& activeCommandBuffer() {
	thread_local CommandBuffer* current = nullptr;
	return current;
}

class ActiveCommandBufferScope {
public:
	explicit ActiveCommandBufferScope(CommandBuffer& buffer)
		: mPrevious(activeCommandBuffer()) {
		activeCommandBuffer() = &buffer;
	}

	~ActiveCommandBufferScope() {
		activeCommandBuffer() = mPrevious;
	}

	ActiveCommandBufferScope(const ActiveCommandBufferScope&) = delete;
	ActiveCommandBufferScope& operator=(const ActiveCommandBufferScope&) = delete;
	ActiveCommandBufferScope(ActiveCommandBufferScope&&) = delete;
	ActiveCommandBufferScope& operator=(ActiveCommandBufferScope&&) = delete;

private:
	CommandBuffer* mPrevious = nullptr;
};
}
