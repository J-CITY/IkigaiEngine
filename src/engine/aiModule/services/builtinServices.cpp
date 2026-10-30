#include "aiModule/behaviorTree/behaviorTree.h"
#include <cmath>

namespace IKIGAI::AI {
namespace {
	struct TargetParameters {
		std::string Candidate;
		std::string Target;
		float Range = 10;
		template <class C> static auto serde(C& c, TargetParameters& v) {
			using namespace serde::attribute;
			serde::serde_struct(c, v)
				.field(&TargetParameters::Candidate, "Candidate")
				.field(&TargetParameters::Target, "Target")
				.field(&TargetParameters::Range, "Range", default_ { 10.f });
		}
	};
	class TargetService final : public BehaviorService {
	public:
		TargetService(BlackboardKeyId candidate, BlackboardKeyId target, float range)
			: mCandidate(candidate)
			, mTarget(target)
			, mRange(range) { }
		void update(TaskContext& context) override {
			const auto& value = context.blackboard.read(mCandidate);
			if (context.host && value) {
				const auto& entity = std::get<EntityRef>(*value);
				const auto owner = context.host->ownerPosition();
				const auto target = context.host->position(entity);
				if (entity.alive() && owner && target) {
					const auto delta = *target - *owner;
					const float distanceSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
					if (distanceSquared <= mRange * mRange) {
						context.blackboard.write(mTarget, entity);
						return;
					}
				}
			}
			context.blackboard.clear(mTarget);
		}

	private:
		BlackboardKeyId mCandidate = 0;
		BlackboardKeyId mTarget = 0;
		float mRange = 10;
	};
}
void registerBuiltinServices(BehaviorRegistry& registry) {
	registry.addService("TargetInRange", [](const auto& json, const BlackboardSchema& schema) -> ServiceFactory {
		const auto parameters = readDescriptor<TargetParameters>(json);
		const auto candidate = schema.key(parameters.Candidate);
		const auto target = schema.key(parameters.Target);
		if (candidate == target || schema.keys[candidate].Type != BlackboardType::EntityRef || schema.keys[target].Type != BlackboardType::EntityRef
			|| schema.keys[target].Access != KeyAccess::Local || !std::isfinite(parameters.Range) || parameters.Range <= 0) {
			throw std::runtime_error("Invalid TargetInRange keys or Range");
		}
		return [candidate, target, parameters] { return std::make_unique<TargetService>(candidate, target, parameters.Range); };
	});
}
}
