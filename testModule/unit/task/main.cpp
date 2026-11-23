#include "support/expect.h"

#include "taskModule/taskSystem.h"

#include <array>
#include <atomic>
#include <latch>
#include <stdexcept>

using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectThrows;
using IKIGAI::TEST::require;

namespace {
using namespace IKIGAI::TASK;

void start(TaskSystem& tasks) {
	require(tasks.setup() && tasks.initialize(), "task.start");
}

void addRange(void* payload, std::size_t begin, std::size_t end) {
	auto* total = static_cast<std::atomic<int>*>(payload);
	for (std::size_t index = begin; index < end; ++index) {
		total->fetch_add(static_cast<int>(index));
	}
}

void checkBasics() {
	std::atomic<int> batchSum = 0;
	TaskSystem tasks;
	expect(tasks.getStatus() == TaskSystemStatus::TERMINANED, "task.initial");
	start(tasks);
	expect(tasks.isReady() && tasks.isBackgroundReady() && tasks.isIoReady(), "task.lanes");
	expect(!tasks.setup(), "task.setupOnce");

	auto value = tasks.submit("product", -1, nullptr, [] { return 21 * 2; });
	require(value.mFuture != nullptr, "task.submit.hasFuture");
	expect(value.mFuture->get() == 42, "task.submit.value");
	auto exploded = tasks.submit("explode", -1, nullptr, []() -> int { throw std::runtime_error("task failed"); });
	require(exploded.mFuture != nullptr, "task.submit.failingHasFuture");
	expectThrows<std::runtime_error>([&] { (void)exploded.mFuture->get(); }, "task.submit.throws");

	auto background = tasks.dispatch("background", [] { });
	expect(background.get().Outcome == BackgroundOutcome::Succeeded, "task.background");
	auto failed = tasks.dispatch("background-fail", [] { throw std::runtime_error("background failed"); });
	expect(failed.get().Outcome == BackgroundOutcome::Failed, "task.background.fails");
	std::function<void()> empty;
	const auto rejected = tasks.dispatch("empty", empty);
	expect(rejected.get().Outcome == BackgroundOutcome::Rejected && rejected.get().Rejection == BackgroundRejection::InvalidWork,
		"task.background.empty");

	auto batch = tasks.createJobBatch();
	JobDescriptor job;
	job.function = addRange;
	job.payload = &batchSum;
	job.zoneName = "task-test-range";
	job.affinity = JobAffinity::AnyCpu;
	require(tasks.submit(batch, job, 1, 5), "task.batch.submit");
	tasks.close(batch);
	const auto waited = tasks.wait(batch);
	expect(waited.status == JobBatchStatus::Completed && batchSum.load() == 10, "task.batch.sum");

	tasks.waitSync();
	expect(tasks.terminate(), "task.terminate");
	expect(tasks.getStatus() == TaskSystemStatus::TERMINANED, "task.terminated");
	const auto after = tasks.dispatch("late", [] { });
	expect(after.get().Outcome == BackgroundOutcome::Rejected && after.get().Rejection == BackgroundRejection::Unavailable, "task.dispatch.stopped");
}

void checkParallelFor() {
	constexpr std::size_t end = 17;
	std::array<std::atomic<int>, end + 1> visits {};
	std::atomic<int> emptyCalls = 0;
	std::array<std::array<std::atomic<int>, 3>, 5> nested {};
	TaskSystem tasks;
	start(tasks);
	tasks.parallelFor(1, end, [&](std::size_t index) { ++visits.at(index); }, 3);
	for (std::size_t index = 1; index < end; ++index) {
		expect(visits[index] == 1, "task.parallelFor.eachIndexExactlyOnce");
	}
	expect(visits.front() == 0 && visits.back() == 0, "task.parallelFor.halfOpenRange");
	tasks.parallelFor(3, 3, [&](std::size_t) { ++emptyCalls; });
	expect(emptyCalls == 0, "task.parallelFor.empty");

	tasks.parallelFor(
		0, nested.size(),
		[&](std::size_t outer) { tasks.parallelFor(0, nested.front().size(), [&](std::size_t inner) { ++nested.at(outer).at(inner); }, 1); }, 1);
	for (const auto& row : nested) {
		for (const auto& count : row) {
			expect(count == 1, "task.parallelFor.nestedEachIndexExactlyOnce");
		}
	}
}

void checkBatchDependencies() {
	std::atomic<int> upstreamValue = 0;
	std::atomic<int> downstreamValue = -1;
	struct Transfer {
		std::atomic<int>& source;
		std::atomic<int>& destination;
	} transfer { upstreamValue, downstreamValue };
	TaskSystem tasks;
	start(tasks);
	auto upstream = tasks.createJobBatch();
	auto downstream = tasks.createJobBatch();
	const JobDescriptor produce { addRange, &upstreamValue, "task-test-produce", JobAffinity::AnyCpu };
	const JobDescriptor consume { [](void* data, std::size_t, std::size_t) {
									 auto& transfer = *static_cast<Transfer*>(data);
									 transfer.destination = transfer.source.load();
								 },
		&transfer, "task-test-consume", JobAffinity::AnyCpu };
	require(tasks.submit(upstream, produce, 1, 5), "task.batch.submitUpstream");
	const std::array<const JobBatch*, 1> prerequisites { &upstream };
	expect(!tasks.submit(downstream, consume, 0, 1, prerequisites), "task.batch.openPrerequisiteRejected");
	tasks.close(upstream);
	require(tasks.submit(downstream, consume, 0, 1, prerequisites), "task.batch.submitDependent");
	expect(tasks.wait(downstream).status == JobBatchStatus::Completed, "task.batch.dependentCompleted");
	expect(downstreamValue == 10, "task.batch.dependentObservesUpstreamResult");
	expect(tasks.wait(upstream).status == JobBatchStatus::Completed, "task.batch.upstreamCompleted");

	auto cancelled = tasks.createJobBatch();
	auto skipped = tasks.createJobBatch();
	tasks.cancel(cancelled);
	downstreamValue = -1;
	const std::array<const JobBatch*, 1> cancelledPrerequisites { &cancelled };
	require(tasks.submit(skipped, consume, 0, 1, cancelledPrerequisites), "task.batch.acceptsCancelledPrerequisite");
	expect(tasks.wait(skipped).status == JobBatchStatus::Cancelled, "task.batch.propagatesCancellation");
	expect(downstreamValue == -1, "task.batch.skipsDependentCallback");
}

void checkBackgroundCancellation() {
	std::latch entered { 1 };
	std::latch stopped { 1 };
	std::atomic<int> completionCalls = 0;
	std::atomic<BackgroundOutcome> completionOutcome = BackgroundOutcome::Failed;
	TaskSystem tasks;
	start(tasks);
	const auto ticket = tasks.dispatch(
		"cooperative",
		[&](std::stop_token token) {
			std::stop_callback onStop(token, [&] { stopped.count_down(); });
			entered.count_down();
			stopped.wait();
		},
		[&](const BackgroundResult& result) {
			completionOutcome = result.Outcome;
			++completionCalls;
		});
	require(ticket.accepted(), "task.background.cooperativeAccepted");
	entered.wait();
	expect(!ticket.ready() && completionCalls == 0, "task.background.ticketPendingBeforeStop");
	expect(tasks.terminate(), "task.background.terminateCooperativeWork");
	expect(ticket.get().Outcome == BackgroundOutcome::Cancelled, "task.background.ticketReportsCancellation");
	expect(completionCalls == 1 && completionOutcome == BackgroundOutcome::Cancelled, "task.background.completionCalledOnceWithCancellation");
}
}

int main(int argc, char** argv) {
	return IKIGAI::TEST::runCases(argc, argv,
		{ { "basics", checkBasics }, { "parallel_for", checkParallelFor }, { "batch_dependencies", checkBatchDependencies },
		{ "background_cancellation", checkBackgroundCancellation } });
}
