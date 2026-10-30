#include "behaviorTree.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::AI {
BehaviorTreeInstance::BehaviorTreeInstance(
	std::shared_ptr<const BehaviorTreeDefinition> definition, std::shared_ptr<Blackboard> blackboard, ExecutionHost* host)
	: mDefinition(std::move(definition))
	, mBlackboard(std::move(blackboard))
	, mHost(host)
	, mInbox(std::make_shared<CompletionInbox>()) {
	if (!mDefinition || !mBlackboard || &mBlackboard->schema() != mDefinition->schema.get()) {
		throw std::runtime_error("Behavior tree requires its compiled Blackboard schema");
	}
	mStates.resize(mDefinition->nodes.size());
	for (size_t index = 0; index < mStates.size(); ++index) {
		const auto& node = mDefinition->nodes[index];
		auto& state = mStates[index];
		if (node.createTask) {
			state.task = node.createTask();
			if (!state.task) {
				throw std::runtime_error("Task factory returned null: " + node.id);
			}
		}
		for (const auto& decorator : node.decorators) {
			DecoratorState entry;
			entry.implementation = decorator.create();
			if (!entry.implementation) {
				throw std::runtime_error("Decorator factory returned null: " + node.id);
			}
			entry.keys = entry.implementation->dependencies();
			entry.versions.resize(entry.keys.size(), 0);
			for (auto key : entry.keys) {
				mBlackboard->version(key);
			}
			state.decorators.push_back(std::move(entry));
		}
		for (const auto& service : node.services) {
			ServiceState entry;
			entry.implementation = service.create();
			if (!entry.implementation) {
				throw std::runtime_error("Service factory returned null: " + node.id);
			}
			state.services.push_back(std::move(entry));
		}
	}
}
BehaviorTreeInstance::~BehaviorTreeInstance() {
	try {
		stop();
	} catch (...) { /* Cleanup must not throw during scene destruction. */
	}
}
TaskContext BehaviorTreeInstance::context(size_t node) {
	return { *mBlackboard, mTime, { mInbox, node, mStates[node].generation }, mHost };
}
void BehaviorTreeInstance::record(size_t node, std::string event) {
	if (mTrace.size() == 128) {
		mTrace.pop_front();
	}
	mTrace.push_back({ mTime, node, std::move(event) });
}
bool BehaviorTreeInstance::allowed(size_t node, bool force) {
	bool result = true;
	for (auto& decorator : mStates[node].decorators) {
		bool dirty = force || !decorator.valid || mTime >= decorator.nextWake;
		for (size_t index = 0; index < decorator.keys.size(); ++index) {
			dirty |= decorator.versions[index] != mBlackboard->version(decorator.keys[index]);
		}
		if (dirty) {
			++mCounters.conditionTests;
			decorator.cached = decorator.implementation->test(*mBlackboard, mTime);
			decorator.valid = true;
			const auto wake = decorator.implementation->wakeTime();
			decorator.nextWake = wake > mTime ? wake : std::numeric_limits<double>::infinity();
			for (size_t index = 0; index < decorator.keys.size(); ++index) {
				decorator.versions[index] = mBlackboard->version(decorator.keys[index]);
			}
		}
		result &= decorator.cached;
	}
	mStates[node].lastAllowed = result;
	return result;
}
void BehaviorTreeInstance::enter(size_t node) {
	auto& state = mStates[node];
	state.child = 0;
	state.active = true;
	++state.generation;
	mPath.push_back(node);
	++mCounters.transitions;
	record(node, "enter");
	for (auto& decorator : state.decorators) {
		decorator.entered = true;
		decorator.implementation->enter(mTime);
		decorator.valid = false;
	}
	auto ctx = context(node);
	for (size_t index = 0; index < state.services.size(); ++index) {
		auto& service = state.services[index];
		service.active = true;
		service.next = mTime + mDefinition->nodes[node].services[index].interval;
		service.implementation->activate(ctx);
		++mCounters.serviceUpdates;
		service.implementation->update(ctx);
	}
	if (state.task) {
		state.taskEntered = true;
		const auto result = state.task->enter(ctx);
		if (result != TaskResult::Running) {
			mPending = result;
		}
	}
}
void BehaviorTreeInstance::leave(size_t node, ExitReason reason) {
	auto& state = mStates[node];
	if (!state.active) {
		return;
	}
	state.active = false;
	std::exception_ptr failure;
	auto cleanup = [&](auto function) {
		try {
			function();
		} catch (...) {
			if (!failure) {
				failure = std::current_exception();
			}
		}
	};
	auto ctx = context(node);
	for (auto& service : state.services) {
		if (service.active) {
			service.active = false;
			cleanup([&] { service.implementation->deactivate(ctx); });
		}
	}
	if (state.task && state.taskEntered) {
		state.taskEntered = false;
		cleanup([&] { state.task->exit(ctx, reason); });
	}
	if (mHost) {
		cleanup([&] { mHost->release(ctx.token); });
	}
	for (auto& decorator : state.decorators) {
		if (decorator.entered) {
			decorator.entered = false;
			cleanup([&] { decorator.implementation->exit(mTime); });
		}
		decorator.valid = false;
	}
	state.active = false;
	++state.generation;
	++mCounters.transitions;
	record(node,
		reason == ExitReason::Aborted		? "aborted"
			: reason == ExitReason::Stopped ? "stopped"
			: reason == ExitReason::Success ? "success"
											: "failure");
	if (failure) {
		std::rethrow_exception(failure);
	}
}
void BehaviorTreeInstance::childFinished(TaskResult result) {
	if (mPath.empty()) {
		mResult = result;
		mRestartNextTick = mDefinition->mode == RunMode::Loop;
		if (!mRestartNextTick) {
			mRunning = false;
		}
		return;
	}
	const auto node = mPath.back();
	auto& state = mStates[node];
	const bool continueChildren = mDefinition->nodes[node].kind == NodeKind::Sequence ? result == TaskResult::Success : result == TaskResult::Failure;
	if (continueChildren) {
		++state.child;
		if (state.child == mDefinition->nodes[node].children.size()) {
			mPending = result;
		}
	}
	else {
		mPending = result;
	}
}
void BehaviorTreeInstance::pump(size_t& budget) {
	while (budget > 0 && mRunning && !mAbort && !mRestartNextTick) {
		--budget;
		if (mPending) {
			const auto result = *mPending;
			mPending.reset();
			const auto node = mPath.back();
			leave(node, result == TaskResult::Success ? ExitReason::Success : ExitReason::Failure);
			mPath.pop_back();
			childFinished(result);
			continue;
		}
		if (mPath.empty()) {
			if (allowed(mDefinition->root, true)) {
				enter(mDefinition->root);
			}
			else {
				childFinished(TaskResult::Failure);
			}
			continue;
		}
		const auto node = mPath.back();
		if (mStates[node].task) {
			break;
		}
		observe();
		if (mAbort) {
			break;
		}
		const auto child = mDefinition->nodes[node].children.at(mStates[node].child);
		if (allowed(child, true)) {
			enter(child);
		}
		else {
			childFinished(TaskResult::Failure);
		}
	}
	if (budget == 0 && mRunning) {
		++mCounters.budgetYields;
	}
}
void BehaviorTreeInstance::services() {
	for (const auto node : mPath) {
		auto ctx = context(node);
		auto& state = mStates[node];
		for (size_t index = 0; index < state.services.size(); ++index) {
			auto& service = state.services[index];
			if (service.active && service.next <= mTime) {
				service.next = mTime + mDefinition->nodes[node].services[index].interval;
				++mCounters.serviceUpdates;
				service.implementation->update(ctx);
			}
		}
	}
}
void BehaviorTreeInstance::beginAbort(size_t cut, std::optional<size_t> nextChild) {
	mPending.reset();
	mAbort = AbortState { cut, nextChild, mTime + mDefinition->abortTimeout, true };
	for (size_t position = cut; position < mPath.size(); ++position) {
		const auto node = mPath[position];
		auto ctx = context(node);
		for (auto& service : mStates[node].services) {
			if (service.active) {
				service.active = false;
				service.implementation->deactivate(ctx);
			}
		}
	}
	if (!mPath.empty()) {
		const auto node = mPath.back();
		auto ctx = context(node);
		if (mHost) {
			mHost->release(ctx.token);
		}
		if (mStates[node].task) {
			mAbort->acknowledged = mStates[node].task->abort(ctx);
		}
		record(node, "abort-request");
	}
}
void BehaviorTreeInstance::finishAbort() {
	const auto request = *mAbort;
	while (mPath.size() > request.cut) {
		leave(mPath.back(), ExitReason::Aborted);
		mPath.pop_back();
	}
	mAbort.reset();
	if (request.nextChild && !mPath.empty()) {
		mStates[mPath.back()].child = *request.nextChild;
	}
	else {
		childFinished(TaskResult::Failure);
	}
}
void BehaviorTreeInstance::observe() {
	for (size_t depth = 0; depth < mPath.size(); ++depth) {
		const auto node = mPath[depth];
		bool self = false;
		for (const auto& decorator : mDefinition->nodes[node].decorators) {
			self |= decorator.aborts == ObserverAborts::Self || decorator.aborts == ObserverAborts::Both;
		}
		if (self) {
			allowed(node);
			for (size_t index = 0; index < mStates[node].decorators.size(); ++index) {
				const auto mode = mDefinition->nodes[node].decorators[index].aborts;
				if ((mode == ObserverAborts::Self || mode == ObserverAborts::Both) && !mStates[node].decorators[index].cached) {
					beginAbort(depth, std::nullopt);
					return;
				}
			}
		}
		if (mDefinition->nodes[node].kind != NodeKind::Selector || depth + 1 == mPath.size()) {
			continue;
		}
		const auto& children = mDefinition->nodes[node].children;
		for (size_t candidate = 0; candidate < mStates[node].child; ++candidate) {
			const auto child = children[candidate];
			bool lower = false;
			bool relevantChange = false;
			for (size_t index = 0; index < mDefinition->nodes[child].decorators.size(); ++index) {
				const auto mode = mDefinition->nodes[child].decorators[index].aborts;
				if (mode != ObserverAborts::LowerPriority && mode != ObserverAborts::Both) {
					continue;
				}
				lower = true;
				const auto& state = mStates[child].decorators[index];
				relevantChange |= !state.valid || mTime >= state.nextWake;
				for (size_t key = 0; key < state.keys.size(); ++key) {
					relevantChange |= state.versions[key] != mBlackboard->version(state.keys[key]);
				}
			}
			if (lower) {
				const bool previouslyAllowed = mStates[child].lastAllowed;
				if (allowed(child) && !previouslyAllowed && relevantChange) {
					beginAbort(depth + 1, candidate);
					return;
				}
			}
		}
	}
}
void BehaviorTreeInstance::update(double dt) {
	if (!std::isfinite(dt) || dt < 0) {
		throw std::runtime_error("Invalid behavior delta time");
	}
	if (!mRunning || mPaused) {
		return;
	}
	try {
		mTime += dt;
		mBlackboard->expireEntities();
		if (mRestartNextTick) {
			mRestartNextTick = false;
			mResult.reset();
		}
		if (!mAbort) {
			services();
			observe();
		}
		std::vector<CompletionMessage> messages;
		{
			std::lock_guard lock(mInbox->mutex);
			messages.swap(mInbox->messages);
		}
		for (const auto& message : messages) {
			if (mPath.empty() || mPath.back() != message.node || mStates[message.node].generation != message.generation) {
				continue;
			}
			if (mAbort) {
				if (message.aborted) {
					mAbort->acknowledged = true;
				}
			}
			else if (!message.aborted && !mPending) {
				mPending = message.result;
			}
		}
		if (mAbort) {
			if (!mAbort->acknowledged && mTime < mAbort->deadline) {
				return;
			}
			if (!mAbort->acknowledged) {
				record(mPath.back(), "abort-timeout");
			}
			finishAbort();
		}
		if (!mPath.empty() && !mPending) {
			const auto node = mPath.back();
			auto& task = mStates[node].task;
			if (task && (task->ticks() || mTime >= task->wakeTime())) {
				++mCounters.taskTicks;
				auto ctx = context(node);
				auto result = task->tick(ctx);
				if (result != TaskResult::Running) {
					mPending = result;
				}
			}
		}
		size_t budget = mDefinition->transitionBudget;
		pump(budget);
		// Newly activated services and writes from tasks are observed before
		// returning motor commands to the scene; all passes share one budget.
		while (budget > 0 && mRunning && !mRestartNextTick) {
			if (!mAbort) {
				services();
				observe();
			}
			if (!mAbort) {
				break;
			}
			if (!mAbort->acknowledged) {
				break;
			}
			finishAbort();
			pump(budget);
		}
	} catch (...) {
		try {
			stop();
		} catch (...) { }
		throw;
	}
}
void BehaviorTreeInstance::stop() {
	mRunning = false;
	mRestartNextTick = false;
	if (!mPath.empty() && !mAbort && mStates[mPath.back()].task && mStates[mPath.back()].taskEntered) {
		auto ctx = context(mPath.back());
		try {
			mStates[mPath.back()].task->abort(ctx);
		} catch (...) {
			record(mPath.back(), "abort-error");
		}
	}
	while (!mPath.empty()) {
		const auto node = mPath.back();
		mPath.pop_back();
		try {
			leave(node, ExitReason::Stopped);
		} catch (...) {
			record(node, "cleanup-error");
		}
	}
	mInbox.reset();
	mAbort.reset();
	mPending.reset();
}
void BehaviorTreeInstance::restart() {
	stop();
	mInbox = std::make_shared<CompletionInbox>();
	mRunning = true;
	mPaused = false;
	mResult.reset();
	for (auto& state : mStates) {
		for (auto& decorator : state.decorators) {
			decorator.valid = false;
		}
	}
}
void BehaviorTreeInstance::pause(bool value) {
	if (value == mPaused) {
		return;
	}
	mPaused = value;
	if (value && mHost) {
		for (auto node : mPath) {
			mHost->release(context(node).token);
		}
	}
	if (!value) {
		for (auto& state : mStates) {
			for (auto& decorator : state.decorators) {
				decorator.valid = false;
			}
		}
	}
}
bool BehaviorTreeInstance::running() const {
	return mRunning;
}
bool BehaviorTreeInstance::paused() const {
	return mPaused;
}
bool BehaviorTreeInstance::aborting() const {
	return mAbort.has_value();
}
std::optional<TaskResult> BehaviorTreeInstance::result() const {
	return mResult;
}
const BehaviorCounters& BehaviorTreeInstance::counters() const {
	return mCounters;
}
const std::deque<BehaviorTrace>& BehaviorTreeInstance::trace() const {
	return mTrace;
}
nlohmann::json BehaviorTreeInstance::snapshot() const {
	nlohmann::json result = { { "Resource", mDefinition->path }, { "Time", mTime }, { "Running", mRunning }, { "Paused", mPaused },
		{ "Aborting", bool(mAbort) }, { "ActivePath", nlohmann::json::array() }, { "Blackboard", nlohmann::json::object() },
		{ "Trace", nlohmann::json::array() }, { "ConditionTests", mCounters.conditionTests }, { "TaskTicks", mCounters.taskTicks },
		{ "ServiceUpdates", mCounters.serviceUpdates }, { "Transitions", mCounters.transitions }, { "BudgetYields", mCounters.budgetYields } };
	for (auto node : mPath) {
		result["ActivePath"].push_back(mDefinition->nodes[node].id);
	}
	result["Result"] = mResult ? (*mResult == TaskResult::Success ? nlohmann::json("Success") : nlohmann::json("Failure")) : nlohmann::json(nullptr);
	result["Services"] = nlohmann::json::array();
	for (auto node : mPath) {
		for (const auto& service : mStates[node].services) {
			result["Services"].push_back({ { "NodeId", mDefinition->nodes[node].id }, { "Active", service.active }, { "NextUpdate", service.next } });
		}
		if (mStates[node].task) {
			const auto wake = mStates[node].task->wakeTime();
			result["Task"] = { { "NodeId", mDefinition->nodes[node].id }, { "Generation", mStates[node].generation },
				{ "WakeTime", std::isfinite(wake) ? nlohmann::json(wake) : nlohmann::json(nullptr) } };
		}
	}
	if (mAbort) {
		result["AbortDeadline"] = mAbort->deadline;
	}
	for (const auto& [name, key] : mBlackboard->schema().names) {
		const auto& value = mBlackboard->read(key);
		if (!value) {
			result["Blackboard"][name] = nullptr;
			continue;
		}
		std::visit(
			[&](const auto& item) {
				using T = std::decay_t<decltype(item)>;
				if constexpr (std::is_same_v<T, EntityRef>) {
					result["Blackboard"][name] = { { "Id", item.id }, { "Alive", item.alive() } };
				}
				else if constexpr (std::is_same_v<T, MATH::Vector3f>) {
					result["Blackboard"][name] = { item.x, item.y, item.z };
				}
				else {
					result["Blackboard"][name] = item;
				}
			},
			*value);
	}
	for (const auto& event : mTrace) {
		result["Trace"].push_back({ { "Time", event.time }, { "NodeId", mDefinition->nodes[event.node].id }, { "Event", event.event } });
	}
	return result;
}
}
