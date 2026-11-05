#include "backends/interface/uniformBufferInterface.h"
#include "render.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	std::vector<std::byte> copyUpload(const void* data, size_t bytes) {
		if (bytes && !data) {
			throw std::invalid_argument("Upload data is null");
		}
		std::vector<std::byte> result(bytes);
		if (bytes) {
			std::memcpy(result.data(), data, bytes);
		}
		return result;
	}
}

Renderer::Renderer(DriverInterface* driver, std::unique_ptr<RenderExecutorInterface> executor)
	: mExecutor(std::move(executor))
	, mDriver(driver) {
	if (!mExecutor) {
		throw std::invalid_argument("Renderer needs an executor");
	}
	if (mDriver) {
		mDriver->requireOwnerThread();
		mDriver->setAccessBoundary([this] { prepareBackendAccess(); });
	}
}

Renderer::~Renderer() {
	try {
		shutdown();
	} catch (const std::exception& error) {
		std::cerr << "Renderer shutdown failed: " << error.what() << '\n';
	}
	if (mDriver) {
		mDriver->setAccessBoundary({});
	}
}

void Renderer::prepareBackendAccess() {
	mOwner.requireCurrentThread();
	if (mDriver) {
		mDriver->requireOwnerThread();
	}
	if (mExecutor->isExecuting() || mShutdown || mPreparingAccess) {
		return;
	}
	mPreparingAccess = true;
	try {
		if (!mActiveFrame) {
			retireFrames(true);
		}
		flush();
	} catch (...) {
		mPreparingAccess = false;
		throw;
	}
	mPreparingAccess = false;
}

DriverInterface* Renderer::getDriver() {
	prepareBackendAccess();
	return mDriver;
}

void Renderer::retireFrames(bool wait) {
	if (mRetiringFrames) {
		return;
	}
	mRetiringFrames = true;
	struct RetirementGuard {
		bool& Active;
		~RetirementGuard() {
			Active = false;
		}
	} guard { mRetiringFrames };
	while (!mSubmittedFrames.empty()) {
		auto& frame = mSubmittedFrames.front();
		if (wait) {
			mDriver->waitForCompletion(frame->completionToken());
		}
		if (!frame->retire(mDriver->completion())) {
			break;
		}
		mFreeFrameCommands.push_back(frame->commands());
		mSubmittedFrames.pop_front();
	}
}

void Renderer::beginFrame() {
	mOwner.requireCurrentThread();
	if (!mDriver || mShutdown || mActiveFrame) {
		throw std::logic_error("Cannot begin render frame");
	}
	flush();
	// One serial frame slot until the backend allocators/state support multiple in-flight frames (RC5).
	retireFrames(true);
	auto commands = mFreeFrameCommands.empty() ? std::make_shared<RenderCommandBuffer>(64 * 1024, true) : mFreeFrameCommands.back();
	if (!mFreeFrameCommands.empty()) {
		mFreeFrameCommands.pop_back();
	}
	mActiveFrame = std::make_shared<FrameContext>(mNextFrameId++, commands);
	mFrameStartSubmission = mDriver->completion().lastSubmitted().Value;
	mExecutor->beginFrame(std::move(commands));
}

void Renderer::addView(RenderViewSnapshot view) {
	mOwner.requireCurrentThread();
	if (!mActiveFrame) {
		throw std::logic_error("View snapshot needs an active frame");
	}
	mActiveFrame->addView(std::move(view));
}

std::shared_ptr<const RenderFrameSnapshot> Renderer::lastSnapshot() const {
	mOwner.requireCurrentThread();
	return mLastSnapshot;
}

void Renderer::finishFrame(GpuCompletionToken token) {
	if (!mActiveFrame) {
		return;
	}
	if (token.Value <= mFrameStartSubmission) {
		throw std::logic_error("Frame has no native GPU submission");
	}
	mActiveFrame->submitted(token);
	mExecutor->endFrame();
	mLastSnapshot = mActiveFrame->snapshot();
	mSubmittedFrames.push_back(std::move(mActiveFrame));
}

