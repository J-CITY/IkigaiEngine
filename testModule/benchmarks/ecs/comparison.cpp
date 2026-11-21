#include "backends.h"
#include "registryFixture.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <random>
#include <ranges>
#include <string>
#include <vector>

namespace IKIGAI::ECS {
struct EcsBenchmarkAccess {
	enum class ScatterPolicy { Source, SourceRowsBitmap, Destination };

	template <class T>
	static std::array<double, 3> profileScatter(ComponentManager& manager, std::span<const Entity> entities, const T& value, ScatterPolicy policy) {
		using Clock = std::chrono::steady_clock;
		const auto begin = Clock::now();
		const auto batch = manager.prepareIndexedBatch(entities, manager.getComponentType<T>());
		if (!batch) {
			throw std::logic_error("Scatter benchmark must use the indexed batch planner");
		}
		using Order = ComponentManager::ScatterOrder;
		const auto order = policy == ScatterPolicy::Destination ? Order::Destination
			: policy == ScatterPolicy::SourceRowsBitmap			? Order::SourceRowsBitmap
																: Order::Source;
		auto scatter = manager.prepareIndexedScatter(*batch, UTILS::ChunkList<T>::chunk_size, order);
		if (!scatter) {
			throw std::logic_error("Scatter benchmark must use grouped rows");
		}
		const auto planned = Clock::now();
		manager.appendIndexedScatterValues(*batch, value);
		manager.appendIndexedScatterRows(*batch, *scatter);
		const auto appended = Clock::now();
		manager.compactIndexedScatter(*batch, *scatter);
		const auto end = Clock::now();
		using Nanoseconds = std::chrono::duration<double, std::nano>;
		return { Nanoseconds(planned - begin).count(), Nanoseconds(appended - planned).count(), Nanoseconds(end - appended).count() };
	}

	template <class T> static void addBatchByRow(ComponentManager& manager, std::span<const Entity> entities, const T& value) {
		manager.addArchetypeComponentBatchImpl<false>(entities, value);
	}

	template <class T, class Func> static void visitPoolChunks(ComponentArray<T>& pool, Func&& func) {
		ComponentManager::visitPoolChunks(pool, std::forward<Func>(func));
	}

	template <class T> static const SparseRowIndex& sparseIndex(const ComponentArray<T>& pool) {
		return *pool.mSparseRows;
	}

	template <class T> static const auto& sparseValues(const ComponentArray<T>& pool) {
		return pool.componentArray;
	}

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

