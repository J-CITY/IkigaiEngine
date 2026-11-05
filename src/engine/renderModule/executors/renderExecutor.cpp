#include "renderExecutor.h"
#include <stdexcept>

namespace IKIGAI::RENDER {
RenderExecutorInterface::RenderExecutorInterface(CommandDispatch dispatch, size_t blockSize)
	: mDispatch(dispatch)
	, mStandaloneCommands(std::make_shared<RenderCommandBuffer>(blockSize))
	, mCommands(mStandaloneCommands) {
	if (!mDispatch) {
		throw std::invalid_argument("Render command dispatch is null");
	}
}

void RenderExecutorInterface::executePending(DriverInterface* driver) {
	mCommands->execute(driver, mDispatch);
}

void RenderExecutorInterface::cancel() noexcept {
	mCommands->cancel();
}

size_t RenderExecutorInterface::pendingCommandCount() const noexcept {
	return mCommands->pendingCommandCount();
}

size_t RenderExecutorInterface::reservedBytes() const noexcept {
	return mCommands->reservedBytes();
}

void RenderExecutorInterface::beginFrame(std::shared_ptr<RenderCommandBuffer> commands) {
	if (!commands || mCommands != mStandaloneCommands || mCommands->pendingCommandCount()) {
		throw std::logic_error("Cannot replace an active render command buffer");
	}
	mCommands = std::move(commands);
}

void RenderExecutorInterface::endFrame() {
	if (mCommands->pendingCommandCount()) {
		throw std::logic_error("Frame still has pending render commands");
	}
	mCommands = mStandaloneCommands;
}

bool RenderExecutorInterface::isExecuting() const noexcept {
	return mCommands->isExecuting();
}

ImmediateExecutor::ImmediateExecutor(CommandDispatch dispatch, size_t blockSize)
	: RenderExecutorInterface(dispatch, blockSize) { }

void ImmediateExecutor::onSubmitted(DriverInterface* driver) {
	executePending(driver);
}

void ImmediateExecutor::flush(DriverInterface* driver) {
	executePending(driver);
}

BufferedExecutor::BufferedExecutor(CommandDispatch dispatch, size_t blockSize)
	: RenderExecutorInterface(dispatch, blockSize) { }

void BufferedExecutor::onSubmitted(DriverInterface*) { }

void BufferedExecutor::flush(DriverInterface* driver) {
	executePending(driver);
}
}
