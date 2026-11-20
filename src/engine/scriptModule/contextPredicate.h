#pragma once
#include "scriptSource.h"
#include "utilsModule/expressions/expression.h"
#include <memory>

namespace IKIGAI::SCRIPTING {
// Per-context VM and cached function. Arguments are scalar snapshots; no owner,
// component setters or scene services are bound to predicate scripts.
class ContextPredicate {
	struct State;
	std::unique_ptr<State> state;

public:
	ContextPredicate(const std::string& path, const std::string& function, ReadScriptText& readText);
	~ContextPredicate();
	bool evaluate(const std::vector<DATA::Value>&);
};
}
