#pragma once
#include "aiModule/blackboard/blackboard.h"
#include <deque>
#include <functional>
#include <limits>
#include <mutex>

namespace IKIGAI::AI {
enum class TaskResult { Success, Failure, Running };
enum class ExitReason { Success, Failure, Aborted, Stopped };
enum class ObserverAborts { None, Self, LowerPriority, Both };
enum class RunMode { Once, Loop };
enum class NodeKind { Sequence, Selector, Parallel, Task };
// RequireAll: succeed when every child succeeds; fail on the first failure (remaining branches abort).
// RequireOne: succeed on the first success (remaining branches abort); fail when every child fails.
enum class ParallelPolicy { RequireAll, RequireOne };
struct ParallelParameters {
	ParallelPolicy Policy = ParallelPolicy::RequireAll;
	template <class C> static auto serde(C& c, ParallelParameters& v) {
		using namespace serde::attribute;
		serde::serde_struct(c, v).field(&ParallelParameters::Policy, "Policy", default_ { ParallelPolicy::RequireAll });
	}
};

struct CompletionMessage {
	size_t node = 0;
	uint64_t generation = 0;
	TaskResult result = TaskResult::Failure;
	bool aborted = false;
};
struct CompletionInbox {
	std::mutex mutex {};
	std::vector<CompletionMessage> messages {};
};
struct TaskToken {
	std::weak_ptr<CompletionInbox> inbox {};
	size_t node = 0;
	uint64_t generation = 0;
	bool complete(TaskResult) const;
	bool abortComplete() const;
	bool sameRun(const TaskToken&) const;
};

class PathQuery {
public:
	virtual ~PathQuery() = default;
	virtual std::optional<std::vector<MATH::Vector3f>> findPath(MATH::Vector3f from, MATH::Vector3f to) const = 0;
};
class ExecutionHost {
public:
	virtual ~ExecutionHost() = default;
	virtual std::optional<MATH::Vector3f> position(const EntityRef&) const;
	virtual std::optional<MATH::Vector3f> ownerPosition() const;
	virtual std::optional<float> ownerHeading() const;
	virtual bool move(const TaskToken&, MATH::Vector3f velocity, std::optional<float> heading);
	virtual void release(const TaskToken&);
	virtual const PathQuery* navigation() const;
};
struct TaskContext {
	Blackboard& blackboard;
	double time = 0;
	TaskToken token {};
	ExecutionHost* host = nullptr;
};
class BehaviorTask {
public:
	virtual ~BehaviorTask() = default;
	virtual TaskResult enter(TaskContext&) = 0;
	virtual TaskResult tick(TaskContext&);
	virtual bool ticks() const;
	virtual double wakeTime() const;
	// false means an asynchronous abort; acknowledge through token.abortComplete().
	virtual bool abort(TaskContext&);
	virtual void exit(TaskContext&, ExitReason);
};
class BehaviorDecorator {
public:
	virtual ~BehaviorDecorator() = default;
	virtual bool test(const Blackboard&, double time) const = 0;
	virtual std::vector<BlackboardKeyId> dependencies() const;
	virtual double wakeTime() const;
	virtual void enter(double time);
	virtual void exit(double time);
};
class BehaviorService {
public:
	virtual ~BehaviorService() = default;
	virtual void activate(TaskContext&);
	virtual void update(TaskContext&) = 0;
	virtual void deactivate(TaskContext&);
};

struct DecoratorDescriptor {
	std::string Type {};
	ObserverAborts Aborts = ObserverAborts::None;
	AuthoringParameters Parameters {};
	template <class C> static auto serde(C& c, DecoratorDescriptor& v) {
		using namespace serde::attribute;
		serde::serde_struct(c, v)
			.field(&DecoratorDescriptor::Type, "Type")
			.field(&DecoratorDescriptor::Aborts, "Aborts", default_ { ObserverAborts::None })
			.field(&DecoratorDescriptor::Parameters, "Parameters", default_ { AuthoringParameters {} });
	}
};
struct ServiceDescriptor {
	std::string Type {};
	double Interval = 0.2;
	AuthoringParameters Parameters {};
	template <class C> static auto serde(C& c, ServiceDescriptor& v) {
		using namespace serde::attribute;
		serde::serde_struct(c, v)
			.field(&ServiceDescriptor::Type, "Type")
			.field(&ServiceDescriptor::Interval, "Interval", default_ { 0.2 })
			.field(&ServiceDescriptor::Parameters, "Parameters", default_ { AuthoringParameters {} });
	}
};
struct BehaviorNodeDescriptor {
	std::string Id {};
	std::string Type {};
	AuthoringParameters Parameters {};
	std::vector<std::string> Children {};
	std::vector<DecoratorDescriptor> Decorators {};
	std::vector<ServiceDescriptor> Services {};
	template <class C> static auto serde(C& c, BehaviorNodeDescriptor& v) {
		using namespace serde::attribute;
		serde::serde_struct(c, v)
			.field(&BehaviorNodeDescriptor::Id, "Id")
			.field(&BehaviorNodeDescriptor::Type, "Type")
			.field(&BehaviorNodeDescriptor::Parameters, "Parameters", default_ { AuthoringParameters {} })
			.field(&BehaviorNodeDescriptor::Children, "Children", default_ { std::vector<std::string> {} })
			.field(&BehaviorNodeDescriptor::Decorators, "Decorators", default_ { std::vector<DecoratorDescriptor> {} })
			.field(&BehaviorNodeDescriptor::Services, "Services", default_ { std::vector<ServiceDescriptor> {} });
	}
};
struct BehaviorTreeDescriptor {
	int Version = 1;
	std::string Blackboard {};
	std::string Root {};
	RunMode Mode = RunMode::Loop;
	size_t TransitionBudget = 256;
	double AbortTimeout = 1.0;
	std::vector<BehaviorNodeDescriptor> Nodes {};
	template <class C> static auto serde(C& c, BehaviorTreeDescriptor& v) {
		using namespace serde::attribute;
		serde::serde_struct(c, v)
			.field(&BehaviorTreeDescriptor::Version, "Version")
			.field(&BehaviorTreeDescriptor::Blackboard, "Blackboard")
			.field(&BehaviorTreeDescriptor::Root, "Root")
			.field(&BehaviorTreeDescriptor::Mode, "Mode", default_ { RunMode::Loop })
			.field(&BehaviorTreeDescriptor::TransitionBudget, "TransitionBudget", default_ { size_t(256) })
			.field(&BehaviorTreeDescriptor::AbortTimeout, "AbortTimeout", default_ { 1.0 })
			.field(&BehaviorTreeDescriptor::Nodes, "Nodes");
	}
};

using TaskFactory = std::function<std::unique_ptr<BehaviorTask>()>;
using DecoratorFactory = std::function<std::unique_ptr<BehaviorDecorator>()>;
using ServiceFactory = std::function<std::unique_ptr<BehaviorService>()>;
class BehaviorRegistry {
public:
	using TaskCompiler = std::function<TaskFactory(const nlohmann::json&, const BlackboardSchema&)>;
	using DecoratorCompiler = std::function<DecoratorFactory(const nlohmann::json&, const BlackboardSchema&)>;
	using ServiceCompiler = std::function<ServiceFactory(const nlohmann::json&, const BlackboardSchema&)>;
	void addTask(std::string, TaskCompiler);
	void addDecorator(std::string, DecoratorCompiler);
	void addService(std::string, ServiceCompiler);
	TaskFactory task(const std::string&, const nlohmann::json&, const BlackboardSchema&) const;
	DecoratorFactory decorator(const std::string&, const nlohmann::json&, const BlackboardSchema&) const;
	ServiceFactory service(const std::string&, const nlohmann::json&, const BlackboardSchema&) const;
	static BehaviorRegistry& defaults();

private:
	std::map<std::string, TaskCompiler> mTasks {};
	std::map<std::string, DecoratorCompiler> mDecorators {};
	std::map<std::string, ServiceCompiler> mServices {};
};
struct CompiledDecorator {
	ObserverAborts aborts = ObserverAborts::None;
	DecoratorFactory create {};
};
struct CompiledService {
	double interval = 0.2;
	ServiceFactory create {};
};
struct BehaviorNode {
	std::string id {};
	NodeKind kind = NodeKind::Task;
	ParallelPolicy parallelPolicy = ParallelPolicy::RequireAll;
	std::vector<size_t> children {};
	std::vector<CompiledDecorator> decorators {};
	std::vector<CompiledService> services {};
	TaskFactory createTask {};
};
struct BehaviorTreeDefinition {
	std::string path {};
	std::string blackboardPath {};
	std::shared_ptr<const BlackboardSchema> schema {};
	std::vector<BehaviorNode> nodes {};
	size_t root = 0;
	RunMode mode = RunMode::Loop;
	size_t transitionBudget = 256;
	double abortTimeout = 1;
	static std::shared_ptr<const BehaviorTreeDefinition> compile(
		const nlohmann::json&, std::shared_ptr<const BlackboardSchema>, const BehaviorRegistry&, const std::string& path = {});
};

struct BehaviorTrace {
	double time = 0;
	size_t node = 0;
	std::string event {};
};
struct BehaviorCounters {
	uint64_t transitions = 0;
	uint64_t conditionTests = 0;
	uint64_t taskTicks = 0;
	uint64_t serviceUpdates = 0;
	uint64_t budgetYields = 0;
};
class BehaviorTreeInstance {
public:
	BehaviorTreeInstance(std::shared_ptr<const BehaviorTreeDefinition>, std::shared_ptr<Blackboard>, ExecutionHost* = nullptr);
	~BehaviorTreeInstance();
	BehaviorTreeInstance(const BehaviorTreeInstance&) = delete;
	BehaviorTreeInstance& operator=(const BehaviorTreeInstance&) = delete;
	void update(double dt);
	void stop();
	void restart();
	void pause(bool);
	bool running() const;
	bool paused() const;
	bool aborting() const;
	std::optional<TaskResult> result() const;
	const BehaviorCounters& counters() const;
	const std::deque<BehaviorTrace>& trace() const;
	nlohmann::json snapshot() const;

private:
	struct ParallelBranch {
		size_t childIndex = 0;
		std::vector<size_t> path {};
		std::optional<TaskResult> pending {};
	};
	struct ParallelRun {
		std::vector<ParallelBranch> branches {};
		std::vector<std::optional<TaskResult>> results {};
		bool started = false;
	};
	struct ServiceState {
		std::unique_ptr<BehaviorService> implementation {};
		double next = 0;
		bool active = false;
	};
	struct DecoratorState {
		std::unique_ptr<BehaviorDecorator> implementation {};
		std::vector<BlackboardKeyId> keys {};
		std::vector<uint64_t> versions {};
		double nextWake = std::numeric_limits<double>::infinity();
		bool valid = false;
		bool cached = false;
		bool entered = false;
	};
	struct NodeState {
		std::unique_ptr<BehaviorTask> task {};
		std::vector<ServiceState> services {};
		std::vector<DecoratorState> decorators {};
		std::optional<ParallelRun> parallel {};
		size_t child = 0;
		uint64_t generation = 0;
		bool active = false;
		bool lastAllowed = false;
		bool taskEntered = false;
	};
	struct AbortState {
		size_t cut = 0;
		std::optional<size_t> nextChild {};
		double deadline = 0;
		bool acknowledged = false;
	};
	TaskContext context(size_t);
	bool allowed(size_t, bool force = false);
	void enter(size_t);
	void enterOnPath(size_t, std::vector<size_t>&);
	void leave(size_t, ExitReason);
	void childFinished(TaskResult);
	void branchChildFinished(ParallelBranch&, size_t parallel, TaskResult);
	void pump(size_t& budget);
	void pumpParallel(size_t, size_t& budget);
	void pumpBranch(ParallelBranch&, size_t parallel, size_t& budget);
	void startParallelBranches(size_t);
	void finishParallelBranch(ParallelBranch&, size_t parallel, TaskResult);
	void abortParallelBranch(ParallelBranch&, size_t parallel);
	TaskResult aggregateParallel(const ParallelRun&, ParallelPolicy) const;
	void tickPath(const std::vector<size_t>&);
	void tickParallelTasks();
	void servicesOnPath(const std::vector<size_t>&);
	void observe();
	void beginAbort(size_t cut, std::optional<size_t> nextChild);
	void finishAbort();
	void services();
	void record(size_t, std::string);
	std::shared_ptr<const BehaviorTreeDefinition> mDefinition {};
	std::shared_ptr<Blackboard> mBlackboard {};
	ExecutionHost* mHost = nullptr;
	std::shared_ptr<CompletionInbox> mInbox {};
	std::vector<NodeState> mStates {};
	std::vector<size_t> mPath {};
	std::optional<TaskResult> mPending {};
	std::optional<TaskResult> mResult {};
	std::optional<AbortState> mAbort {};
	std::deque<BehaviorTrace> mTrace;
	BehaviorCounters mCounters {};
	double mTime = 0;
	bool mRunning = true;
	bool mPaused = false;
	bool mRestartNextTick = false;
};
void registerBuiltinTasks(BehaviorRegistry&);
void registerBuiltinDecorators(BehaviorRegistry&);
void registerBuiltinServices(BehaviorRegistry&);
void registerDefaultPathQuery();
}
