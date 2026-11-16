#include "EcsDemo.h"
#include "ecsModule/world.h"

#include <benchmark/benchmark.h>

#include <vector>

namespace {
using namespace Example;
using namespace IKIGAI::ECS;

template <bool Follower> void demoSystem(benchmark::State& state) {
	const auto count = static_cast<std::size_t>(state.range(0));
	World world;
	auto& components = *world.getComponentManager();
	auto& systems = *world.getSystemManager();
	const auto follows = components.getComponentType<Follows>();
	std::vector<Entity> entities;
	entities.reserve(count);
	for (std::size_t index = 0; index < count; ++index) {
		const auto entity = world.createEntity();
		const auto initial = static_cast<float>(index % 2);
		const auto velocity = Follower ? Velocity {} : Velocity { 0.5f, -0.25f };
		components.addComponents(entity, Position { initial, -initial }, velocity);
		entities.push_back(entity);
	}
	if constexpr (Follower) {
		auto& relations = components.getRelationManager();
		for (std::size_t index = 0; index < count; index += 2) {
			relations.addRelation(entities[index], follows, entities[index + 1]);
		}
		registerFollowerSystem(world);
	}
	else {
		systems.addSystem(std::make_shared<MovementSystem>());
	}
	constexpr std::chrono::duration<double> step { 0.5 };
	systems.runUpdate(std::chrono::duration<double> { 0 });
	for (auto _ : state) {
		systems.runUpdate(step);
		benchmark::ClobberMemory();
	}
	for (std::size_t index = 0; index < count; ++index) {
		const auto entity = entities[index];
		const auto initial = static_cast<float>(index % 2);
		bool correct;
		if constexpr (Follower) {
			const auto* velocity = components.tryGetComponent<Velocity>(entity);
			const auto expected = index % 2 == 0 ? 0.1f : 0.0f;
			correct = velocity && velocity->dx == expected && velocity->dy == -expected;
		}
		else {
			const auto* position = components.tryGetComponent<Position>(entity);
			const auto iterations = static_cast<float>(state.iterations());
			correct = position && position->x == initial + iterations * 0.25f && position->y == -initial - iterations * 0.125f;
		}
		if (!correct) {
			state.SkipWithError("demo system did not update the expected data");
			return;
		}
	}
	state.SetItemsProcessed(state.iterations() * count);
	state.counters["position_bytes"] = sizeof(Position);
	state.counters["velocity_bytes"] = sizeof(Velocity);
}

BENCHMARK_TEMPLATE(demoSystem, false)->Name("Demo/Movement")->UseRealTime()->Arg(1000)->Arg(10000)->Arg(100000);
BENCHMARK_TEMPLATE(demoSystem, true)->Name("Demo/Follower")->UseRealTime()->Arg(1000)->Arg(10000)->Arg(100000);

}
