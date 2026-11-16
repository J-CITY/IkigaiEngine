#include "backends.h"
#include "registryFixture.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

namespace IKIGAI::ECS {
struct EcsBenchmarkAccess {
	static std::size_t sumRows(const ComponentManager& manager, std::span<const Entity> entities) {
		std::size_t sum = 0;
		ComponentManager::EntityRecords::PageCursor cursor;
		for (const auto entity : entities) {
			const auto* record = manager.mEntityRecords.find(entity, cursor);
			if (record) {
				sum += record->row;
			}
		}
		return sum;
	}

	static Archetype* table(const ComponentManager& manager, Entity entity) {
		return manager.mEntityRecords.at(entity).archetype;
	}

	static const Archetype::Transition* addPlan(ComponentManager& manager, Entity entity, ComponentType type) {
		return manager.getSingleComponentTransition(table(manager, entity), type, true);
	}

	static Archetype* addTarget(ComponentManager& manager, Entity entity, ComponentType type) {
		const auto* plan = addPlan(manager, entity, type);
		return plan ? plan->target : table(manager, entity);
	}

	template <class T>
	static void movePrepared(
		ComponentManager& manager, Entity entity, Archetype& target, ComponentType type, const Archetype::Transition* plan, T value) {
		auto& record = manager.mEntityRecords.at(entity);
		manager.addIndexedComponents(entity, record, &target, std::span(&type, 1), plan, value);
	}
};
}

namespace ECS_BENCHMARK {
namespace {
	using IKIGAI::ECS::ArchetypeLayout;

	constexpr std::array entityCounts { 1000, 10000, 100000 };
	constexpr float delta = 0.5f;

	void movementSizes(benchmark::internal::Benchmark* benchmark) {
		benchmark->UseRealTime();
		for (const auto count : entityCounts) {
			benchmark->Args({ count, 100, 1 });
			benchmark->Args({ count, 10, 1 });
			benchmark->Args({ count, 50, 1 });
			benchmark->Args({ count, 100, 16 });
		}
		benchmark->ArgNames({ "entities", "match_pct", "signatures" });
	}

	void entitySizes(benchmark::internal::Benchmark* benchmark) {
		benchmark->UseRealTime();
		for (const auto count : entityCounts) {
			benchmark->Arg(count);
		}
	}

	template <class Backend, std::size_t... Indexes>
	void addTags(Backend& backend, typename Backend::Entity entity, std::size_t mask, std::index_sequence<Indexes...>) {
		(
			[&] {
				if ((mask & (std::size_t { 1 } << Indexes)) != 0) {
					backend.add(entity, Tag<Indexes> {});
				}
			}(),
			...);
	}

	template <class Backend, bool LookupOnly = false> void movement(benchmark::State& state) {
		using PositionType = typename Backend::PositionType;
		using VelocityType = typename Backend::VelocityType;
		const auto count = static_cast<std::size_t>(state.range(0));
		const auto stride = 100 / static_cast<std::size_t>(state.range(1));
		const auto signatures = static_cast<std::size_t>(state.range(2));
		Backend backend;
		backend.registerMovement();
		std::vector<typename Backend::Entity> entities;
		entities.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			const auto entity = backend.create();
			entities.push_back(entity);
			const auto value = static_cast<float>(index % 29);
			backend.add(entity, PositionType { value, -value });
			if (index % stride == 0) {
				backend.add(entity, VelocityType { 0.5f, -0.25f });
			}
			addTags(backend, entity, index % signatures, std::make_index_sequence<4> {});
		}