	template <bool WithMultipleSignatures = true> void movementSizes(benchmark::internal::Benchmark* benchmark) {
		benchmark->UseRealTime();
		for (const auto count : entityCounts) {
			benchmark->Args({ count, 100, 1 });
			benchmark->Args({ count, 10, 1 });
			benchmark->Args({ count, 50, 1 });
			if constexpr (WithMultipleSignatures) {
				benchmark->Args({ count, 100, 16 });
			}
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

	template <class Backend> auto populateMovement(Backend& backend, std::size_t count, std::size_t stride, std::size_t signatures) {
		using PositionType = typename Backend::PositionType;
		using VelocityType = typename Backend::VelocityType;
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
		return entities;
	}

	template <class Backend, bool LookupOnly = false> void movement(benchmark::State& state) {
		using PositionType = typename Backend::PositionType;
		using VelocityType = typename Backend::VelocityType;
		const auto count = static_cast<std::size_t>(state.range(0));
		const auto stride = 100 / static_cast<std::size_t>(state.range(1));
		const auto signatures = static_cast<std::size_t>(state.range(2));
		Backend backend;
		const auto entities = populateMovement(backend, count, stride, signatures);

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
		using Backend = Ikigai<alignof(float), MovementStorage::Archetype, Layout>;
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
	using IkigaiMixed = Ikigai<alignof(float), MovementStorage::Mixed>;
	using IkigaiSparse = Ikigai<alignof(float), MovementStorage::Sparse>;
	using IkigaiAligned = Ikigai<16>;
	using FlecsUncached = Flecs<>;
	using FlecsCached = Flecs<flecs::QueryCacheAuto>;

	enum class SparseProbe { LeadingRows, LeadingChunks, RowLookup, RowLookupAndRead };

	template <class Checksum, class Probe>
	void measureSparseProbe(benchmark::State& state, std::size_t candidates, const Checksum& expected, Probe&& probe) {
		if (probe() != expected) {
			state.SkipWithError("sparse diagnostic did not visit the expected rows");
			return;
		}
		for (auto _ : state) {
			const auto checksum = probe();
			benchmark::DoNotOptimize(checksum);
			benchmark::ClobberMemory();
		}
		state.SetItemsProcessed(state.iterations() * candidates);
	}

	template <SparseProbe Probe, bool PositionLeads, class Backend>
	void measureSparseMovement(benchmark::State& state, Backend& backend, std::span<const typename Backend::Entity> entities, std::size_t stride) {
		using Access = IKIGAI::ECS::EcsBenchmarkAccess;
		using Position = typename Backend::PositionType;
		using Velocity = typename Backend::VelocityType;
		using Leading = std::conditional_t<PositionLeads, Position, Velocity>;
		using Other = std::conditional_t<PositionLeads, Velocity, Position>;
		using Entity = typename Backend::Entity;
		const auto candidates = (entities.size() + stride - 1) / stride;
		const auto entityId = [](Entity entity) -> std::uint32_t {
			if constexpr (std::is_same_v<Backend, EnTT>) {
				return entt::to_integral(entity);
			}
			else {
				return entity.getUniqueId().value();
			}
		};
		if constexpr (Probe == SparseProbe::RowLookup || Probe == SparseProbe::RowLookupAndRead) {
			struct Checksum {
				std::uint64_t rows = 0;
				std::size_t hits = 0;
				float values = 0.0f;

				bool operator==(const Checksum&) const = default;
			};
			constexpr auto missing = IKIGAI::ECS::SparseRowIndex::missing;
			Checksum expected { static_cast<std::uint64_t>(candidates) * (candidates - 1) / 2 * stride, candidates };
			if constexpr (Probe == SparseProbe::RowLookupAndRead) {
				for (std::size_t row = 0; row < entities.size(); row += stride) {
					expected.values += PositionLeads ? 0.5f : static_cast<float>(row % 29);
				}
			}
			const auto measureLookup = [&](auto ids, auto findRow, auto readValue) {
				measureSparseProbe(state, candidates, expected, [&] {
					Checksum result {};
					for (const auto entity : ids) {
						const auto row = findRow(entity);
						if (row != missing) {
							result.rows += row;
							++result.hits;
							if constexpr (Probe == SparseProbe::RowLookupAndRead) {
								result.values += readValue(row);
							}
						}
					}
					return result;
				});
			};
			if constexpr (std::is_same_v<Backend, EnTT>) {
				const auto& leading = backend.template storage<Leading>();
				const auto& other = backend.template storage<Other>();
				const auto ids = std::span(leading.data(), leading.size());
				const auto values = other.raw();
				constexpr auto pageSize = entt::component_traits<Other, Entity>::page_size;
				measureLookup(
					ids | std::views::reverse, [&](Entity entity) { return other.contains(entity) ? other.index(entity) : missing; },
					[&](std::size_t row) { return values[row / pageSize][row % pageSize].x; });
			}
			else {
				const auto ids = backend.components.template getComponentsArray<Leading>().getEntities();
				const auto& other = backend.components.template getComponentsArray<Other>();
				const auto& index = Access::sparseIndex(other);
				const auto& values = Access::sparseValues(other);
				measureLookup(ids, [&](Entity entity) { return index.find(entityId(entity)); }, [&](std::size_t row) { return values[row].x; });
			}
		}
		else {
			using Checksum = std::pair<std::uint64_t, float>;
			Checksum expected {};
			for (std::size_t row = 0; row < entities.size(); row += stride) {
				expected.first += entityId(entities[row]);
				expected.second += PositionLeads ? static_cast<float>(row % 29) : 0.5f;
			}
			auto& leading = [&]() -> auto& {
				if constexpr (std::is_same_v<Backend, EnTT>) {
					return backend.template storage<Leading>();
				}
				else {
					return backend.components.template getComponentsArray<Leading>();
				}
			}();
			measureSparseProbe(state, candidates, expected, [&] {
				Checksum result {};
				const auto accumulate = [&](Entity entity, const Leading& value) {
					result.first += entityId(entity);
					result.second += value.x;
				};
				if constexpr (std::is_same_v<Backend, EnTT>) {
					for (const auto [entity, value] : leading.each()) {
						accumulate(entity, value);
					}
				}
				else {
					if constexpr (Probe == SparseProbe::LeadingChunks) {
						Access::visitPoolChunks(leading, accumulate);
					}
					else {
						const auto ids = leading.getEntities();
						for (std::size_t row = 0; row < ids.size(); ++row) {
							accumulate(ids[row], leading.at(row));
						}
					}
				}
				return result;
			});
		}
	}

	template <class Backend, SparseProbe Probe> void sparseMovementProbe(benchmark::State& state) {
		const auto count = static_cast<std::size_t>(state.range(0));
		const auto stride = 100 / static_cast<std::size_t>(state.range(1));
		Backend backend;
		const auto entities = populateMovement(backend, count, stride, 1);
		if (stride == 1) {
			measureSparseMovement<Probe, true>(state, backend, std::span(entities), stride);
		}
		else {
			measureSparseMovement<Probe, false>(state, backend, std::span(entities), stride);
		}
	}

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

	enum class BatchDispatch { Single, ReservedRows, Range, ProfileSource, ProfileSourceRowsBitmap, ProfileDestination };
	enum class BatchSelection { Full, Slice, Strided, Shuffled };

	template <class Backend, std::size_t Columns, BatchDispatch Dispatch, BatchSelection Selection = BatchSelection::Full>
	void batchAddition(benchmark::State& state) {
		using Entity = typename Backend::Entity;
		constexpr bool scattered = Selection == BatchSelection::Strided || Selection == BatchSelection::Shuffled;
		constexpr bool profiled = Dispatch == BatchDispatch::ProfileSource || Dispatch == BatchDispatch::ProfileSourceRowsBitmap
			|| Dispatch == BatchDispatch::ProfileDestination;
		const auto size = static_cast<std::size_t>(state.range(0));
		const auto stride = scattered ? 100 / static_cast<std::size_t>(state.range(1)) : 1;
		const auto count = (size + stride - 1) / stride;
		const auto first = Selection == BatchSelection::Slice ? count / 2 : 0;
		std::vector<Entity> entities(Selection == BatchSelection::Slice ? 2 * count : size);
		std::vector<std::size_t> selectedIndices;
		std::vector<Entity> selectedEntities;
		std::array<double, 3> phaseTimes {};
		if constexpr (scattered) {
			selectedIndices.reserve(count);
			selectedEntities.resize(count);
			for (std::size_t index = 0; index < size; index += stride) {
				selectedIndices.push_back(index);
			}
			if constexpr (Selection == BatchSelection::Shuffled) {
				std::mt19937 generator(407);
				std::shuffle(selectedIndices.begin(), selectedIndices.end(), generator);
			}
		}
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
			for (std::size_t index = 0; index < entities.size(); ++index) {
				entities[index] = createSource(index);
			}
			const auto selected = [&]() -> std::span<const Entity> {
				if constexpr (scattered) {
					for (std::size_t index = 0; index < count; ++index) {
						selectedEntities[index] = entities[selectedIndices[index]];
					}
					return selectedEntities;
				}
				else {
					return std::span(entities).subspan(first, count);
				}
			}();
			state.ResumeTiming();
			if constexpr (profiled) {
				using Access = IKIGAI::ECS::EcsBenchmarkAccess;
				constexpr auto policy = Dispatch == BatchDispatch::ProfileDestination ? Access::ScatterPolicy::Destination
					: Dispatch == BatchDispatch::ProfileSourceRowsBitmap			  ? Access::ScatterPolicy::SourceRowsBitmap
																					  : Access::ScatterPolicy::Source;
				const auto phases = Access::profileScatter(backend->components, selected, Marker { 7 }, policy);
				for (std::size_t index = 0; index < phases.size(); ++index) {
					phaseTimes[index] += phases[index];
				}
			}
			else if constexpr (Dispatch == BatchDispatch::ReservedRows) {
				IKIGAI::ECS::EcsBenchmarkAccess::addBatchByRow(
					backend->components, std::span<const typename Backend::Entity>(selected), Marker { 7 });
			}
			else if constexpr (Dispatch == BatchDispatch::Range && std::is_same_v<Backend, IkigaiCompact>) {
				backend->components.addArchetypeComponentBatch(selected, Marker { 7 });
			}
			else if constexpr (Dispatch == BatchDispatch::Range) {
				backend->addBatch(std::span<const typename Backend::Entity>(selected), Marker { 7 });
			}
			else {
				for (const auto entity : selected) {
					backend->add(entity, Marker { 7 });
				}
			}
			benchmark::ClobberMemory();
			state.PauseTiming();
			bool valid = true;
			for (std::size_t index = 0; index < entities.size() && valid; ++index) {
				const auto entity = entities[index];
				const auto* marker = backend->template get<Marker>(entity);
				const auto retained = [&]<std::size_t I> {
					const auto* component = backend->template get<RetainedComponent<I>>(entity);
					return component && component->value == static_cast<int>(index + I);
				};
				const bool expectedMarker = scattered ? index % stride == 0 : index >= first && index - first < count;
				const bool correctMarker = expectedMarker ? marker && marker->value == 7 : marker == nullptr;
				valid = correctMarker && [&]<std::size_t... Index>(std::index_sequence<Index...>) {
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
		if constexpr (profiled) {
			const auto operations = static_cast<double>(state.iterations() * count);
			state.counters["plan_ns"] = phaseTimes[0] / operations;
			state.counters["target_ns"] = phaseTimes[1] / operations;
			state.counters["compact_ns"] = phaseTimes[2] / operations;
		}
	}

	template <std::size_t Columns, BatchSelection Selection> void registerScatteredAddition(const char* order) {
		const auto add = [&](const char* name, void (*run)(benchmark::State&)) {
			const auto label = std::string("ArchetypeBatchScatter/") + order + "/" + name + "/" + std::to_string(Columns);
			benchmark::RegisterBenchmark(label, run)->UseRealTime()->Args({ 100000, 10 })->Args({ 100000, 50 });
		};
		add("Ikigai/ReservedRows", batchAddition<IkigaiCompact, Columns, BatchDispatch::ReservedRows, Selection>);
		add("Ikigai/Grouped", batchAddition<IkigaiCompact, Columns, BatchDispatch::Range, Selection>);
		add("EnTT/ReservedRange", batchAddition<EnTT, Columns, BatchDispatch::Range, Selection>);
		add("Flecs/Single", batchAddition<FlecsUncached, Columns, BatchDispatch::Single, Selection>);
	}

	template <std::size_t Columns> void registerBatchAddition() {
		const auto add = [](const char* name, void (*run)(benchmark::State&)) {
			const auto label = std::string("ArchetypeBatch/") + name + "/" + std::to_string(Columns);
			benchmark::RegisterBenchmark(label, run)->UseRealTime()->Arg(1000)->Arg(100000);
		};
		add("Ikigai/Single", batchAddition<IkigaiCompact, Columns, BatchDispatch::Single>);
		add("Ikigai/SingleNoHandle", batchAddition<IkigaiNoHandle, Columns, BatchDispatch::Single>);
		add("Ikigai/ReservedRows", batchAddition<IkigaiCompact, Columns, BatchDispatch::ReservedRows>);
		add("Ikigai/Chunked", batchAddition<IkigaiCompact, Columns, BatchDispatch::Range>);
		add("EnTT/Single", batchAddition<EnTT, Columns, BatchDispatch::Single>);
		add("EnTT/ReservedRange", batchAddition<EnTT, Columns, BatchDispatch::Range>);
		add("Flecs/Single", batchAddition<FlecsUncached, Columns, BatchDispatch::Single>);
		if constexpr (Columns > 1) {
			const auto addSlice = [](const char* name, void (*run)(benchmark::State&)) {
				const auto label = std::string("ArchetypeBatchSlice/") + name + "/" + std::to_string(Columns);
				benchmark::RegisterBenchmark(label, run)->UseRealTime()->Arg(100000);
			};
			addSlice("Ikigai/ReservedRows", batchAddition<IkigaiCompact, Columns, BatchDispatch::ReservedRows, BatchSelection::Slice>);
			addSlice("Ikigai/Chunked", batchAddition<IkigaiCompact, Columns, BatchDispatch::Range, BatchSelection::Slice>);
			addSlice("EnTT/ReservedRange", batchAddition<EnTT, Columns, BatchDispatch::Range, BatchSelection::Slice>);
			addSlice("Flecs/Single", batchAddition<FlecsUncached, Columns, BatchDispatch::Single, BatchSelection::Slice>);
			registerScatteredAddition<Columns, BatchSelection::Strided>("Strided");
			registerScatteredAddition<Columns, BatchSelection::Shuffled>("Shuffled");
		}
	}

	const bool batchRegistered = [] {
		registerBatchAddition<1>();
		registerBatchAddition<4>();
		registerBatchAddition<8>();
		const auto focus = [](const char* name, void (*run)(benchmark::State&)) {
			benchmark::RegisterBenchmark(std::string("ArchetypeScatterFocus/") + name, run)->UseRealTime()->Args({ 100000, 10 });
		};
		focus("Ikigai/SourceChunks", batchAddition<IkigaiCompact, 8, BatchDispatch::ProfileSource, BatchSelection::Shuffled>);
		focus("Ikigai/SourceRowsBitmap", batchAddition<IkigaiCompact, 8, BatchDispatch::ProfileSourceRowsBitmap, BatchSelection::Shuffled>);
		focus("Ikigai/TargetChunksBitmap", batchAddition<IkigaiCompact, 8, BatchDispatch::ProfileDestination, BatchSelection::Shuffled>);
		focus("Ikigai/Adaptive", batchAddition<IkigaiCompact, 8, BatchDispatch::Range, BatchSelection::Shuffled>);
		focus("EnTT/ReservedRange", batchAddition<EnTT, 8, BatchDispatch::Range, BatchSelection::Shuffled>);
		focus("Flecs/Single", batchAddition<FlecsUncached, 8, BatchDispatch::Single, BatchSelection::Shuffled>);
		return true;
	}();

	BENCHMARK_TEMPLATE(movement, IkigaiCompact)->Apply(movementSizes<>);
	BENCHMARK_TEMPLATE(movement, IkigaiSparse)->Apply(movementSizes<false>);
	BENCHMARK_TEMPLATE(movement, EnTT)->Apply(movementSizes<>);
	BENCHMARK_TEMPLATE(movement, FlecsUncached)->Apply(movementSizes<>);
	BENCHMARK_TEMPLATE(movement, FlecsCached)->Apply(movementSizes<>);
	const auto sparseProbes = [] {
		const auto add = [](const char* name, void (*run)(benchmark::State&)) {
			benchmark::RegisterBenchmark(name, run)->UseRealTime()->Args({ 100000, 10 })->Args({ 100000, 50 })->Args({ 100000, 100 });
		};
		add("SparseProbe/Ikigai/LeadingRows", sparseMovementProbe<IkigaiSparse, SparseProbe::LeadingRows>);
		add("SparseProbe/Ikigai/LeadingChunks", sparseMovementProbe<IkigaiSparse, SparseProbe::LeadingChunks>);
		add("SparseProbe/Ikigai/RowLookup", sparseMovementProbe<IkigaiSparse, SparseProbe::RowLookup>);
		add("SparseProbe/Ikigai/RowLookupAndRead", sparseMovementProbe<IkigaiSparse, SparseProbe::RowLookupAndRead>);
		add("SparseProbe/EnTT/LeadingStorage", sparseMovementProbe<EnTT, SparseProbe::LeadingChunks>);
		add("SparseProbe/EnTT/RowLookup", sparseMovementProbe<EnTT, SparseProbe::RowLookup>);
		add("SparseProbe/EnTT/RowLookupAndRead", sparseMovementProbe<EnTT, SparseProbe::RowLookupAndRead>);
		return true;
	}();
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
