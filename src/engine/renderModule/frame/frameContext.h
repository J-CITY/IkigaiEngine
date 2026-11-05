#pragma once
#include "gpuCompletion.h"
#include "renderFrameSnapshot.h"
#include "renderModule/commands/renderCommandBuffer.h"
#include <memory>

namespace IKIGAI::RENDER {
class FrameContext final {
public:
	enum class State { RECORDING, SEALED, SUBMITTED, COMPLETE };
	explicit FrameContext(uint64_t frameId, std::shared_ptr<RenderCommandBuffer> commands);
	void addView(RenderViewSnapshot view);
	void seal();
	void submitted(GpuCompletionToken token);
	// Drop pending CPU work, retain recorded packets until the supplied native submission completes.
	void cancelledSubmission(GpuCompletionToken token);
	bool retire(const GpuCompletionSource& completion);
	[[nodiscard]] State state() const noexcept;
	[[nodiscard]] GpuCompletionToken completionToken() const noexcept;
	[[nodiscard]] std::shared_ptr<const RenderFrameSnapshot> snapshot() const noexcept;
	[[nodiscard]] std::shared_ptr<RenderCommandBuffer> commands() const noexcept;

private:
	uint64_t mFrameId = 0;
	State mState = State::RECORDING;
	std::vector<RenderViewSnapshot> mViews {};
	std::shared_ptr<const RenderFrameSnapshot> mSnapshot {};
	std::shared_ptr<RenderCommandBuffer> mCommands {};
	GpuCompletionToken mCompletion {};
};
}