		const auto expectedMatches = (count + stride - 1) / stride;
		std::size_t matched = 0;
		{
			auto countMatches = backend.makePass([&](PositionType&, const VelocityType&) { ++matched; });
			countMatches();
		}
		if (matched != expectedMatches) {
			state.SkipWithError("movement query matched the wrong number of entities");
			return;
		}
		const auto move = backend.makePass([](PositionType& position, const VelocityType& velocity) {
			position.x += velocity.x * delta;
			position.y += velocity.y * delta;
		});
		std::span<const IKIGAI::ECS::Entity> lookupEntities;
		if constexpr (LookupOnly) {
			lookupEntities = backend.components.template getComponentsArray<VelocityType>().getEntities();
			const auto expectedSum = expectedMatches * (expectedMatches - 1) / 2 * stride;
			if (IKIGAI::ECS::EcsBenchmarkAccess::sumRows(backend.components, lookupEntities) != expectedSum) {
				state.SkipWithError("record lookup returned unexpected rows");
				return;
			}
		}
		for (auto _ : state) {
			if constexpr (LookupOnly) {
				benchmark::DoNotOptimize(IKIGAI::ECS::EcsBenchmarkAccess::sumRows(backend.components, lookupEntities));
			}
			else {
				move();
			}
			benchmark::ClobberMemory();
		}
		const auto iterations = static_cast<float>(state.iterations());
		for (std::size_t index = 0; index < count; ++index) {
			const auto* position = backend.template get<PositionType>(entities[index]);
			const auto initial = static_cast<float>(index % 29);
			const auto updates = !LookupOnly && index % stride == 0 ? iterations : 0.0f;
			if (!position || position->x != initial + updates * 0.25f || position->y != -initial - updates * 0.125f) {
				state.SkipWithError("movement updated the wrong entity or value");
				return;
			}
		}
		state.SetItemsProcessed(state.iterations() * expectedMatches);
		state.counters["position_bytes"] = sizeof(PositionType);
		state.counters["velocity_bytes"] = sizeof(VelocityType);
	}

	enum class Structure { Create, Destroy, Add, Remove, AddRemove };

	template <class Backend, Structure Operation> void structural(benchmark::State& state) {
		const auto count = static_cast<std::size_t>(state.range(0));
		using Entity = typename Backend::Entity;
		std::vector<Entity> entities(count);
		for (auto _ : state) {
			state.PauseTiming();
			auto backend = std::make_unique<Backend>();
			if constexpr (Operation == Structure::Add) {
				const auto warm = backend->create();
				backend->add(warm, typename Backend::PositionType {});
				backend->add(warm, Marker { 7 });
				backend->template remove<Marker>(warm);
				backend->destroy(warm);
			}
			if constexpr (Operation != Structure::Create) {
				for (auto& entity : entities) {
					entity = backend->create();
					if constexpr (Operation == Structure::Add || Operation == Structure::Remove || Operation == Structure::AddRemove) {
						backend->add(entity, typename Backend::PositionType { 1.0f, -2.0f });
					}
					if constexpr (Operation == Structure::Remove) {
						backend->add(entity, Marker { 7 });
					}
				}
			}
			state.ResumeTiming();

			for (auto& entity : entities) {
				if constexpr (Operation == Structure::Create) {
					entity = backend->create();
				}
				else if constexpr (Operation == Structure::Destroy) {
					backend->destroy(entity);
				}
				else if constexpr (Operation == Structure::AddRemove) {
					backend->add(entity, Marker { 7 });
					backend->template remove<Marker>(entity);
				}
				else if constexpr (Operation == Structure::Add) {
					backend->add(entity, Marker { 7 });
				}
				else {
					backend->template remove<Marker>(entity);
				}
			}
			benchmark::ClobberMemory();

			state.PauseTiming();
			const auto valid = std::ranges::all_of(entities, [&](Entity entity) {
				if constexpr (Operation == Structure::Create || Operation == Structure::Destroy) {
					return backend->alive(entity) == (Operation == Structure::Create);
				}
				else {
					const auto* position = backend->template get<typename Backend::PositionType>(entity);
					const auto* marker = backend->template get<Marker>(entity);
					const bool correctMarker = Operation == Structure::Add ? marker && marker->value == 7 : !marker;
					return position && position->x == 1.0f && position->y == -2.0f && correctMarker;
				}
			});
			backend.reset();
			state.ResumeTiming();
			if (!valid) {
				state.SkipWithError("structural operation produced an unexpected entity state");
				break;
			}
		}
		constexpr auto operations = Operation == Structure::AddRemove ? 2 : 1;
		state.SetItemsProcessed(state.iterations() * count * operations);
	}

	void cachedQueryBuild(benchmark::State& state) {
		Flecs<flecs::QueryCacheAuto> backend;
		const auto entity = backend.create();
		backend.add(entity, Flecs<>::PositionType {});
		backend.add(entity, Flecs<>::VelocityType {});
		for (std::size_t signature = 1; signature < 16; ++signature) {
			const auto tagged = backend.create();
			backend.add(tagged, Flecs<>::PositionType {});
			backend.add(tagged, Flecs<>::VelocityType {});
			addTags(backend, tagged, signature, std::make_index_sequence<4> {});
		}
		for (auto _ : state) {
			state.SetIterationTime(backend.queryBuildNs() * 1e-9);
		}
	}

