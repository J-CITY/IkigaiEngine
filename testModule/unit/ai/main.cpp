#include "support/expect.h"

#include "aiModule/behaviorTree/behaviorTree.h"
#include "aiModule/navigation/pathQuery.h"
#include "utilsModule/variables/variableContext.h"

#include <cmath>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectThrows;

namespace {
using IKIGAI::AI::BehaviorRegistry;
using IKIGAI::AI::BehaviorTask;
using IKIGAI::AI::BehaviorTreeDefinition;
using IKIGAI::AI::BehaviorTreeInstance;
using IKIGAI::AI::Blackboard;
using IKIGAI::AI::GridPathQuery;
using IKIGAI::AI::StraightLinePathQuery;
using IKIGAI::AI::TaskContext;
using IKIGAI::AI::TaskResult;
using IKIGAI::DATA::Value;
using IKIGAI::DATA::ValueType;
using IKIGAI::DATA::VariableAccess;
using IKIGAI::DATA::VariableKeyDescriptor;
using IKIGAI::DATA::VariableSchema;
using IKIGAI::DATA::VariableSchemaDescriptor;
using IKIGAI::MATH::Vector3f;

std::shared_ptr<const VariableSchema> schemaWith(std::initializer_list<VariableKeyDescriptor> keys) {
	VariableSchemaDescriptor document;
	document.Keys.assign(keys.begin(), keys.end());
	return VariableSchema::compile(document);
}

VariableKeyDescriptor key(const char* name, VariableAccess access) {
	VariableKeyDescriptor descriptor;
	descriptor.Name = name;
	descriptor.Type = ValueType::Bool;
	descriptor.Access = access;
	return descriptor;
}

nlohmann::json node(const char* id, const char* type, nlohmann::json parameters = nlohmann::json::object(),
	nlohmann::json children = nlohmann::json::array(), nlohmann::json decorators = nlohmann::json::array()) {
	// `{}` in a call is JSON null, and the tree compiler only accepts objects and arrays.
	if (!parameters.is_object()) {
		parameters = nlohmann::json::object();
	}
	if (!children.is_array()) {
		children = nlohmann::json::array();
	}
	if (!decorators.is_array()) {
		decorators = nlohmann::json::array();
	}
	return { { "Id", id }, { "Type", type }, { "Children", std::move(children) }, { "Parameters", std::move(parameters) },
		{ "Decorators", std::move(decorators) } };
}

std::unique_ptr<BehaviorTreeInstance> start(const nlohmann::json& nodes, const std::shared_ptr<const VariableSchema>& schema,
	const std::shared_ptr<Blackboard>& board, const BehaviorRegistry& registry = BehaviorRegistry::defaults()) {
	const nlohmann::json document = { { "Version", 1 }, { "Blackboard", "board" }, { "Root", "root" }, { "Mode", "Once" }, { "Nodes", nodes } };
	return std::make_unique<BehaviorTreeInstance>(BehaviorTreeDefinition::compile(document, schema, registry, "test"), board);
}

bool flagValue(const Blackboard& board, IKIGAI::DATA::VariableId id) {
	const auto value = board.read(id);
	return value.has_value() && std::get<bool>(*value);
}
void checkBehavior() {
	const auto schema = schemaWith({ key("Flag", VariableAccess::Local), key("Gate", VariableAccess::Input) });
	const auto flag = schema->key("Flag");
	const auto gate = schema->key("Gate");
	auto board = std::make_shared<Blackboard>(schema);

	expect(!board->read(flag).has_value(), "ai.blackboard.empty");
	expect(!board->takeChanges().empty(), "ai.blackboard.initialRevision");
	const auto before = board->version(flag);
	board->write(flag, Value(std::in_place_type<bool>, true));
	expect(flagValue(*board, flag), "ai.blackboard.write");
	expect(board->version(flag) != before, "ai.blackboard.version");
	const auto changes = board->takeChanges();
	expect(changes.size() == 1 && changes[0] == flag, "ai.blackboard.changes");
	expect(board->takeChanges().empty(), "ai.blackboard.changesOnce");
	board->clear(flag);
	expect(!board->read(flag).has_value(), "ai.blackboard.clear");
	expectThrows<std::runtime_error>([&] { board->write(gate, Value(std::in_place_type<bool>, true)); }, "ai.blackboard.writeInput");
	expectThrows<std::runtime_error>([&] { board->expressionValue(flag); }, "ai.blackboard.expressionUnset");
	board->setInput(gate, Value(std::in_place_type<bool>, false));
	expect(board->read(gate).has_value() && !std::get<bool>(*board->read(gate)), "ai.blackboard.input");

	{
		auto sequence = start(nlohmann::json::array({ node("root", "Sequence", {}, { "set" }),
							  node("set", "SetBlackboardValue", { { "Key", "Flag" }, { "Value", { { "Type", "Bool" }, { "Bool", true } } } }) }),
			schema, board);
		sequence->update(0.0);
		expect(flagValue(*board, flag), "ai.sequence.writes");
		expect(sequence->result() == TaskResult::Success, "ai.sequence.success");
	}
	board->clear(flag);
	{
		auto blocked = start(nlohmann::json::array({ node("root", "Sequence", {}, { "bad", "set" }), node("bad", "Fail"),
							 node("set", "SetBlackboardValue", { { "Key", "Flag" }, { "Value", { { "Type", "Bool" }, { "Bool", true } } } }) }),
			schema, board);
		blocked->update(0.0);
		expect(blocked->result() == TaskResult::Failure, "ai.sequence.stops");
		expect(!board->read(flag).has_value(), "ai.sequence.skipsRest");
	}
	{
		const nlohmann::json decorators
			= nlohmann::json::array({ { { "Type", "BlackboardCondition" }, { "Parameters", { { "Key", "Flag" }, { "Test", "IsTrue" } } } } });
		auto guarded
			= start(nlohmann::json::array({ node("root", "Sequence", {}, { "set" }), node("set", "Succeed", {}, {}, decorators) }), schema, board);
		guarded->update(0.0);
		expect(guarded->result() == TaskResult::Failure, "ai.decorator.blocks");
		board->write(flag, Value(std::in_place_type<bool>, true));
		guarded->restart();
		guarded->update(0.0);
		expect(guarded->result() == TaskResult::Success, "ai.decorator.allows");
	}
	board->clear(flag);
	{
		auto waiting = start(nlohmann::json::array({ node("root", "Wait", { { "Duration", 0.5 } }) }), schema, board);
		waiting->update(0.0);
		expect(!waiting->result().has_value() && waiting->running(), "ai.wait.running");
		waiting->update(0.5);
		expect(waiting->result() == TaskResult::Success, "ai.wait.completes");
	}
	expectThrows<std::runtime_error>(
		[&] { (void)start(nlohmann::json::array({ node("root", "Sequence", {}, { "missing" }) }), schema, board); }, "ai.tree.unknownChild");
	auto paused = start(nlohmann::json::array({ node("root", "Succeed") }), schema, board);
	paused->pause(true);
	paused->update(0.0);
	expect(!paused->result().has_value() && paused->paused(), "ai.pause.holds");
	paused->pause(false);
	paused->update(0.0);
	expect(paused->result() == TaskResult::Success, "ai.pause.resumes");
	expectThrows<std::runtime_error>([&] { paused->update(-1.0); }, "ai.tree.negativeDelta");
}

class RecordingTask final : public BehaviorTask {
public:
	RecordingTask(std::vector<std::string>& visits, std::string name, TaskResult result)
		: mVisits(visits)
		, mName(std::move(name))
		, mResult(result) { }