void Renderer::submit() {
	mOwner.requireCurrentThread();
	if (!mDriver) {
		throw std::logic_error("Frame submission needs a driver");
	}
	auto receipt = std::make_shared<std::optional<GpuCompletionToken>>();
	execute([driver = mDriver, receipt] {
		driver->requireOwnerThread();
		driver->submit();
		*receipt = driver->completion().lastSubmitted();
	});
	if (mActiveFrame) {
		mActiveFrame->seal();
	}
	flush();
	if (!*receipt) {
		throw std::logic_error("Frame submission was cancelled before native execution");
	}
	finishFrame(**receipt);
}

bool Renderer::hasActiveFrame() const {
	mOwner.requireCurrentThread();
	return bool(mActiveFrame);
}

void Renderer::shutdown() {
	mOwner.requireCurrentThread();
	if (mShutdown) {
		return;
	}
	if (mActiveFrame) {
		mExecutor->cancel();
		// A failed present may already have submitted work. Do not submit an ended native list twice.
		if (mDriver->hasOpenRecording()) {
			mDriver->requireOwnerThread();
			mDriver->end();
		}
		const auto token = mDriver->completion().lastSubmitted();
		mActiveFrame->cancelledSubmission(token);
		mExecutor->endFrame();
		mDriver->waitForCompletion(token);
		if (mDriver->mFrameGeneration) {
			mDriver->mFrameGeneration->waitForPresents();
		}
		if (!mActiveFrame->retire(mDriver->completion())) {
			throw std::logic_error("Backend wait returned before GPU completion");
		}
		mActiveFrame.reset();
	}
	if (mDriver) {
		retireFrames(true);
	}
	mExecutor->cancel();
	mFreeFrameCommands.clear();
	mShutdown = true;
}

void Renderer::updateUniformBuffer(std::shared_ptr<UniformBufferInterface> buffer, const void* data, size_t bytes, size_t offset) {
	mOwner.requireCurrentThread();
	if (!buffer || offset > std::numeric_limits<size_t>::max() - bytes) {
		throw std::invalid_argument("Invalid uniform buffer upload");
	}
	auto payload = copyUpload(data, bytes);
	execute([buffer = std::move(buffer), payload = std::move(payload), offset] { buffer->setData(payload.data(), payload.size(), offset); });
}

void Renderer::updateStorageBuffer(std::shared_ptr<StorageBufferInterface> buffer, const void* data, size_t count, size_t stride) {
	mOwner.requireCurrentThread();
	if (!buffer || !stride || count > std::numeric_limits<size_t>::max() / stride) {
		throw std::invalid_argument("Invalid storage buffer upload");
	}
	// Null initial data is valid for allocation-only storage buffers.
	auto payload = data ? copyUpload(data, count * stride) : std::vector<std::byte> {};
	execute([buffer = std::move(buffer), payload = std::move(payload), count, stride, hasData = data != nullptr] {
		buffer->setData(hasData ? payload.data() : nullptr, count, stride);
	});
}

void Renderer::updateStorageBufferRange(std::shared_ptr<StorageBufferInterface> buffer, const void* data, size_t bytes, size_t offset) {
	mOwner.requireCurrentThread();
	if (!buffer || offset > std::numeric_limits<size_t>::max() - bytes) {
		throw std::invalid_argument("Invalid storage buffer range");
	}
	auto payload = copyUpload(data, bytes);
	execute([buffer = std::move(buffer), payload = std::move(payload), offset] {
		if (offset > buffer->getSizeByte() || payload.size() > buffer->getSizeByte() - offset) {
			throw std::invalid_argument("Storage buffer upload exceeds its recorded logical range");
		}
		buffer->setSubData(payload.data(), payload.size(), offset);
	});
}
}