	template <std::size_t Index> struct RetainedComponent {
		int value = 0;
	};

	enum class TransitionProbe { PublicAdd, TargetLookup, MovePrepared };

	template <std::size_t Columns, bool ReuseCapacity, ArchetypeLayout Layout, TransitionProbe Probe = TransitionProbe::PublicAdd>
	void storageGrowth(benchmark::State& state) {
		using IKIGAI::ECS::EcsBenchmarkAccess;
		const auto count = static_cast<std::size_t>(state.range(0));
		using Backend = Ikigai<alignof(float), false, Layout>;
		std::vector<typename Backend::Entity> entities(count);
		const std::size_t warmRows = ReuseCapacity ? count : 1;
		std::vector<typename Backend::Entity> warmEntities(warmRows);
		for (auto _ : state) {
			state.PauseTiming();
			auto backend = std::make_unique<Backend>();
			const auto createSource = [&](std::size_t value) {
				const auto entity = backend->create();
				[&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
					backend->components.addComponents(entity, RetainedComponent<Indices> { static_cast<int>(value + Indices) }...);
				}(std::make_index_sequence<Columns> {});
				return entity;
			};
			// Warm only the target table before creating the timed sources in the same order.
			for (auto& entity : warmEntities) {
				entity = createSource(0);
				backend->add(entity, Marker { 7 });
			}
			auto* target = EcsBenchmarkAccess::table(backend->components, warmEntities.back());
			const auto markerType = backend->components.template getComponentType<Marker>();
			for (const auto entity : warmEntities | std::views::reverse) {
				backend->destroy(entity);
			}
			for (std::size_t i = 0; i < count; ++i) {
				entities[i] = createSource(i);
			}
			const auto* plan
				= Probe == TransitionProbe::MovePrepared ? EcsBenchmarkAccess::addPlan(backend->components, entities.front(), markerType) : nullptr;
			state.ResumeTiming();
			std::size_t foundTargets = 0;
			for (const auto entity : entities) {
				if constexpr (Probe == TransitionProbe::TargetLookup) {
					foundTargets += EcsBenchmarkAccess::addTarget(backend->components, entity, markerType) == target;
				}
				else if constexpr (Probe == TransitionProbe::MovePrepared) {
					EcsBenchmarkAccess::movePrepared(backend->components, entity, *target, markerType, plan, Marker { 7 });
				}
				else {
					backend->add(entity, Marker { 7 });
				}
			}
			benchmark::ClobberMemory();
			state.PauseTiming();
			bool valid = Probe != TransitionProbe::TargetLookup || foundTargets == count;
			for (std::size_t i = 0; i < count; ++i) {
				const auto* marker = backend->template get<Marker>(entities[i]);
				const auto* first = backend->template get<RetainedComponent<0>>(entities[i]);
				const auto* last = backend->template get<RetainedComponent<Columns - 1>>(entities[i]);
				const auto validMarker = Probe == TransitionProbe::TargetLookup ? marker == nullptr : marker && marker->value == 7;
				valid = valid && validMarker && first && first->value == static_cast<int>(i) && last
					&& last->value == static_cast<int>(i + Columns - 1);
			}
			backend.reset();
			state.ResumeTiming();
			if (!valid) {
				state.SkipWithError("bulk migration did not preserve the expected components");
				break;
			}
		}
		state.SetItemsProcessed(state.iterations() * count);
	}

