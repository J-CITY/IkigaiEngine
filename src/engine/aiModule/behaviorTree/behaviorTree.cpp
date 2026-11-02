#include "behaviorTree.h"
#include <cmath>

namespace IKIGAI::AI {
namespace {
	bool post(const TaskToken& token, TaskResult result, bool aborted) {
		auto inbox = token.inbox.lock();
		if (!inbox || result == TaskResult::Running) {
			return false;
		}
		std::lock_guard lock(inbox->mutex);
		if (inbox->messages.size() >= 4096) {
			return false;
		}
		inbox->messages.push_back({ token.node, token.generation, result, aborted });
		return true;
	}
	template <class Map, class Value> void registerType(Map& map, std::string name, Value compiler) {
		if (name.empty() || !compiler || !map.emplace(name, std::move(compiler)).second) {
			throw std::runtime_error("Empty or duplicate behavior type: " + name);
		}
	}
}
bool TaskToken::complete(TaskResult result) const {
	return post(*this, result, false);
}
bool TaskToken::abortComplete() const {
	return post(*this, TaskResult::Failure, true);
}
bool TaskToken::sameRun(const TaskToken& other) const {
	return node == other.node && generation == other.generation && !inbox.owner_before(other.inbox) && !other.inbox.owner_before(inbox);
}
std::optional<MATH::Vector3f> ExecutionHost::position(const EntityRef&) const {
	return std::nullopt;
}
std::optional<MATH::Vector3f> ExecutionHost::ownerPosition() const {
	return std::nullopt;
}
std::optional<float> ExecutionHost::ownerHeading() const {
	return std::nullopt;
}
bool ExecutionHost::move(const TaskToken&, MATH::Vector3f, std::optional<float>) {
	return false;
}
void ExecutionHost::release(const TaskToken&) { }
const PathQuery* ExecutionHost::navigation() const {
	return nullptr;
}
TaskResult BehaviorTask::tick(TaskContext&) {
	return TaskResult::Running;
}
bool BehaviorTask::ticks() const {
	return false;
}
double BehaviorTask::wakeTime() const {
	return std::numeric_limits<double>::infinity();
}
bool BehaviorTask::abort(TaskContext&) {
	return true;
}
void BehaviorTask::exit(TaskContext&, ExitReason) { }
std::vector<BlackboardKeyId> BehaviorDecorator::dependencies() const {
	return {};
}
double BehaviorDecorator::wakeTime() const {
	return std::numeric_limits<double>::infinity();
}
void BehaviorDecorator::enter(double) { }
void BehaviorDecorator::exit(double) { }
void BehaviorService::activate(TaskContext&) { }
void BehaviorService::deactivate(TaskContext&) { }

void BehaviorRegistry::addTask(std::string name, TaskCompiler compiler) {
	registerType(mTasks, std::move(name), std::move(compiler));
}
void BehaviorRegistry::addDecorator(std::string name, DecoratorCompiler compiler) {
	registerType(mDecorators, std::move(name), std::move(compiler));
}
void BehaviorRegistry::addService(std::string name, ServiceCompiler compiler) {
	registerType(mServices, std::move(name), std::move(compiler));
}
TaskFactory BehaviorRegistry::task(const std::string& name, const nlohmann::json& parameters, const BlackboardSchema& schema) const {
	auto found = mTasks.find(name);
	if (found == mTasks.end()) {
		throw std::runtime_error("Unknown task: " + name);
	}
	return found->second(parameters, schema);
}
DecoratorFactory BehaviorRegistry::decorator(const std::string& name, const nlohmann::json& parameters, const BlackboardSchema& schema) const {
	auto found = mDecorators.find(name);
	if (found == mDecorators.end()) {
		throw std::runtime_error("Unknown decorator: " + name);
	}
	return found->second(parameters, schema);
}
ServiceFactory BehaviorRegistry::service(const std::string& name, const nlohmann::json& parameters, const BlackboardSchema& schema) const {
	auto found = mServices.find(name);
	if (found == mServices.end()) {
		throw std::runtime_error("Unknown service: " + name);
	}
	return found->second(parameters, schema);
}
BehaviorRegistry& BehaviorRegistry::defaults() {
	static BehaviorRegistry registry = [] {
		BehaviorRegistry result;
		registerBuiltinTasks(result);
		registerBuiltinDecorators(result);
		registerBuiltinServices(result);
		return result;
	}();
	return registry;
}

std::shared_ptr<const BehaviorTreeDefinition> BehaviorTreeDefinition::compile(
	const nlohmann::json& document, std::shared_ptr<const BlackboardSchema> schema, const BehaviorRegistry& registry, const std::string& path) {
	std::string location = path;
	try {
		if (!document.at("Nodes").is_array()) {
			throw std::runtime_error("Behavior Nodes must be an array");
		}
		const auto descriptor = readDescriptor<BehaviorTreeDescriptor>(document);
		if (!schema || descriptor.Version != 1 || descriptor.Nodes.empty() || descriptor.Nodes.size() > 4096 || descriptor.TransitionBudget == 0
			|| descriptor.TransitionBudget > 65536 || !std::isfinite(descriptor.AbortTimeout) || descriptor.AbortTimeout <= 0
			|| descriptor.Blackboard.empty()) {
			throw std::runtime_error("Invalid tree Version, schema, node count, budget or AbortTimeout");
		}
		auto result = std::make_shared<BehaviorTreeDefinition>();
		result->path = path;
		result->blackboardPath = descriptor.Blackboard;
		result->schema = std::move(schema);
		result->mode = descriptor.Mode;
		result->transitionBudget = descriptor.TransitionBudget;
		result->abortTimeout = descriptor.AbortTimeout;
		std::map<std::string, size_t> ids;
		for (size_t index = 0; index < descriptor.Nodes.size(); ++index) {
			if (descriptor.Nodes[index].Id.empty() || !ids.emplace(descriptor.Nodes[index].Id, index).second) {
				throw std::runtime_error("Empty or duplicate NodeId: " + descriptor.Nodes[index].Id);
			}
		}
		if (!ids.contains(descriptor.Root)) {
			throw std::runtime_error("Unknown Root: " + descriptor.Root);
		}
		result->root = ids.at(descriptor.Root);
		std::vector<size_t> parents(ids.size(), 0);
		for (size_t index = 0; index < descriptor.Nodes.size(); ++index) {
			auto source = readDescriptor<BehaviorNodeDescriptor>(document.at("Nodes").at(index));
			location = path + " / NodeId=" + source.Id;
			for (const auto* field : { "Children", "Decorators", "Services" }) {
				const auto& authored = document.at("Nodes").at(index);
				if (authored.contains(field) && !authored.at(field).is_array()) {
					throw std::runtime_error(std::string(field) + " must be an array");
				}
			}
			BehaviorNode node;
			node.id = source.Id;
			if (source.Type == "Sequence" || source.Type == "Selector" || source.Type == "Parallel") {
				if (source.Children.empty()) {
					throw std::runtime_error("Composite requires children");
				}
				if (source.Type == "Parallel") {
					node.kind = NodeKind::Parallel;
					node.parallelPolicy = readDescriptor<ParallelParameters>(source.Parameters.document).Policy;
				}
				else {
					node.kind = source.Type == "Sequence" ? NodeKind::Sequence : NodeKind::Selector;
					if (!source.Parameters.document.empty()) {
						throw std::runtime_error("Sequence and Selector cannot have parameters");
					}
				}
			}
			else {
				if (!source.Children.empty()) {
					throw std::runtime_error("Task cannot have children");
				}
				node.createTask = registry.task(source.Type, source.Parameters.document, *result->schema);
				if (!node.createTask) {
					throw std::runtime_error("Empty task factory");
				}
			}
			for (const auto& child : source.Children) {
				if (!ids.contains(child)) {
					throw std::runtime_error("Unknown child: " + child);
				}
				const auto childIndex = ids.at(child);
				if (++parents[childIndex] != 1) {
					throw std::runtime_error("Multiple parents: " + child);
				}
				node.children.push_back(childIndex);
			}
			for (size_t decorator = 0; decorator < source.Decorators.size(); ++decorator) {
				const auto entry = readDescriptor<DecoratorDescriptor>(document.at("Nodes").at(index).at("Decorators").at(decorator));
				auto factory = registry.decorator(entry.Type, entry.Parameters.document, *result->schema);
				if (!factory) {
					throw std::runtime_error("Empty decorator factory");
				}
				if (entry.Type == "TimeLimit" && entry.Aborts != ObserverAborts::Self && entry.Aborts != ObserverAborts::Both) {
					throw std::runtime_error("TimeLimit requires Self or Both aborts");
				}
				node.decorators.push_back({ entry.Aborts, std::move(factory) });
			}
			for (size_t service = 0; service < source.Services.size(); ++service) {
				const auto entry = readDescriptor<ServiceDescriptor>(document.at("Nodes").at(index).at("Services").at(service));
				if (!std::isfinite(entry.Interval) || entry.Interval <= 0) {
					throw std::runtime_error("Service Interval must be positive");
				}
				auto factory = registry.service(entry.Type, entry.Parameters.document, *result->schema);
				if (!factory) {
					throw std::runtime_error("Empty service factory");
				}
				node.services.push_back({ entry.Interval, std::move(factory) });
			}
			result->nodes.push_back(std::move(node));
		}
		location = path;
		if (parents[result->root] != 0) {
			throw std::runtime_error("Root has a parent");
		}
		std::vector<bool> visited(ids.size(), false);
		std::function<void(size_t, size_t)> visit = [&](size_t node, size_t depth) {
			if (depth > 128 || visited[node]) {
				throw std::runtime_error("Cyclic tree or depth exceeds 128");
			}
			visited[node] = true;
			for (auto child : result->nodes[node].children) {
				visit(child, depth + 1);
			}
		};
		visit(result->root, 0);
		for (size_t index = 0; index < visited.size(); ++index) {
			if (!visited[index]) {
				throw std::runtime_error("Unreachable or cyclic node: " + result->nodes[index].id);
			}
		}
		return result;
	} catch (const std::exception& error) {
		throw std::runtime_error(location + ": " + error.what());
	}
}
}
