#include "components.h"
#include "ecsModule/world.h"
#include "support/expect.h"
#include "taskModule/taskSystem.h"
#include "utilsModule/serviceManager.h"

#include <array>
#include <chrono>
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
		expectThrows([&] { anotherBuffer.destroyEntity(pending); }, "pending entity is local to its recording");
		expectThrows([&] { commands.execute(); }, "typed playback needs a world");

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
		expectThrows([&] { commands.removeComponent<Position>(pending); }, "playback expires the pending handle");

		commands.destroyEntity(handle);
		commands.addComponent<Position>(handle, 9);
		commands.execute(world);
		sample();
		expect(count == 1 && sum == 8, "an edit targeting a destroyed entity is skipped");
	}

	void checkScheduling(ArchetypeLayout layout, bool withWorkers) {
		World world { layout };
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
		expect(positionThread != std::thread::id {} && (positionThread != caller) == withWorkers, "position writer uses the expected executor");
		expect(velocityThread != std::thread::id {} && (velocityThread != caller) == withWorkers, "velocity writer uses the expected executor");
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
		expectThrows(
			[&] { world.getSystemManager()->runUpdate(std::chrono::duration<double> { 0.01 }); }, "declared read access rejects a mutable lookup");
	}
}

void runSystemTests() {
	constexpr std::array layouts { ECS::ArchetypeLayout::PerColumn, ECS::ArchetypeLayout::SharedRows };
	for (const auto layout : layouts) {
		checkCommands(layout);
		checkScheduling(layout, false);
		checkAccessValidation(layout);
	}

	TASK::TaskSystem tasks;
	const bool started = tasks.setup() && tasks.initialize() && tasks.getThreadCounts() > 0;
	expect(started, "frame workers start");
	if (started) {
		struct TaskRegistration {
			explicit TaskRegistration(TASK::TaskSystem& value) {
				UTILS::ServiceManager::Set(&value);
			}
			~TaskRegistration() {
				UTILS::ServiceManager::Remove<TASK::TaskSystem>();
			}
		} registration(tasks);
		for (const auto layout : layouts) {
			checkScheduling(layout, true);
		}
	}
}
}
