#include "frameContext.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace IKIGAI::RENDER {
GpuCompletionToken FrameSubmissionReceipt::token() const noexcept {
	return mToken;
}

FrameContext::FrameContext(uint64_t frameId, std::shared_ptr<RenderCommandBuffer> commands)
	: mFrameId(frameId)
	, mCommands(std::move(commands)) {
	if (!mCommands || !mCommands->retainsExecuted() || mCommands->pendingCommandCount()) {
		throw std::invalid_argument("Frame needs an empty command buffer");
	}
}

void FrameContext::addView(RenderViewSnapshot view) {
	if (mState != State::RECORDING
		|| std::any_of(mViews.begin(), mViews.end(), [&](const auto& existing) { return existing.ViewId == view.ViewId; })) {
		throw std::logic_error("Cannot append a view to a sealed frame or reuse a view id");
	}
	mViews.push_back(std::move(view));
}

void FrameContext::seal() {
	if (mState != State::RECORDING) {
		throw std::logic_error("Frame is already sealed");
	}
	mSnapshot = std::make_shared<const RenderFrameSnapshot>(mFrameId, std::move(mViews));
	mCommands->seal();
	mState = State::SEALED;
}

void FrameContext::submitted(GpuCompletionToken token) {
	if (mState != State::SEALED || token.Domain == 0 || token.Value == 0) {
		throw std::logic_error("Frame requires a valid GPU submission");
	}
	mCompletion = token;
	mReceipt->mToken = token;
	mState = State::SUBMITTED;
}

bool FrameContext::retire(const GpuCompletionSource& completion) {
	if (mState == State::COMPLETE) {
		return true;
	}
	if (mState != State::SUBMITTED || !completion.isComplete(mCompletion)) {
		return false;
	}
	mCommands->reset();
	mState = State::COMPLETE;
	return true;
}

void FrameContext::cancelledSubmission(GpuCompletionToken token) {
	if (mState == State::COMPLETE || token.Domain == 0 || token.Value == 0) {
		throw std::logic_error("Cancelled recording requires a native submission before retirement");
	}
	mCommands->cancel();
	mCommands->seal();
	mViews.clear();
	mSnapshot.reset();
	mState = State::SEALED;
	submitted(token);
}

FrameContext::State FrameContext::state() const noexcept {
	return mState;
}
GpuCompletionToken FrameContext::completionToken() const noexcept {
	return mCompletion;
}
std::shared_ptr<const FrameSubmissionReceipt> FrameContext::submissionReceipt() const noexcept {
	return mReceipt;
}

std::shared_ptr<const RenderFrameSnapshot> FrameContext::snapshot() const noexcept {
	return mSnapshot;
}
std::shared_ptr<RenderCommandBuffer> FrameContext::commands() const noexcept {
	return mCommands;
}
}
