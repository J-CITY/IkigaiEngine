#include "support/expect.h"

#include "renderModule/renderGraph/renderGraphCompiler.h"

#include <array>
#include <stdexcept>

namespace {
using namespace IKIGAI::RENDER::GRAPH;
using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectThrows;
using IKIGAI::TEST::require;

void checkSchedule() {
	const std::array resources { Resource { "visible" }, Resource { "unused" } };
	const std::array passes {
		Pass { .Name = "present", .Uses = { { 0, 1, Access::SAMPLED } }, .SideEffect = true },
		Pass { .Name = "produce", .Uses = { { 0, 1, Access::ATTACHMENT_WRITE } } },
		Pass { .Name = "unused writer", .Uses = { { 1, 1, Access::ATTACHMENT_WRITE } } },
	};
	const auto plan = Compile(resources, passes);
	require(plan.Schedule.size() == 2, "renderGraph.schedule.includesProducerAndConsumer");
	expect(plan.Schedule[0].Id == 1 && plan.Schedule[1].Id == 0, "renderGraph.schedule.ordersProducerBeforeConsumer");
	expect(plan.Culled == std::vector<PassId> { 2 }, "renderGraph.schedule.cullsUnobservedWriter");
	const auto& barriers = plan.Schedule[1].Barriers;
	require(barriers.size() == 1 && barriers.front().Before.has_value(), "renderGraph.schedule.emitsTransition");
	const auto& barrier = barriers.front();
	expect(barrier.Resource == 0 && barrier.Before->Pass == 1 && barrier.Before->Mode == Access::ATTACHMENT_WRITE && barrier.After == Access::SAMPLED,
		"renderGraph.barrier.connectsProducerWriteToConsumerRead");
}

void checkInvalidGraph() {
	const std::array resources { Resource { "intermediate" } };
	const std::array missingWriter { Pass { .Name = "reader", .Uses = { { 0, 1, Access::SAMPLED } }, .SideEffect = true } };
	expectThrows<std::invalid_argument>([&] { (void)Compile(resources, missingWriter); }, "renderGraph.validation.rejectsVersionWithoutWriter");
	const std::array unknownResource { Pass { .Name = "reader", .Uses = { { 1, 1, Access::SAMPLED } }, .SideEffect = true } };
	expectThrows<std::invalid_argument>([&] { (void)Compile(resources, unknownResource); }, "renderGraph.validation.rejectsUnknownResource");
	const std::array cycle { missingWriter.front(), Pass { .Name = "writer", .Uses = { { 0, 1, Access::ATTACHMENT_WRITE } }, .After = { 0 } } };
	expectThrows<std::invalid_argument>([&] { (void)Compile(resources, cycle); }, "renderGraph.validation.rejectsDataAndOrderCycle");
}
}

int main(int argc, char** argv) {
	return IKIGAI::TEST::runCases(argc, argv, { { "schedule", checkSchedule }, { "validation", checkInvalidGraph } });
}
