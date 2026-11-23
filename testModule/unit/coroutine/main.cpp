#include "support/expect.h"

#include "coroutineModule/awaitables.h"
#include "coroutineModule/coroutineScope.h"
#include "coroutineModule/scheduler.h"

#include <chrono>
#include <stdexcept>

using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectThrows;
using IKIGAI::TEST::require;

namespace {
using IKIGAI::COROUTINE::Clock;
using IKIGAI::COROUTINE::CoroutineScope;
using IKIGAI::COROUTINE::NextFrame;
using IKIGAI::COROUTINE::Scheduler;
using IKIGAI::COROUTINE::SchedulerConfig;
using IKIGAI::COROUTINE::Task;
using IKIGAI::COROUTINE::TaskHandle;
using IKIGAI::COROUTINE::TaskStatus;
using IKIGAI::COROUTINE::WaitForSeconds;
using IKIGAI::COROUTINE::YieldIfBudgetExceeded;

const auto kFrame = std::chrono::duration<double>(1.0 / 60.0);

SchedulerConfig generous() {
	SchedulerConfig config;
	config.frameBudget = std::chrono::seconds(1);
	return config;
}

Scheduler::SteadyClock::time_point stationaryClock() {
	return {};
}

void tick(Scheduler& scheduler, std::chrono::duration<double> game = kFrame, std::chrono::duration<double> real = kFrame) {
	scheduler.tick(game, real);
}

Task<void> finishImmediately() {
	co_return;
}

Task<void> waitOneFrame() {
	co_await NextFrame();
	co_return;
}

Task<int> answer() {
	co_return 7;
}

Task<void> publish(int& slot) {
	slot = co_await answer();
	co_return;
}

Task<void> fail() {
	throw std::runtime_error("coroutine failed");
	co_return;
}

Task<void> waitGame(double seconds) {
	co_await WaitForSeconds(seconds, Clock::Game);
	co_return;
}

Task<void> waitReal(double seconds) {
	co_await WaitForSeconds(seconds, Clock::Real);
	co_return;
}
void checkBasics() {
	Scheduler immediate(generous(), stationaryClock);
	const auto done = immediate.start(finishImmediately(), "done");
	expect(done.name() == "done", "coroutine.name");
	tick(immediate);
	expect(done.status() == TaskStatus::Completed, "coroutine.completes");
	expect(immediate.stats().completed == 1 && immediate.stats().failed == 0, "coroutine.stats");

	Task<void> empty;
	expectThrows<std::invalid_argument>([&] { immediate.start(std::move(empty)); }, "coroutine.empty");
	SchedulerConfig broken;
	broken.maxResumesPerFrame = 0;
	expectThrows<std::invalid_argument>([&] { Scheduler rejected(broken); }, "coroutine.invalidBudget");

	Scheduler frames(generous(), stationaryClock);
	const auto waiting = frames.start(waitOneFrame(), "frame");
	tick(frames);
	expect(waiting.status() == TaskStatus::Suspended, "coroutine.nextFrame.waits");
	tick(frames);
	expect(waiting.status() == TaskStatus::Completed, "coroutine.nextFrame.resumes");

	int slot = 0;
	Scheduler nested(generous(), stationaryClock);
	nested.start(publish(slot));
	tick(nested);
	expect(slot == 7 && nested.stats().completed == 2 && nested.stats().failed == 0, "coroutine.child");

	Scheduler delays(generous(), stationaryClock);
	delays.start(waitGame(0.05));
	tick(delays, std::chrono::duration<double>(0.02));
	tick(delays, std::chrono::duration<double>(0.02));
	expect(delays.stats().completed == 0, "coroutine.gameClock.waits");
	tick(delays, std::chrono::duration<double>(0.02));
	tick(delays, std::chrono::duration<double>(0.02));
	expect(delays.stats().completed == 1, "coroutine.gameClock.elapses");

	Scheduler realClock(generous(), stationaryClock);
	realClock.start(waitReal(0.05));
	tick(realClock, std::chrono::duration<double>(1.0), std::chrono::duration<double>(0.01));
	expect(realClock.stats().completed == 0, "coroutine.realClock.ignoresGame");
	tick(realClock, std::chrono::duration<double>(0.0), std::chrono::duration<double>(0.05));
	expect(realClock.stats().completed == 1, "coroutine.realClock.elapses");

	Scheduler failing(generous(), stationaryClock);
	const auto failed = failing.start(fail(), "fail");
	tick(failing);
	expect(failed.status() == TaskStatus::Failed && failing.stats().failed == 1, "coroutine.failure");
	expectThrows<std::runtime_error>([&] { failed.rethrowIfFailed(); }, "coroutine.rethrow");

	Scheduler cancelled(generous(), stationaryClock);
	const auto handle = cancelled.start(waitOneFrame(), "cancel");
	tick(cancelled);
	handle.cancel();
	expect(handle.status() == TaskStatus::Cancelled && cancelled.stats().cancelled == 1, "coroutine.cancel");

	Scheduler stopping(generous(), stationaryClock);
	const auto live = stopping.start(waitOneFrame());
	tick(stopping);
	expect(stopping.acceptingTasks(), "coroutine.accepting");
	stopping.shutdown();
	expect(!stopping.acceptingTasks() && live.status() == TaskStatus::Cancelled && stopping.stats().cancelled == 1, "coroutine.shutdown");
}

struct ChildObservation {
	int entered = 0;
	int destroyed = 0;
	int resumed = 0;
};

Task<void> observedChild(ChildObservation& observation) {
	struct Lifetime {
		int& destroyed;
		~Lifetime() {
			++destroyed;
		}
	} lifetime { observation.destroyed };
	++observation.entered;
	co_await NextFrame();
	++observation.resumed;
}

Task<void> awaitChild(ChildObservation& observation, bool& returned) {
	co_await observedChild(observation);
	returned = true;
}

void checkChildCancellation() {
	ChildObservation child;
	bool parentReturned = false;
	Scheduler scheduler(generous(), stationaryClock);
	const auto parent = scheduler.start(awaitChild(child, parentReturned));
	tick(scheduler);
	require(child.entered == 1 && parent.status() == TaskStatus::Suspended, "coroutine.cancelChild.parentAwaitsLiveChild");
	parent.cancel();
	expect(parent.status() == TaskStatus::Cancelled && scheduler.stats().cancelled == 2, "coroutine.cancelChild.bothCancelled");
	expect(child.destroyed == 1 && scheduler.tasks().empty(), "coroutine.cancelChild.framesReleased");
	tick(scheduler);
	expect(!parentReturned && child.resumed == 0, "coroutine.cancelChild.noLateResume");
}

void checkScope() {
	Scheduler scheduler(generous(), stationaryClock);
	TaskHandle scoped;
	const auto independent = scheduler.start(waitOneFrame());
	{
		CoroutineScope scope(scheduler);
		scoped = scope.start(waitOneFrame());
		tick(scheduler);
		require(scoped.status() == TaskStatus::Suspended, "coroutine.scope.taskSuspendedBeforeDestruction");
	}
	expect(scoped.status() == TaskStatus::Cancelled, "coroutine.scope.destructionCancelsTask");
	tick(scheduler);
	expect(independent.status() == TaskStatus::Completed, "coroutine.scope.independentTaskFinishes");
}

Task<void> consumeBudget(Scheduler::SteadyClock::time_point& now, int& progress) {
	co_await YieldIfBudgetExceeded();
	progress = 1;
	now += std::chrono::milliseconds(2);
	co_await YieldIfBudgetExceeded();
	progress = 2;
}

void checkBudget() {
	Scheduler::SteadyClock::time_point now {};
	int progress = 0;
	SchedulerConfig config;
	config.frameBudget = std::chrono::milliseconds(2);
	Scheduler scheduler(config, [&] { return now; });
	const auto budgeted = scheduler.start(consumeBudget(now, progress));
	const auto queued = scheduler.start(finishImmediately());
	tick(scheduler);
	expect(progress == 1 && budgeted.status() == TaskStatus::Suspended, "coroutine.budget.yieldsAtLimit");
	expect(scheduler.stats().budgetExceeded && scheduler.stats().resumes == 1 && queued.status() == TaskStatus::Pending,
		"coroutine.budget.remainingTaskDeferred");
	tick(scheduler);
	expect(progress == 2 && budgeted.status() == TaskStatus::Completed && queued.status() == TaskStatus::Completed,
		"coroutine.budget.bothCompleteNextTick");
}
}

int main(int argc, char** argv) {
	return IKIGAI::TEST::runCases(argc, argv,
		{ { "basics", checkBasics }, { "child_cancellation", checkChildCancellation }, { "scope", checkScope }, { "budget", checkBudget } });
}