	TaskResult enter(TaskContext&) override {
		mVisits.push_back(mName);
		return mResult;
	}

private:
	std::vector<std::string>& mVisits;
	std::string mName;
	TaskResult mResult;
};

void checkSelector() {
	const auto schema = schemaWith({});
	auto board = std::make_shared<Blackboard>(schema);
	std::vector<std::string> visits;
	BehaviorRegistry registry;
	const auto registerTask = [&](std::string name, TaskResult result) {
		registry.addTask(name, [&visits, name, result](const nlohmann::json&, const VariableSchema&) {
			return [&visits, name, result] { return std::make_unique<RecordingTask>(visits, name, result); };
		});
	};
	registerTask("Reject", TaskResult::Failure);
	registerTask("Accept", TaskResult::Success);
	registerTask("Skipped", TaskResult::Success);
	auto selector = start(nlohmann::json::array({ node("root", "Selector", {}, { "first", "second", "third" }), node("first", "Reject"),
						  node("second", "Accept"), node("third", "Skipped") }),
		schema, board, registry);
	selector->update(0.0);
	expect(selector->result() == TaskResult::Success, "ai.selector.success");
	expect(visits == std::vector<std::string> { "Reject", "Accept" }, "ai.selector.visitsInOrderAndStopsOnSuccess");
}

void checkPaths() {
	const Vector3f from { 0.f, 2.f, 0.f };
	const Vector3f to { 3.f, 2.f, 0.f };
	const std::vector<Vector3f> direct { from, to };
	expect(StraightLinePathQuery {}.findPath(from, to) == direct, "ai.path.straight");
	expect(!StraightLinePathQuery {}.findPath(Vector3f { std::nanf(""), 0.f, 0.f }, to).has_value(), "ai.path.nonfinite");
	const std::vector<Vector3f> cells { from, { 1.f, 2.f, 0.f }, { 2.f, 2.f, 0.f }, to };
	expect(GridPathQuery { 1.f, 3 }.findPath(from, to) == cells, "ai.path.gridAtRadius");
	expect(GridPathQuery { 1.f, 2 }.findPath(from, to) == direct, "ai.path.fallsBackBeyondRadius");
	const Vector3f sameCellFrom { 0.1f, 0.f, 0.1f };
	const Vector3f sameCellTo { 0.2f, 0.f, 0.2f };
	expect(GridPathQuery { 1.f }.findPath(sameCellFrom, sameCellTo) == std::vector { sameCellFrom, sameCellTo }, "ai.path.sameCell");
	expectThrows<std::runtime_error>([] { GridPathQuery { 0.f }; }, "ai.path.invalidGrid");
}
}

int main(int argc, char** argv) {
	return IKIGAI::TEST::runCases(argc, argv, { { "basics", checkBehavior }, { "selector", checkSelector }, { "paths", checkPaths } });
}
