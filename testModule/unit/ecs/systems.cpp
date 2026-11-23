#include "components.h"
#include "coreModule/ecs/systemBatchExecutor.h"
#include "ecsModule/world.h"
#include "support/expect.h"
#include "taskModule/taskSystem.h"

#include <array>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace IKIGAI::TEST::ECS_CASES {
namespace {
	using namespace IKIGAI::ECS;

	void checkCommands(ArchetypeLayout layout) {
		World world { layout };
		auto& components = *world.getComponentManager();
		const auto entity = world.createEntity();
		const auto handle = components.getEntityHandle(entity);
		components.addComponent(entity, Position { 1 });

		CommandBuffer commands;
		const auto pending = commands.createEntity();
		commands.addComponent<Position>(pending, 8);
		commands.removeComponent<Position>(handle);
		commands.addComponent<Position>(handle, 3);
		CommandBuffer anotherBuffer;
		expectThrows<std::logic_error>([&] { anotherBuffer.destroyEntity(pending); }, "pending entity is local to its recording");
		expectThrows<std::logic_error>([&] { commands.execute(); }, "typed playback needs a world");

		int count = 0;
		int sum = 0;
		const auto sample = [&] {
			count = 0;
			sum = 0;
			components.forEach<Read<Position>>([&](Entity, const Position& value) {
				++count;
				sum += value.value;
			});
		};
		sample();
		expect(count == 1 && sum == 1, "commands do not change the world during recording");
		commands.execute(world);
		sample();
		expect(count == 2 && sum == 11, "playback preserves command order and populates the new entity");
		expectThrows<std::logic_error>([&] { commands.removeComponent<Position>(pending); }, "playback expires the pending handle");

		commands.destroyEntity(handle);
		commands.addComponent<Position>(handle, 9);
		commands.execute(world);
		sample();
		expect(count == 1 && sum == 8, "an edit targeting a destroyed entity is skipped");
	}

	void checkScheduling(ArchetypeLayout layout, const SystemBatchExecutor& executor) {
		std::size_t dispatchedBatches = 0;
		SystemBatchExecutor observedExecutor;
		if (executor) {
			observedExecutor = [&](std::span<const char* const> names, const std::function<void(std::size_t)>& runJob) {
				const bool dispatched = executor(names, runJob);
				if (dispatched) {
					++dispatchedBatches;
				}
				return dispatched;
			};
		}
		World world { layout, std::move(observedExecutor) };
		auto& components = *world.getComponentManager();
		const auto entity = world.createEntity();
		components.addComponents(entity, Position { 1 }, Velocity { 2 });
		const auto caller = std::this_thread::get_id();
		std::thread::id positionThread;
		std::thread::id velocityThread;
		int observedPosition = 0;

		auto reader = world.system<Read<Position>>("reader")
						  .onUpdate([&](Entity, const Position& position, std::chrono::duration<double>) { observedPosition = position.value; })
						  .getSystem();
		reader->runAfter("position writer");
		reader->runAfter("velocity writer");
		world.system<Write<Position>>("position writer").onUpdate([&](Entity, Position& position, std::chrono::duration<double>) {
			positionThread = std::this_thread::get_id();
			position.value = 7;
		});
		world.system<Write<Velocity>>("velocity writer").onUpdate([&](Entity, Velocity& velocity, std::chrono::duration<double>) {
			velocityThread = std::this_thread::get_id();
			velocity.value = 9;
		});
		world.getSystemManager()->runUpdate(std::chrono::duration<double> { 0.01 });
		expect(observedPosition == 7, "reader scheduled after the writer sees its value");
		const auto* velocity = components.tryGetComponent<Velocity>(entity);
		expect(velocity && velocity->value == 9, "independent writer updates its component");
		if (executor) {
			expect(dispatchedBatches == 1, "task pool accepts the batch of independent writers");
		}
		else {
			expect(positionThread == caller && velocityThread == caller, "writers run on the caller without a batch executor");
		}
	}

	void checkAccessValidation(ArchetypeLayout layout) {
		World world { layout };
		auto& components = *world.getComponentManager();
		const auto entity = world.createEntity();
		components.addComponent(entity, Position { 1 });
		const auto handle = components.getComponentHandle<Position>(entity);
		auto system = world.system<Read<Position>>("invalid writer")
						  .onUpdate([&](Entity, const Position&, std::chrono::duration<double>) { (void)components.resolveComponent(handle); })
						  .getSystem();
		system->mValidateAccess = true;
		expectThrows<std::logic_error>(
			[&] { world.getSystemManager()->runUpdate(std::chrono::duration<double> { 0.01 }); }, "declared read access rejects a mutable lookup");
	}

