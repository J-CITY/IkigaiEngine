#include "support/expect.h"

#include "aiModule/behaviorTree/behaviorTree.h"
#include "aiModule/navigation/pathQuery.h"
#include "utilsModule/variables/variableContext.h"

#include <cmath>
#include <nlohmann/json.hpp>
#include <variant>

using IKIGAI::TEST::expect;

int main() {
	using IKIGAI::AI::Blackboard;
	using IKIGAI::AI::StraightLinePathQuery;
	using IKIGAI::AI::TaskResult;
	using IKIGAI::DATA::Value;
	using IKIGAI::DATA::ValueType;
	using IKIGAI::DATA::VariableAccess;
	using IKIGAI::DATA::VariableKeyDescriptor;
	using IKIGAI::DATA::VariableSchema;
	using IKIGAI::DATA::VariableSchemaDescriptor;
	using IKIGAI::MATH::Vector3f;

	VariableSchemaDescriptor schemaDocument;
	VariableKeyDescriptor flag;
	flag.Name = "Flag";
	flag.Type = ValueType::Bool;
	flag.Access = VariableAccess::Local;
	schemaDocument.Keys.push_back(flag);
	const auto schema = VariableSchema::compile(schemaDocument);
	const auto flagKey = schema->key("Flag");
	auto board = std::make_shared<Blackboard>(schema);
	expect(!board->read(flagKey).has_value(), "ai.blackboard.empty");
	board->write(flagKey, Value(std::in_place_type<bool>, true));
	const auto stored = board->read(flagKey);
	expect(stored.has_value() && std::get<bool>(*stored), "ai.blackboard.write");

	const auto from = Vector3f { 0.f, 1.f, 0.f };
	const auto to = Vector3f { 4.f, 1.f, 2.f };
	const auto path = StraightLinePathQuery {}.findPath(from, to);
	expect(path.has_value() && path->size() == 2 && (*path)[0] == from && (*path)[1] == to, "ai.path.straight");
	expect(!StraightLinePathQuery {}.findPath(Vector3f { std::nanf(""), 0.f, 0.f }, to).has_value(), "ai.path.nonfinite");

	board->clear(flagKey);
	const nlohmann::json tree = { { "Version", 1 }, { "Blackboard", "board" }, { "Root", "root" }, { "Mode", "Once" },
		{ "Nodes",
		nlohmann::json::array({ { { "Id", "root" }, { "Type", "Sequence" }, { "Children", nlohmann::json::array({ "set" }) } },
		{ { "Id", "set" }, { "Type", "SetBlackboardValue" },
		{ "Parameters", { { "Key", "Flag" }, { "Value", { { "Type", "Bool" }, { "Bool", true } } } } } } }) } };
	IKIGAI::AI::BehaviorTreeInstance instance(
		IKIGAI::AI::BehaviorTreeDefinition::compile(tree, schema, IKIGAI::AI::BehaviorRegistry::defaults(), "test"), board);
	instance.update(0.0);
	const auto written = board->read(flagKey);
	expect(written.has_value() && std::get<bool>(*written), "ai.tree.writesBlackboard");
	const auto result = instance.result();
	expect(result.has_value() && *result == TaskResult::Success, "ai.tree.succeeds");
	return IKIGAI::TEST::failureCount() == 0 ? 0 : 1;
}