	template <bool Warm> void addTransition(benchmark::State& state) {
		using Backend = Ikigai<>;
		constexpr std::size_t batchSize = 64;
		std::array<std::unique_ptr<Backend>, batchSize> worlds;
		std::array<Backend::Entity, batchSize> entities;
		for (auto _ : state) {
			state.PauseTiming();
			for (std::size_t i = 0; i < batchSize; ++i) {
				worlds[i] = std::make_unique<Backend>();
				auto& world = *worlds[i];
				world.components.registerComponent<Marker>();
				entities[i] = world.create();
				world.add(entities[i], Backend::PositionType { 1.0f, -2.0f });
				if constexpr (Warm) {
					world.add(entities[i], Marker { 7 });
					world.remove<Marker>(entities[i]);
				}
			}
			state.ResumeTiming();
			for (std::size_t i = 0; i < batchSize; ++i) {
				worlds[i]->add(entities[i], Marker { 7 });
			}
			benchmark::ClobberMemory();
			state.PauseTiming();
			bool valid = true;
			for (std::size_t i = 0; i < batchSize; ++i) {
				const auto* position = worlds[i]->get<Backend::PositionType>(entities[i]);
				const auto* marker = worlds[i]->get<Marker>(entities[i]);
				valid = valid && position && position->x == 1.0f && position->y == -2.0f && marker && marker->value == 7;
				worlds[i].reset();
			}
			state.ResumeTiming();
			if (!valid) {
				state.SkipWithError("transition did not preserve the expected components");
				break;
			}
		}
		state.SetItemsProcessed(state.iterations() * batchSize);
	}

	template <bool Register, bool SparseIds = false> void entityRegistry(benchmark::State& state) {
		using IKIGAI::ECS::Entity;
		const auto count = static_cast<std::size_t>(state.range(0));
		std::vector<Entity> entities(count);
		if constexpr (SparseIds) {
			for (std::size_t index = 0; index < count; ++index) {
				entities[index] = IKIGAI::BENCHMARKS::sparseRegistryEntity(index);
			}
		}
		else if constexpr (Register) {
			std::ranges::generate(entities, Entity::create);
		}
		for (auto _ : state) {
			if constexpr (Register) {
				state.PauseTiming();
				auto world = std::make_unique<Ikigai<>>();
				state.ResumeTiming();
				for (const auto entity : entities) {
					world->world.registerEntity(entity);
				}
				benchmark::ClobberMemory();
				state.PauseTiming();
				const bool valid = std::ranges::all_of(entities, [&](Entity entity) { return world->alive(entity); });
				world.reset();
				state.ResumeTiming();
				if (!valid) {
					state.SkipWithError("registry lost a supplied ID");
					break;
				}
			}
			else {
				std::ranges::generate(entities, Entity::create);
				benchmark::ClobberMemory();
			}
		}
		state.SetItemsProcessed(state.iterations() * count);
	}

