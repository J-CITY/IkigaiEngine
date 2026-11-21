#pragma once
#include "expression.h"
#include <limits>

namespace IKIGAI::DATA {
struct StateTransition {
	std::string target;
	Expression condition;
	int priority = 0;
	bool interrupt = false, force = false;
	float blend = 0;
	std::vector<size_t> consumeSlots;
	std::string displayText;
};
struct StateMachineDefinition {
	std::map<std::string, std::vector<StateTransition>> transitions;
	std::vector<StateTransition> anyState;
};
// Independent of skeletons, physics, input, ECS and the scripting frontend.
class StateMachineInstance {
protected:
	std::string mState, mPreviousState;
	float mTime = 0, mPreviousTime = 0, mBlendTime = 0, mBlendDuration = 0;

public:
	const StateTransition* select(const StateMachineDefinition& definition, bool locked, const std::function<Value(size_t)>& read) const {
		const StateTransition* selected = nullptr;
		int priority = std::numeric_limits<int>::min();
		auto consider = [&](const StateTransition& edge, bool any) {
			if (any && locked && !edge.force) {
				return;
			}
			if (mBlendTime < mBlendDuration && !edge.interrupt) {
				return;
			}
			if (!any && edge.target == mState) {
				return;
			}
			if (edge.priority > priority && boolean(edge.condition.evaluate(read))) {
				priority = edge.priority;
				selected = edge.target == mState ? nullptr : &edge;
			}
		};
		if (auto it = definition.transitions.find(mState); it != definition.transitions.end()) {
			for (const auto& edge : it->second) {
				consider(edge, false);
			}
		}
		for (const auto& edge : definition.anyState) {
			consider(edge, true);
		}
		return selected;
	}
};
}
