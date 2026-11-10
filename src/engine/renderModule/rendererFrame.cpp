#include "backends/interface/uniformBufferInterface.h"
#include "executors/renderThread.h"
#include "profilerModule/trace.h"
#include "render.h"
#ifdef _WIN32
#include <Windows.h>
#endif
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace IKIGAI::RENDER {
namespace {
	struct AccessGuard {
		bool& Active;
		bool Previous = std::exchange(Active, true);
		~AccessGuard() {
			Active = Previous;
		}
	};

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

Renderer::Renderer(DriverInterface* driver, std::unique_ptr<RenderExecutorInterface> executor, bool threaded)
	: mExecutor(std::move(executor))
	, mDriver(driver) {
	if (!mExecutor) {
		throw std::invalid_argument("Renderer needs an executor");
	}
	if (threaded) {
		if (!mDriver) {
			throw std::invalid_argument("Threaded rendering needs a driver");
		}
		mRenderThread = std::make_unique<RenderThread>();
		if (DriverInterface::settings.pipelined()) {
			mGpuWaitThread = std::make_unique<RenderThread>(DriverInterface::settings.framesInFlight, RenderThread::Role::GPU_COMPLETION);
		}
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
	if (mExecutor->isExecuting() || mShutdown || mPreparingAccess || mRetiringFrames) {
		return;
	}
	waitForReplay();
	pollPresent();
	AccessGuard guard { mPreparingAccess };
	if (!mActiveFrame) {
		retireFrames(!mGpuWaitThread);
	}
	flush();
}

DriverInterface* Renderer::getDriver() {
	if (!mDriver || DriverInterface::sReplayDriver != mDriver) {
		prepareBackendAccess();
	}
	return mDriver;
}

void Renderer::flush() {
	mOwner.requireCurrentThread();
	waitForReplay();
	pollPresent();
	if (!mRenderThread) {
		IKIGAI_PROFILE_SCOPE("Flush Render Commands");
		mExecutor->flush(mDriver);
	}
	else {
		retireFrames(false);
		if (mExecutor->pendingCommandCount()) {
			IKIGAI_PROFILE_SCOPE("Wait for Render Replay");
			auto ticket = mRenderThread->post([this] {
				IKIGAI_PROFILE_SCOPE("Replay Render Commands");
				IKIGAI_PROFILE_ZONE_VALUE(mExecutor->pendingCommandCount());
				mDriver->withReplayAccess([this] { mExecutor->flush(mDriver); });
			});
			ticket.get();
		}
	}
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
	if (wait) {
		waitForPendingPresent();
	}
	while (!mSubmittedFrames.empty()) {
		auto& frame = mSubmittedFrames.front();
		if (wait) {
			IKIGAI_PROFILE_SCOPE("Wait for Frame Retirement");
			IKIGAI_PROFILE_ZONE_VALUE(frame->completionToken().Value);
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
	IKIGAI_PROFILE_SCOPE("Begin Render Frame");
	mOwner.requireCurrentThread();
	if (!mDriver || mShutdown || mActiveFrame) {
		throw std::logic_error("Cannot begin render frame");
	}
	// A new CPU snapshot can be built while the previous GPU frame is pending.
	// The first native access joins it before any new recording is replayed.
	if (mGpuWaitThread || mPendingPresents.empty() || mExecutor->pendingCommandCount()) {
		flush();
		retireFrames(!mGpuWaitThread);
	}
	auto commands = mFreeFrameCommands.empty() ? std::make_shared<RenderCommandBuffer>(64 * 1024, true) : mFreeFrameCommands.back();
	if (!mFreeFrameCommands.empty()) {
		mFreeFrameCommands.pop_back();
	}
	mActiveFrame = std::make_shared<FrameContext>(mNextFrameId++, commands);
	IKIGAI_PROFILE_COUNTER("Render Frame ID", mNextFrameId - 1);
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

std::shared_ptr<const FrameSubmissionReceipt> Renderer::submissionReceipt() const {
	mOwner.requireCurrentThread();
	if (!mActiveFrame) {
		throw std::logic_error("Submission receipt needs an active frame");
	}
	return mActiveFrame->submissionReceipt();
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
	IKIGAI_PROFILE_SCOPE("Publish Frame Submission");
	IKIGAI_PROFILE_ZONE_VALUE(token.Value);
	mActiveFrame->submitted(token);
	mExecutor->endFrame();
	mLastSnapshot = mActiveFrame->snapshot();
	IKIGAI_PROFILE_COUNTER("Submitted Render Frame ID", mLastSnapshot->frameId());
	mSubmittedFrames.push_back(std::move(mActiveFrame));
}

void Renderer::submit() {
	submitAsync();
	waitForPendingPresent();
}

RenderTicket Renderer::submitAsync() {
	return submitAsync({});
}

bool Renderer::supportsCpuReplayOverlap() const {
	mOwner.requireCurrentThread();
	const auto& settings = DriverInterface::settings;
	return mGpuWaitThread && (settings.backend == RenderSettings::Backend::DIRECTX12 || settings.backend == RenderSettings::Backend::VULKAN)
		&& settings.upscaler.technology == UpscalerType::OFF && settings.frameGeneration.technology == FrameGenerationType::OFF
		&& !mDriver->mFrameGenerationStatus.Active;
}

void Renderer::waitForReplay() {
	if (mPendingReplay.valid()) {
		auto ticket = std::exchange(mPendingReplay, {});
		try {
			IKIGAI_PROFILE_SCOPE("Wait for Sealed Render Replay");
			ticket.get();
		} catch (...) {
			mReplayFailure = std::current_exception();
		}
	}
	if (mReplayFailure) {
		std::rethrow_exception(mReplayFailure);
	}
}

void Renderer::replayWhilePreparing(const std::function<void()>& prepare) {
	auto replay = mExecutor->detachSealedFrame(mActiveFrame->commands());
	if (replay.Commands->pendingCommandCount()) {
		const auto frameId = mActiveFrame->snapshot()->frameId();
		mPendingReplay = mRenderThread->post([replay = std::move(replay), driver = mDriver, frameId] {
			IKIGAI_PROFILE_SCOPE("Replay Sealed Render Frame");
			IKIGAI_PROFILE_ZONE_VALUE(frameId);
			driver->withReplayAccess([&] { replay.Commands->execute(driver, replay.Dispatch); });
		});
	}
	std::exception_ptr preparationFailure;
	{
		AccessGuard preparing { mPreparingCpuReplay };
		try {
			IKIGAI_PROFILE_SCOPE("Prepare Frame CPU State");
			IKIGAI_PROFILE_ZONE_VALUE(mActiveFrame->snapshot()->frameId());
			prepare();
		} catch (...) {
			preparationFailure = std::current_exception();
		}
	}
	// Always join the sealed buffer before propagating a producer exception.
	// If replay also failed, the backend error is the one callers observe.
	waitForReplay();
	if (preparationFailure) {
		mReplayFailure = preparationFailure;
		std::rethrow_exception(preparationFailure);
	}
}

RenderTicket Renderer::submitAsync(const std::function<void()>& prepare) {
	IKIGAI_PROFILE_SCOPE("Submit Render Frame");
	mOwner.requireCurrentThread();
	if (mPreparingCpuReplay) {
		throw std::logic_error("Cannot submit a frame during CPU replay preparation");
	}
	if (!mDriver || mShutdown) {
		throw std::logic_error("Frame submission needs a driver");
	}
	waitForReplay();
	if (prepare && (!mActiveFrame || !supportsCpuReplayOverlap())) {
		throw std::logic_error("CPU replay overlap requires an active pipelined Vulkan or DirectX 12 frame with upscaling and frame generation off");
	}
	if (mRenderThread) {
		if (mActiveFrame) {
			mActiveFrame->seal();
		}
		if (prepare) {
			pollPresent();
			retireFrames(false);
			replayWhilePreparing(prepare);
		}
		else {
			flush();
		}
		{
			IKIGAI_PROFILE_SCOPE("Present Render Frame");
			AccessGuard guard { mPreparingAccess };
			mDriver->presentFrame();
		}
		const auto token = mDriver->completion().lastSubmitted();
		finishFrame(token);
		if (mGpuWaitThread) {
			auto ticket = mGpuWaitThread->post(mDriver->presentedFrameWait(token));
			mPendingPresents.push_back(ticket);
			AccessGuard guard { mPreparingAccess };
			// Opening the next recording requires an available slot.
			if (mPendingPresents.size() >= DriverInterface::settings.framesInFlight) {
				waitOldestPresent();
			}
			mDriver->finishPresent();
			retireFrames(false);
			return ticket;
		}
		auto ticket = mRenderThread->post([driver = mDriver] {
			IKIGAI_PROFILE_SCOPE("Wait for Present GPU Completion");
			driver->waitForPresentedFrame();
		});
		mPendingPresents.push_back(ticket);
		return ticket;
	}
	auto receipt = std::make_shared<std::optional<GpuCompletionToken>>();
	execute([driver = mDriver, receipt] {
		IKIGAI_PROFILE_SCOPE("Submit Serial Render Frame");
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
	return {};
}

void Renderer::waitForPendingPresent() {
	mOwner.requireCurrentThread();
	while (!mWaitingForPresent && !mPendingPresents.empty()) {
		waitOldestPresent();
	}
}

void Renderer::waitOldestPresent() {
	AccessGuard waiting { mWaitingForPresent };
	auto ticket = mPendingPresents.front();
	{
		IKIGAI_PROFILE_SCOPE("Wait for Present Ticket");
#ifdef _WIN32
		// DXGI may send synchronous window messages while the queue drains.
		while (ticket.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
			MSG message {};
			PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
		}
#endif
		mPendingPresents.pop_front();
		ticket.get();
	}
	if (!mGpuWaitThread) {
		AccessGuard guard { mPreparingAccess };
		mDriver->finishPresent();
	}
}

void Renderer::pollPresent() {
	mOwner.requireCurrentThread();
	if (!mGpuWaitThread) {
		waitForPendingPresent();
		return;
	}
	while (
		!mWaitingForPresent && !mPendingPresents.empty() && mPendingPresents.front().wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		waitOldestPresent();
	}
}

bool Renderer::hasActiveFrame() const {
	mOwner.requireCurrentThread();
	return bool(mActiveFrame);
}

void Renderer::shutdown() {
	mOwner.requireCurrentThread();
	if (mPreparingCpuReplay) {
		throw std::logic_error("Cannot shut down the renderer during CPU replay preparation");
	}
	if (mShutdown) {
		return;
	}
	bool replayFailed = false;
	try {
		waitForReplay();
	} catch (...) {
		replayFailed = true;
	}
	bool presentFailed = false;
	try {
		waitForPendingPresent();
	} catch (const std::exception& error) {
		std::cerr << "Render thread present failed: " << error.what() << '\n';
		mPendingPresents.clear();
		presentFailed = true;
	}
	mRenderThread.reset();
	mGpuWaitThread.reset();
	std::exception_ptr drainFailure;
	try {
		if (mDriver && !mDriver->hasGpuFailure() && !presentFailed && !replayFailed && std::uncaught_exceptions() == 0) {
			if (mActiveFrame) {
				mExecutor->cancel();
				// A failed present may already have submitted work. Do not submit an ended native list twice.
				if (mDriver->hasOpenRecording()) {
					mDriver->requireOwnerThread();
					mDriver->end();
				}
				const auto token = mDriver->completion().lastSubmitted();
				if (token.Value > mFrameStartSubmission) {
					mDriver->waitForCompletion(token);
					if (mDriver->mFrameGeneration) {
						mDriver->mFrameGeneration->waitForPresents();
					}
					mActiveFrame->cancelledSubmission(token);
					if (!mActiveFrame->retire(mDriver->completion())) {
						throw std::logic_error("Backend wait returned before GPU completion");
					}
				}
			}
			if (DriverInterface::settings.pipelined()) {
				mDriver->waitForCompletion(mDriver->completion().lastSubmitted());
			}
			retireFrames(true);
		}
	} catch (const std::exception& error) {
		std::cerr << "Render drain failed: " << error.what() << '\n';
		drainFailure = std::current_exception();
	} catch (...) {
		drainFailure = std::current_exception();
	}
	mShutdown = true;
	mReplayFailure = {};
	mExecutor->cancel();
	if (mDriver) {
		mDriver->drainForShutdown();
		mDriver->stopPresentation();
	}
	if (mActiveFrame) {
		mExecutor->endFrame();
		mActiveFrame.reset();
	}
	mSubmittedFrames.clear();
	mFreeFrameCommands.clear();
	if (drainFailure && (!mDriver || !mDriver->hasGpuFailure())) {
		std::rethrow_exception(drainFailure);
	}
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