	void checkDependencyCycle(ArchetypeLayout layout) {
		World world { layout };
		auto& components = *world.getComponentManager();
		const auto entity = world.createEntity();
		components.addComponent(entity, Position { 1 });
		int calls = 0;
		const auto update = [&](Entity, const Position&, std::chrono::duration<double>) { ++calls; };
		world.system<Read<Position>>("first").onUpdate(update).getSystem()->runAfter("second");
		world.system<Read<Position>>("second").onUpdate(update).getSystem()->runAfter("first");
		expectThrows<std::logic_error>([&] { world.getSystemManager()->runUpdate({}); }, "ecs.schedule.rejectsCycle");
		expect(calls == 0, "ecs.schedule.cycleRejectedBeforeCallbacks");
	}

	void checkWriteConflict(ArchetypeLayout layout) {
		int dispatched = 0;
		World world { layout, [&](std::span<const char* const> names, const std::function<void(std::size_t)>& runJob) {
						 ++dispatched;
						 for (auto index = names.size(); index > 0; --index) {
							 runJob(index - 1);
						 }
						 return true;
					 } };
		auto& components = *world.getComponentManager();
		const auto entity = world.createEntity();
		components.addComponent(entity, Position { 1 });
		world.system<Write<Position>>("multiply").onUpdate([](Entity, Position& position, std::chrono::duration<double>) { position.value *= 3; });
		world.system<Write<Position>>("add").onUpdate([](Entity, Position& position, std::chrono::duration<double>) { position.value += 2; });
		world.getSystemManager()->runUpdate({});
		const auto* position = components.tryGetComponent<Position>(entity);
		expect(dispatched == 0 && position && position->value == 5, "ecs.schedule.conflictingWritersRunInSeparateBatches");
	}

	struct CommandTarget {
		EntityHandle entity;
	};

	class PositionCommandSystem final : public System {
	public:
		using Record = void (*)(CommandBuffer&, EntityHandle);

		PositionCommandSystem(const char* name, const CommandTarget& target, Record record)
			: mTarget(target)
			, mRecord(record) {
			setName(name);
			setPhases({ SystemPhase::Update });
			readsResource<CommandTarget>();
		}

		void onUpdate(World&, CommandBuffer& buffer, std::chrono::duration<double>) override {
			mRecord(buffer, readResource(mTarget).entity);
		}

	private:
		const CommandTarget& mTarget;
		Record mRecord;
	};

	void checkCommandBatch(ArchetypeLayout layout) {
		std::size_t dispatchedJobs = 0;
		CommandTarget target {};
		World world { layout, [&](std::span<const char* const> names, const std::function<void(std::size_t)>& runJob) {
						 dispatchedJobs += names.size();
						 for (auto index = names.size(); index > 0; --index) {
							 runJob(index - 1);
						 }
						 return true;
					 } };
		auto& components = *world.getComponentManager();
		const auto entity = world.createEntity();
		components.addComponent(entity, Position { 1 });
		target.entity = components.getEntityHandle(entity);
		auto& systems = *world.getSystemManager();
		systems.registerSystem<PositionCommandSystem>(
			"remove", target, +[](CommandBuffer& buffer, EntityHandle entity) { buffer.removeComponent<Position>(entity); });
		systems.registerSystem<PositionCommandSystem>(
			"restore", target, +[](CommandBuffer& buffer, EntityHandle entity) { buffer.addComponent<Position>(entity, 8); });
		int observed = 0;
		auto reader = world.system<Read<Position>>("observe")
						  .onUpdate([&](Entity, const Position& position, std::chrono::duration<double>) { observed = position.value; })
						  .getSystem();
		reader->runAfter("remove");
		reader->runAfter("restore");
		systems.runUpdate({});
		expect(dispatchedJobs == 2, "ecs.commands.recordedInOneBatch");
		expect(observed == 8, "ecs.commands.playedInScheduleOrderBeforeNextBatch");
	}
}

void runSystemTests() {
	constexpr std::array layouts { ECS::ArchetypeLayout::PerColumn, ECS::ArchetypeLayout::SharedRows };
	for (const auto layout : layouts) {
		checkCommands(layout);
		checkScheduling(layout, {});
		checkAccessValidation(layout);
		checkDependencyCycle(layout);
		checkWriteConflict(layout);
		checkCommandBatch(layout);
	}

	auto tasks = std::make_shared<TASK::TaskSystem>();
	const bool started = tasks->setup() && tasks->initialize() && tasks->getThreadCounts() > 0;
	expect(started, "frame workers start");
	if (started) {
		const auto executor = makeTaskSystemBatchExecutor(tasks);
		for (const auto layout : layouts) {
			checkScheduling(layout, executor);
		}
	}
}
}
