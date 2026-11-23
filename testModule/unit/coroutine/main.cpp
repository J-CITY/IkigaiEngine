#include "support/expect.h"

#include "coroutineModule/awaitables.h"
#include "coroutineModule/scheduler.h"

#include <chrono>

using IKIGAI::TEST::expect;

namespace {
IKIGAI::COROUTINE::Task<void> finishImmediately() {
	co_return;
}

IKIGAI::COROUTINE::Task<void> waitOneFrame() {
	co_await IKIGAI::COROUTINE::NextFrame();
	co_return;
}

void tick(IKIGAI::COROUTINE::Scheduler& scheduler) {
	const auto frame = std::chrono::duration<double>(1.0 / 60.0);
	scheduler.tick(frame, frame);
}
}

int main() {
	IKIGAI::COROUTINE::Scheduler immediate;
	immediate.start(finishImmediately());
	tick(immediate);
	expect(immediate.stats().completed == 1, "coroutine.completes");
	expect(immediate.stats().failed == 0, "coroutine.noFailure");

	IKIGAI::COROUTINE::Scheduler frames;
	frames.start(waitOneFrame());
	tick(frames);
	expect(frames.stats().completed == 0, "coroutine.nextFrame.waits");
	expect(frames.stats().active + frames.stats().waiting > 0, "coroutine.nextFrame.pending");
	tick(frames);
	expect(frames.stats().completed == 1, "coroutine.nextFrame.resumes");
	expect(frames.stats().failed == 0, "coroutine.nextFrame.noFailure");
	return IKIGAI::TEST::failureCount() == 0 ? 0 : 1;
}