	template <bool Record> void commands(benchmark::State& state) {
		const auto count = static_cast<std::size_t>(state.range(0));
		Ikigai<> backend;
		std::vector<IKIGAI::ECS::EntityHandle> entities;
		entities.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			const auto entity = backend.create();
			backend.add(entity, Ikigai<>::PositionType {});
			entities.push_back(backend.components.getEntityHandle(entity));
		}
		const auto record = [&](IKIGAI::ECS::CommandBuffer& buffer) {
			for (const auto entity : entities) {
				buffer.addComponent<Marker>(entity, 7);
			}
		};
		for (auto _ : state) {
			state.PauseTiming();
			IKIGAI::ECS::CommandBuffer buffer;
			if constexpr (!Record) {
				record(buffer);
			}
			state.ResumeTiming();
			if constexpr (Record) {
				record(buffer);
			}
			else {
				buffer.execute(backend.world);
			}
			state.PauseTiming();
			if constexpr (Record) {
				buffer.execute(backend.world);
			}
			bool correct = true;
			for (const auto handle : entities) {
				const auto entity = *backend.components.resolveEntity(handle);
				const auto* added = backend.get<Marker>(entity);
				correct = correct && added && added->value == 7;
				backend.remove<Marker>(entity);
			}
			state.ResumeTiming();
			if (!correct) {
				state.SkipWithError("command buffer did not add the expected component");
				break;
			}
		}
		state.SetItemsProcessed(state.iterations() * count);
	}

	using IkigaiCompact = Ikigai<>;
	using IkigaiMixed = Ikigai<alignof(float), true>;
	using IkigaiAligned = Ikigai<16>;
	using FlecsUncached = Flecs<>;
	using FlecsCached = Flecs<flecs::QueryCacheAuto>;

	class IkigaiNoHandle : public IkigaiCompact {
	public:
		template <class T> void add(Entity entity, T value) {
			components.addComponents(entity, std::move(value));
		}
	};

	class IkigaiWithListeners : public IkigaiCompact {
	public:
		IkigaiWithListeners() {
			using IKIGAI::ECS::OnAdd;
			using IKIGAI::ECS::OnRemove;
			auto& events = components.getEventManager();
			const auto observe = [](const auto& event) { benchmark::DoNotOptimize(event.component->value); };
			events.subscribe<OnAdd<Marker>>(observe);
			events.subscribe<OnRemove<Marker>>(observe);
		}
	};

	template <class Backend, std::size_t Columns, bool Batch> void batchAddition(benchmark::State& state) {
		const auto count = static_cast<std::size_t>(state.range(0));
		std::vector<typename Backend::Entity> entities(count);
		for (auto _ : state) {
			state.PauseTiming();
			auto backend = std::make_unique<Backend>();
			const auto createSource = [&](std::size_t value) {
				const auto entity = backend->create();
				[&]<std::size_t... Index>(std::index_sequence<Index...>) {
					(backend->add(entity, RetainedComponent<Index> { static_cast<int>(value + Index) }), ...);
				}(std::make_index_sequence<Columns> {});
				return entity;
			};
			const auto warm = createSource(0);
			backend->add(warm, Marker { 7 });
			backend->destroy(warm);
			for (std::size_t index = 0; index < count; ++index) {
				entities[index] = createSource(index);
			}
			state.ResumeTiming();
			if constexpr (Batch && std::is_same_v<Backend, IkigaiCompact>) {
				backend->components.addArchetypeComponentBatch(entities, Marker { 7 });
			}
			else if constexpr (Batch) {
				backend->addBatch(entities, Marker { 7 });
			}
			else {
				for (const auto entity : entities) {
					backend->add(entity, Marker { 7 });
				}
			}
			benchmark::ClobberMemory();
			state.PauseTiming();
			bool valid = true;
			for (std::size_t index = 0; index < count && valid; ++index) {
				const auto entity = entities[index];
				const auto* marker = backend->template get<Marker>(entity);
				const auto retained = [&]<std::size_t I> {
					const auto* component = backend->template get<RetainedComponent<I>>(entity);
					return component && component->value == static_cast<int>(index + I);
				};
				valid = marker && marker->value == 7 && [&]<std::size_t... Index>(std::index_sequence<Index...>) {
					return (retained.template operator()<Index>() && ...);
				}(std::make_index_sequence<Columns> {});
			}
			backend.reset();
			state.ResumeTiming();
			if (!valid) {
				state.SkipWithError("batch addition lost a component or updated the wrong entity");
				break;
			}
		}
		state.SetItemsProcessed(state.iterations() * count);
	}

	template <std::size_t Columns> void registerBatchAddition() {
		const auto add = [](const char* name, void (*run)(benchmark::State&)) {
			const auto label = std::string("ArchetypeBatch/") + name + "/" + std::to_string(Columns);
			benchmark::RegisterBenchmark(label, run)->UseRealTime()->Arg(1000)->Arg(100000);
		};
		add("Ikigai/Single", batchAddition<IkigaiCompact, Columns, false>);
		add("Ikigai/SingleNoHandle", batchAddition<IkigaiNoHandle, Columns, false>);
		add("Ikigai/Reserved", batchAddition<IkigaiCompact, Columns, true>);
		add("EnTT/Single", batchAddition<EnTT, Columns, false>);
		add("EnTT/ReservedRange", batchAddition<EnTT, Columns, true>);
		add("Flecs/Single", batchAddition<FlecsUncached, Columns, false>);
	}

	const bool batchRegistered = [] {
		registerBatchAddition<1>();
		registerBatchAddition<4>();
		registerBatchAddition<8>();
		return true;
	}();

	BENCHMARK_TEMPLATE(movement, IkigaiCompact)->Apply(movementSizes);
	BENCHMARK_TEMPLATE(movement, EnTT)->Apply(movementSizes);
	BENCHMARK_TEMPLATE(movement, FlecsUncached)->Apply(movementSizes);
	BENCHMARK_TEMPLATE(movement, FlecsCached)->Apply(movementSizes);
	BENCHMARK_TEMPLATE(movement, IkigaiAligned)->UseRealTime()->Args({ 1000, 100, 1 })->Args({ 10000, 100, 1 })->Args({ 100000, 100, 1 });
	BENCHMARK_TEMPLATE(movement, IkigaiMixed)
		->UseRealTime()
		->Args({ 1000, 10, 1 })
		->Args({ 1000, 50, 1 })
		->Args({ 10000, 10, 1 })
		->Args({ 10000, 50, 1 })
		->Args({ 100000, 10, 1 })
		->Args({ 100000, 50, 1 });
	BENCHMARK_TEMPLATE(movement, IkigaiMixed, true)->Name("Ikigai/MixedRows/Lookup")->UseRealTime()->Args({ 100000, 10, 1 })->Args({ 100000, 50, 1 });
	BENCHMARK(cachedQueryBuild)->UseManualTime();

	BENCHMARK_TEMPLATE(structural, IkigaiCompact, Structure::Create)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, EnTT, Structure::Create)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, FlecsUncached, Structure::Create)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, IkigaiCompact, Structure::Destroy)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, EnTT, Structure::Destroy)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, FlecsUncached, Structure::Destroy)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, IkigaiCompact, Structure::AddRemove)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, EnTT, Structure::AddRemove)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, FlecsUncached, Structure::AddRemove)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, IkigaiCompact, Structure::Add)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, EnTT, Structure::Add)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, FlecsUncached, Structure::Add)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, IkigaiCompact, Structure::Remove)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, EnTT, Structure::Remove)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, FlecsUncached, Structure::Remove)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, IkigaiNoHandle, Structure::Add)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, IkigaiWithListeners, Structure::Add)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(structural, IkigaiWithListeners, Structure::Remove)->Apply(entitySizes);
	BENCHMARK_TEMPLATE(addTransition, false)->Name("Ikigai/AddTransition/Cold")->UseRealTime();
	BENCHMARK_TEMPLATE(addTransition, true)->Name("Ikigai/AddTransition/Warm")->UseRealTime();
	BENCHMARK_TEMPLATE(entityRegistry, false)->Name("Ikigai/EntityRegistry/GenerateIds")->UseRealTime()->Arg(100000);
	BENCHMARK_TEMPLATE(entityRegistry, true)->Name("Ikigai/EntityRegistry/RegisterKnownIds")->UseRealTime()->Arg(100000);
	BENCHMARK_TEMPLATE(entityRegistry, true, true)->Name("Ikigai/EntityRegistry/RegisterSparseIds")->UseRealTime()->Arg(1000);
	template <ArchetypeLayout Layout, std::size_t Columns> void registerGrowth(const char* layout) {
		const auto prefix = std::string("Ikigai/StorageGrowth/") + layout + "/" + std::to_string(Columns);
		benchmark::RegisterBenchmark(prefix + "/Grow", storageGrowth<Columns, false, Layout>)->UseRealTime()->Arg(100000);
		benchmark::RegisterBenchmark(prefix + "/Reuse", storageGrowth<Columns, true, Layout>)->UseRealTime()->Arg(100000);
		if constexpr (Layout == ArchetypeLayout::SharedRows) {
			benchmark::RegisterBenchmark(prefix + "/TargetLookup", storageGrowth<Columns, false, Layout, TransitionProbe::TargetLookup>)
				->UseRealTime()
				->Arg(100000);
			benchmark::RegisterBenchmark(prefix + "/MovePrepared", storageGrowth<Columns, true, Layout, TransitionProbe::MovePrepared>)
				->UseRealTime()
				->Arg(100000);
		}
	}

	const bool growthRegistered = [] {
		registerGrowth<ArchetypeLayout::PerColumn, 1>("PerColumn");
		registerGrowth<ArchetypeLayout::PerColumn, 4>("PerColumn");
		registerGrowth<ArchetypeLayout::PerColumn, 8>("PerColumn");
		registerGrowth<ArchetypeLayout::SharedRows, 1>("SharedRows");
		registerGrowth<ArchetypeLayout::SharedRows, 4>("SharedRows");
		registerGrowth<ArchetypeLayout::SharedRows, 8>("SharedRows");
		return true;
	}();

	BENCHMARK_TEMPLATE(commands, true)->Name("CommandBuffer/RecordAdd")->Apply(entitySizes);
	BENCHMARK_TEMPLATE(commands, false)->Name("CommandBuffer/PlaybackAdd")->Apply(entitySizes);

}
}
