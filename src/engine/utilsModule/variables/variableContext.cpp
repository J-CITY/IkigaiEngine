#include "variableContext.h"
#include <cmath>

namespace IKIGAI::DATA {
namespace {
	bool equal(const Value& a, const Value& b) {
		return a.index() == b.index() && std::visit([&](const auto& value) { return value == std::get<std::decay_t<decltype(value)>>(b); }, a);
	}
	void validateValue(const Value& value) {
		if (const auto* scalar = std::get_if<float>(&value); scalar && !std::isfinite(*scalar)) {
			throw std::runtime_error("Non-finite context value");
		}
		if (const auto* vector = std::get_if<MATH::Vector3f>(&value);
			vector && (!std::isfinite(vector->x) || !std::isfinite(vector->y) || !std::isfinite(vector->z))) {
			throw std::runtime_error("Non-finite context vector");
		}
	}
}
VariableId VariableSchema::key(const std::string& name) const {
	auto found = names.find(name);
	if (found == names.end()) {
		throw std::runtime_error("Unknown context variable: " + name);
	}
	return found->second;
}
std::shared_ptr<const VariableSchema> VariableSchema::compile(const nlohmann::json& document, const std::string& path) {
	try {
		if (!document.at("Keys").is_array()) {
			throw std::runtime_error("Context Keys must be an array");
		}
		return compile(readDescriptor<VariableSchemaDescriptor>(document));
	} catch (const std::exception& error) {
		throw std::runtime_error(path + ": " + error.what());
	}
}
std::shared_ptr<const VariableSchema> VariableSchema::compile(const VariableSchemaDescriptor& descriptor) {
	if (descriptor.Version != 1 || descriptor.Keys.size() > 4096) {
		throw std::runtime_error("Unsupported context Version or too many variables");
	}
	auto result = std::make_shared<VariableSchema>();
	for (auto key : descriptor.Keys) {
		const auto index = result->keys.size();
		if (key.Name.empty() || !result->names.emplace(key.Name, index).second) {
			throw std::runtime_error("Empty or duplicate context variable: " + key.Name);
		}
		if (key.Expression) {
			key.Access = VariableAccess::Computed;
		}
		if (key.Event && (key.Type != ValueType::Bool || key.Access != VariableAccess::Local)) {
			throw std::runtime_error("Event must be an owned Bool: " + key.Name);
		}
		std::optional<Value> initial;
		if (key.Default) {
			if (key.Default->Type != key.Type || key.Type == ValueType::EntityRef) {
				throw std::runtime_error("Context default type mismatch: " + key.Name);
			}
			initial = key.Default->value();
			validateValue(*initial);
		}
		else if (key.Event) {
			initial = false;
		}
		if (key.Type != ValueType::EntityRef) {
			result->symbols.emplace(key.Name, Symbol { index, key.Type });
		}
		result->defaults.push_back(std::move(initial));
		result->keys.push_back(std::move(key));
	}
	result->expressions.resize(result->keys.size());
	for (size_t key = 0; key < result->keys.size(); ++key) {
		if (result->keys[key].Expression) {
			auto expression = Expression::compile(*result->keys[key].Expression, result->symbols);
			if (expression.type() != result->keys[key].Type) {
				throw std::runtime_error("Context expression type mismatch: " + result->keys[key].Name);
			}
			result->expressions[key] = std::move(expression);
		}
	}
	VariableContext validation(result);
	return result;
}
VariableContext::VariableContext(std::shared_ptr<const VariableSchema> schema)
	: mSchema(std::move(schema)) {
	if (!mSchema) {
		throw std::runtime_error("VariableContext requires a schema");
	}
	mCurrent = mSchema->defaults;
	mSnapshot = mCurrent;
	mVersions.resize(mCurrent.size(), 1);
	mComputations.resize(mCurrent.size());
	mDependents.resize(mCurrent.size());
	mDirty.resize(mCurrent.size(), true);
	mEvaluating.resize(mCurrent.size(), false);
	for (size_t key = 0; key < mCurrent.size(); ++key) {
		if (mSchema->expressions[key]) {
			const auto expression = *mSchema->expressions[key];
			const auto dependencies = expression.dependencies();
			bindComputation(key, dependencies, [expression, dependencies](const VariableContext& context) -> std::optional<Value> {
				for (const auto dependency : dependencies) {
					if (!context.readCurrent(dependency)) {
						return std::nullopt;
					}
				}
				return expression.evaluate([&](size_t slot) { return *context.readCurrent(slot); });
			});
		}
	}
}
const VariableSchema& VariableContext::schema() const {
	return *mSchema;
}
const std::optional<Value>& VariableContext::readCurrent(VariableId key) const {
	evaluate(key);
	return mCurrent.at(key);
}
const std::optional<Value>& VariableContext::readSnapshot(VariableId key) const {
	return mSnapshot.at(key);
}
uint64_t VariableContext::version(VariableId key) const {
	evaluate(key);
	return mVersions.at(key);
}
uint64_t VariableContext::tick() const {
	return mTick;
}
void VariableContext::invalidateDependents(VariableId key) const {
	for (const auto dependent : mDependents[key]) {
		if (!mDirty[dependent]) {
			mDirty[dependent] = true;
			invalidateDependents(dependent);
		}
	}
}
void VariableContext::assign(VariableId key, std::optional<Value> value) const {
	if (value) {
		if (typeOf(*value) != mSchema->keys.at(key).Type) {
			throw std::runtime_error("Context type mismatch: " + mSchema->keys[key].Name);
		}
		validateValue(*value);
	}
	if (bool(mCurrent[key]) == bool(value) && (!value || equal(*mCurrent[key], *value))) {
		return;
	}
	mCurrent[key] = std::move(value);
	++mVersions[key];
	invalidateDependents(key);
}
void VariableContext::evaluate(VariableId key) const {
	if (!mDirty.at(key)) {
		return;
	}
	if (mEvaluating[key]) {
		throw std::runtime_error("Recursive context evaluator: " + mSchema->keys[key].Name);
	}
	const auto& computation = mComputations[key];
	if (!computation.evaluate) {
		if (mSchema->keys[key].Access == VariableAccess::Computed) {
			throw std::runtime_error("Unbound context computation: " + mSchema->keys[key].Name);
		}
		mDirty[key] = false;
		return;
	}
	mEvaluating[key] = true;
	try {
		for (const auto dependency : computation.dependencies) {
			evaluate(dependency);
		}
		auto value = computation.evaluate(*this);
		assign(key, std::move(value));
		mDirty[key] = false;
		mEvaluating[key] = false;
	} catch (...) {
		mEvaluating[key] = false;
		throw;
	}
}
void VariableContext::write(VariableId key, Value value) {
	const auto& descriptor = mSchema->keys.at(key);
	if (descriptor.Access != VariableAccess::Local || descriptor.Event) {
		throw std::runtime_error("Context variable is read-only or event: " + descriptor.Name);
	}
	assign(key, std::move(value));
}
void VariableContext::clear(VariableId key) {
	const auto& descriptor = mSchema->keys.at(key);
	if (descriptor.Access != VariableAccess::Local || descriptor.Event) {
		throw std::runtime_error("Context variable is read-only or event: " + descriptor.Name);
	}
	assign(key, std::nullopt);
}
void VariableContext::fire(VariableId key) {
	if (!mSchema->keys.at(key).Event) {
		throw std::runtime_error("Not a context event");
	}
	assign(key, true);
}
void VariableContext::setInput(VariableId key, std::optional<Value> value) {
	if (mSchema->keys.at(key).Access != VariableAccess::Input) {
		throw std::runtime_error("Not a context Input: " + mSchema->keys[key].Name);
	}
	assign(key, std::move(value));
}
void VariableContext::validateComputations() const {
	std::vector<int> marks(mComputations.size(), 0);
	std::function<void(size_t, size_t)> visit = [&](size_t key, size_t depth) {
		if (depth > 128 || marks.at(key) == 1) {
			throw std::runtime_error("Cyclic context computation or depth exceeds 128");
		}
		if (marks[key] == 2) {
			return;
		}
		marks[key] = 1;
		for (auto dependency : mComputations[key].dependencies) {
			visit(dependency, depth + 1);
		}
		marks[key] = 2;
	};
	for (size_t key = 0; key < marks.size(); ++key) {
		visit(key, 0);
	}
}
void VariableContext::bindComputation(VariableId key, std::vector<VariableId> dependencies, Evaluator evaluator, bool poll) {
	if (mSchema->keys.at(key).Access != VariableAccess::Computed || !evaluator) {
		throw std::runtime_error("Invalid context computation binding");
	}
	auto previous = std::move(mComputations[key]);
	mComputations[key] = { std::move(dependencies), std::move(evaluator), poll };
	try {
		validateComputations();
	} catch (...) {
		mComputations[key] = std::move(previous);
		throw;
	}
	for (auto& dependents : mDependents) {
		dependents.clear();
	}
	for (size_t node = 0; node < mComputations.size(); ++node) {
		for (auto dependency : mComputations[node].dependencies) {
			mDependents.at(dependency).push_back(node);
		}
	}
	mDirty[key] = true;
	invalidateDependents(key);
}
void VariableContext::publish() {
	expireEntities();
	for (size_t key = 0; key < mComputations.size(); ++key) {
		if (mComputations[key].poll) {
			mDirty[key] = true;
			invalidateDependents(key);
		}
	}
	for (size_t key = 0; key < mCurrent.size(); ++key) {
		evaluate(key);
	}
	mSnapshot = mCurrent;
	++mTick;
	for (size_t key = 0; key < mCurrent.size(); ++key) {
		if (mSchema->keys[key].Event) {
			assign(key, false);
		}
	}
}
void VariableContext::expireEntities() {
	for (size_t key = 0; key < mCurrent.size(); ++key) {
		if (mCurrent[key]) {
			if (const auto* reference = std::get_if<EntityRef>(&*mCurrent[key]); reference && !reference->alive()) {
				assign(key, std::nullopt);
			}
		}
	}
}
void VariableContext::migrate(const VariableContext& previous) {
	for (const auto& [name, key] : mSchema->names) {
		auto found = previous.schema().names.find(name);
		if (found != previous.schema().names.end() && mSchema->keys[key].Access == VariableAccess::Local && !mSchema->keys[key].Event
			&& previous.schema().keys[found->second].Access == VariableAccess::Local
			&& previous.schema().keys[found->second].Type == mSchema->keys[key].Type) {
			assign(key, previous.readCurrent(found->second));
		}
	}
}
}
