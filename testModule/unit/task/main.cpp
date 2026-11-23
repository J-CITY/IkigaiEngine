#include "support/expect.h"

#include "taskModule/taskSystem.h"

#include <atomic>

using IKIGAI::TEST::expect;

int main() {
	IKIGAI::TASK::TaskSystem tasks;
	expect(tasks.setup(), "task.setup");
	expect(tasks.initialize(), "task.initialize");

	std::atomic<int> sum = 0;
	tasks.parallelFor(1, 5, [&](std::size_t index) { sum.fetch_add(static_cast<int>(index)); });
	expect(sum.load() == 10, "task.parallelFor");

	auto ticket = tasks.dispatch("background-add", [] { });
	const auto result = ticket.get();
	expect(result.Outcome == IKIGAI::TASK::BackgroundOutcome::Succeeded, "task.background");
	expect(tasks.terminate(), "task.terminate");
	return IKIGAI::TEST::failureCount() == 0 ? 0 : 1;
}
