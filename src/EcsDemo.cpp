#include "EcsDemo.h"
#include "engine/aiModule/tasks/taskParameters.h"
#include "engine/coreModule/ecs/components/behaviorTreeComponent.h"
#include "engine/coreModule/ecs/components/blackboardComponent.h"
#include "engine/coreModule/ecs/components/variableContextComponent.h"
#include "engine/coreModule/ecs/object.h"

namespace Example {
namespace {
	class MarkAlertTask final : public IKIGAI::AI::BehaviorTask {
	public:
		explicit MarkAlertTask(IKIGAI::AI::BlackboardKeyId key)
			: mKey(key) { }
		IKIGAI::AI::TaskResult enter(IKIGAI::AI::TaskContext& context) override {
			context.blackboard.write(mKey, true);
			return IKIGAI::AI::TaskResult::Success;
		}

	private:
		IKIGAI::AI::BlackboardKeyId mKey = 0;
	};
}
void RegisterBehaviorTreeTaskExample() {
	static const bool registered = [] {
		IKIGAI::AI::BehaviorRegistry::defaults().addTask(
			"Example.MarkAlert", [](const nlohmann::json& document, const IKIGAI::AI::BlackboardSchema& schema) -> IKIGAI::AI::TaskFactory {
				const auto parameters = IKIGAI::AI::readDescriptor<IKIGAI::AI::KeyParameters>(document);
				const auto key = schema.key(parameters.Key);
				if (schema.keys[key].Type != IKIGAI::AI::BlackboardType::Bool || schema.keys[key].Access != IKIGAI::AI::KeyAccess::Local) {
					throw std::runtime_error("Example.MarkAlert requires a local Bool key");
				}
				return [key] { return std::make_unique<MarkAlertTask>(key); };
			});
		return true;
	}();
	(void)registered;
}
void ConfigureBehaviorTree(IKIGAI::ECS::Object& npc, int candidateObjectId, bool controlMotor) {
	auto context = npc.addComponent<IKIGAI::ECS::VariableContextComponent>();
	context->settings.Schema = "/ai/patrol.blackboard";
	IKIGAI::ECS::VariableDescriptor candidate;
	candidate.Type = IKIGAI::ECS::VariableType::EntityRef;
	candidate.ObjectId = candidateObjectId;
	context->settings.Variables.insert_or_assign("Candidate", candidate);
	auto board = npc.addComponent<IKIGAI::ECS::BlackboardComponent>();
	board->settings.Schema = "/ai/patrol.blackboard";
	auto behavior = npc.addComponent<IKIGAI::ECS::BehaviorTreeComponent>();
	behavior->settings.Tree = "/ai/patrol.btree";
	if (controlMotor) {
		behavior->settings.Motor = { int(npc.getID().getUniqueId().value()), "CharacterMotorComponent" };
	}
}
}
