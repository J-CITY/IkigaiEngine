#include "blackboard.h"
#include <numeric>
#include <set>
namespace IKIGAI::AI {
BlackboardValue blackboardValue(const DATA::Value& value) {
	return value;
}
Blackboard::Blackboard(std::shared_ptr<const BlackboardSchema> schema)
	: mSchema(std::move(schema))
	, mContext(std::make_shared<DATA::VariableContext>(mSchema)) {
	mSlots.resize(mSchema->keys.size());
	std::iota(mSlots.begin(), mSlots.end(), size_t(0));
	mObservedVersions.resize(mSlots.size(), 0);
}
Blackboard::Blackboard(
	std::shared_ptr<const BlackboardSchema> schema, std::shared_ptr<DATA::VariableContext> context, std::vector<BlackboardKeyId> slots)
	: mSchema(std::move(schema))
	, mContext(std::move(context))
	, mSlots(std::move(slots)) {
	if (!mSchema || !mContext || mSlots.size() != mSchema->keys.size()) {
		throw std::runtime_error("Invalid Blackboard context view");
	}
	std::set<size_t> unique;
	for (size_t key = 0; key < mSlots.size(); ++key) {
		const auto& target = mContext->schema().keys.at(mSlots[key]);
		const auto& required = mSchema->keys[key];
		if (target.Type != required.Type || target.Event || !unique.insert(mSlots[key]).second) {
			throw std::runtime_error("Blackboard binding type, event or alias mismatch: " + required.Name);
		}
		if (required.Access == KeyAccess::Local && target.Access != KeyAccess::Local) {
			throw std::runtime_error("Writable Blackboard key targets a read-only context variable: " + required.Name);
		}
	}
	mObservedVersions.resize(mSlots.size(), 0);
}
const BlackboardSchema& Blackboard::schema() const {
	return *mSchema;
}
const std::optional<BlackboardValue>& Blackboard::read(BlackboardKeyId key) const {
	return mContext->readCurrent(mSlots.at(key));
}
uint64_t Blackboard::version(BlackboardKeyId key) const {
	return mContext->version(mSlots.at(key));
}
DATA::Value Blackboard::expressionValue(BlackboardKeyId key) const {
	const auto& value = read(key);
	if (!value || std::holds_alternative<EntityRef>(*value)) {
		throw std::runtime_error("Expression reads Unset or EntityRef: " + mSchema->keys.at(key).Name);
	}
	return *value;
}
void Blackboard::write(BlackboardKeyId key, BlackboardValue value) {
	if (mSchema->keys.at(key).Access != KeyAccess::Local) {
		throw std::runtime_error("Blackboard key is read-only");
	}
	mContext->write(mSlots.at(key), std::move(value));
}
void Blackboard::clear(BlackboardKeyId key) {
	if (mSchema->keys.at(key).Access != KeyAccess::Local) {
		throw std::runtime_error("Blackboard key is read-only");
	}
	mContext->clear(mSlots.at(key));
}
void Blackboard::setInput(BlackboardKeyId key, std::optional<BlackboardValue> value) {
	if (mSchema->keys.at(key).Access != KeyAccess::Input) {
		throw std::runtime_error("Not a Blackboard Input");
	}
	mContext->setInput(mSlots.at(key), std::move(value));
}
std::vector<BlackboardKeyId> Blackboard::takeChanges() {
	std::vector<BlackboardKeyId> result;
	for (size_t key = 0; key < mSlots.size(); ++key) {
		const auto revision = version(key);
		if (mObservedVersions[key] != revision) {
			result.push_back(key);
			mObservedVersions[key] = revision;
		}
	}
	return result;
}
void Blackboard::expireEntities() {
	mContext->expireEntities();
}
void Blackboard::migrate(const Blackboard& previous) {
	for (const auto& [name, key] : mSchema->names) {
		auto found = previous.schema().names.find(name);
		if (found != previous.schema().names.end() && mSchema->keys[key].Access == KeyAccess::Local
			&& previous.schema().keys[found->second].Access == KeyAccess::Local
			&& mSchema->keys[key].Type == previous.schema().keys[found->second].Type) {
			const auto value = previous.read(found->second);
			if (value) {
				write(key, *value);
			}
			else {
				clear(key);
			}
		}
	}
}
std::shared_ptr<DATA::VariableContext> Blackboard::context() const {
	return mContext;
}
}
