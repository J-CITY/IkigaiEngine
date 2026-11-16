#include "backends.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace ECS_BENCHMARK {
namespace {

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

	template <class Backend> void movement(benchmark::State& state) {
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
		for (auto _ : state) {
			move();
			benchmark::ClobberMemory();
		}
		const auto iterations = static_cast<float>(state.iterations());
		for (std::size_t index = 0; index < count; ++index) {
			const auto* position = backend.template get<PositionType>(entities[index]);
			const auto initial = static_cast<float>(index % 29);
			const auto updates = index % stride == 0 ? iterations : 0.0f;
			if (!position || position->x != initial + updates * 0.25f || position->y != -initial - updates * 0.125f) {
				state.SkipWithError("movement updated the wrong entity or value");
				return;
			}
		}
		state.SetItemsProcessed(state.iterations() * expectedMatches);
		state.counters["position_bytes"] = sizeof(PositionType);
		state.counters["velocity_bytes"] = sizeof(VelocityType);
	}

	enum class Structure { Create, Destroy, AddRemove };

	template <class Backend, Structure Operation> void structural(benchmark::State& state) {
		const auto count = static_cast<std::size_t>(state.range(0));
		using Entity = typename Backend::Entity;
		std::vector<Entity> entities(count);
		for (auto _ : state) {
			state.PauseTiming();
			auto backend = std::make_unique<Backend>();
			if constexpr (Operation != Structure::Create) {
				for (auto& entity : entities) {
					entity = backend->create();
					if constexpr (Operation == Structure::AddRemove) {
						backend->add(entity, typename Backend::PositionType {});
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
				else {
					backend->add(entity, Marker { 7 });
					backend->template remove<Marker>(entity);
				}
			}
			benchmark::ClobberMemory();

			state.PauseTiming();
			const auto valid = std::ranges::all_of(entities, [&](Entity entity) {
				if constexpr (Operation == Structure::AddRemove) {
					return backend->template get<typename Backend::PositionType>(entity) && !backend->template get<Marker>(entity);
				}
				else {
					return backend->alive(entity) == (Operation == Structure::Create);
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

	BENCHMARK_TEMPLATE(commands, true)->Name("CommandBuffer/RecordAdd")->Apply(entitySizes);
	BENCHMARK_TEMPLATE(commands, false)->Name("CommandBuffer/PlaybackAdd")->Apply(entitySizes);

}
}
