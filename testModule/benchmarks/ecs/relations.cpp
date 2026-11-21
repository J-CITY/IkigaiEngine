#include "ecsModule/componentManager.h"
#include "ecsModule/world.h"

#include <benchmark/benchmark.h>
#include <entt/entity/registry.hpp>
#include <flecs.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {
struct Follows { };

class IkigaiRelations {
public:
	using Entity = IKIGAI::ECS::Entity;

	Entity create() {
		return world.createEntity();
	}
	void add(Entity source, Entity target) {
		relations.addRelation(source, follows, target);
	}
	template <class Visitor> void visit(Entity source, const Visitor& visitor) const {
		relations.forEachTarget(source, follows, visitor);
	}
	void destroy(Entity target) {
		world.destroyEntity(target);
	}
	bool alive(Entity entity) const {
		return components.resolveEntity(components.getEntityHandle(entity)).has_value();
	}
	static std::uint64_t id(Entity entity) {
		return entity.getUniqueId().value();
	}

private:
	IKIGAI::ECS::World world { IKIGAI::ECS::ArchetypeLayout::SharedRows };
	IKIGAI::ECS::ComponentManager& components = *world.getComponentManager();
	IKIGAI::ECS::RelationManager& relations = components.getRelationManager();
	const IKIGAI::ECS::ComponentType follows = components.getComponentType<Follows>();
};

class EnTTRelations {
	struct Targets {
		std::vector<entt::entity> entities;
	};

public:
	using Entity = entt::entity;

	EnTTRelations() {
		registry.storage<Targets>();
	}
	Entity create() {
		return registry.create();
	}
	void add(Entity source, Entity target) {
		auto& targets = registry.get_or_emplace<Targets>(source).entities;
		if (std::ranges::find(targets, target) == targets.end()) {
			targets.push_back(target);
		}
	}
	template <class Visitor> void visit(Entity source, const Visitor& visitor) const {
		if (const auto* targets = registry.try_get<Targets>(source)) {
			for (const auto target : targets->entities) {
				visitor(target);
			}
		}
	}
	void destroy(Entity target) {
		registry.view<Targets>().each([target](Targets& targets) { std::erase(targets.entities, target); });
		registry.destroy(target);
	}
	bool alive(Entity entity) const {
		return registry.valid(entity);
	}
	static std::uint64_t id(Entity entity) {
		return entt::to_integral(entity);
	}

private:
	entt::registry registry;
};

class FlecsRelations {
public:
	using Entity = flecs::entity;

	FlecsRelations() {
		follows.add(flecs::OnDeleteTarget, flecs::Remove);
	}
	Entity create() {
		return world.entity();
	}
	void add(Entity source, Entity target) {
		source.add(follows, target);
	}
	template <class Visitor> void visit(Entity source, const Visitor& visitor) const {
		source.each(follows, visitor);
	}
	void destroy(Entity target) {
		target.destruct();
	}
	bool alive(Entity entity) const {
		return entity.is_alive();
	}
	static std::uint64_t id(Entity entity) {
		return entity.id();
	}

private:
	flecs::world world;
	flecs::entity follows = world.component<Follows>();
};

constexpr std::size_t sharedTargetCount = 3;
constexpr std::size_t targetsPerSource = sharedTargetCount + 1;

template <class Backend> class RelationGraph {
public:
	using Entity = typename Backend::Entity;

	RelationGraph(std::size_t sourceCount, std::size_t incoming)
		: groupCount(sourceCount / incoming) {
		targets.reserve(sharedTargetCount + groupCount);
		for (std::size_t index = 0; index < sharedTargetCount + groupCount; ++index) {
			targets.push_back(backend.create());
		}
		sources.reserve(sourceCount);
		for (std::size_t index = 0; index < sourceCount; ++index) {
			sources.push_back(backend.create());
		}
	}

	void addEdges() {
		for (std::size_t index = 0; index < sources.size(); ++index) {
			for (const auto target : expectedTargets(index)) {
				backend.add(sources[index], target);
			}
		}
	}

	std::uint64_t visitEdges() const {
		std::uint64_t checksum = 0;
		for (const auto source : sources) {
			backend.visit(source, [&](Entity target) { checksum += Backend::id(target); });
		}
		return checksum;
	}

	std::uint64_t expectedChecksum() const {
		std::uint64_t checksum = 0;
		for (std::size_t index = 0; index < sources.size(); ++index) {
			for (const auto target : expectedTargets(index)) {
				checksum += Backend::id(target);
			}
		}
		return checksum;
	}

	void destroyTarget() {
		backend.destroy(targets[sharedTargetCount]);
	}

	bool validate(bool targetDeleted) const {
		for (std::size_t index = 0; index < targets.size(); ++index) {
			if (backend.alive(targets[index]) != !(targetDeleted && index == sharedTargetCount)) {
				return false;
			}
		}
		for (std::size_t index = 0; index < sources.size(); ++index) {
			if (!backend.alive(sources[index])) {
				return false;
			}
			auto expected = expectedTargets(index);
			const auto expectedCount = targetDeleted && index % groupCount == 0 ? sharedTargetCount : targetsPerSource;
			const auto end = expected.begin() + expectedCount;
			auto remaining = end;
			bool valid = true;
			backend.visit(sources[index], [&](Entity target) {
				const auto found = std::find(expected.begin(), remaining, target);
				if (found == remaining) {
					valid = false;
					return;
				}
				*found = *--remaining;
			});
			if (!valid || remaining != expected.begin()) {
				return false;
			}
		}
		return true;
	}

private:
	std::array<Entity, targetsPerSource> expectedTargets(std::size_t index) const {
		return { targets[0], targets[1], targets[2], targets[sharedTargetCount + index % groupCount] };
	}

	Backend backend;
	const std::size_t groupCount = 0;
	std::vector<Entity> sources;
	std::vector<Entity> targets;
};

enum class Operation { Add, VisitTargets, DestroyTarget };

template <class Backend, Operation Op> void relationBenchmark(benchmark::State& state) {
	const auto sources = static_cast<std::size_t>(state.range(0));
	const auto incoming = static_cast<std::size_t>(state.range(1));
	const auto edges = sources * targetsPerSource;
	if constexpr (Op == Operation::VisitTargets) {
		RelationGraph<Backend> graph(sources, incoming);
		graph.addEdges();
		if (!graph.validate(false)) {
			state.SkipWithError("target traversal does not match the fixture");
			return;
		}
		const auto expected = graph.expectedChecksum();
		std::uint64_t checksum = 0;
		for (auto _ : state) {
			checksum = graph.visitEdges();
			benchmark::DoNotOptimize(checksum);
		}
		if (checksum != expected) {
			state.SkipWithError("target traversal checksum differs");
		}
	}
	else {
		for (auto _ : state) {
			state.PauseTiming();
			auto graph = std::make_unique<RelationGraph<Backend>>(sources, incoming);
			if constexpr (Op == Operation::DestroyTarget) {
				graph->addEdges();
			}
			state.ResumeTiming();
			if constexpr (Op == Operation::Add) {
				graph->addEdges();
			}
			else {
				graph->destroyTarget();
			}
			benchmark::ClobberMemory();
			state.PauseTiming();
			const bool valid = graph->validate(Op == Operation::DestroyTarget);
			graph.reset();
			state.ResumeTiming();
			if (!valid) {
				state.SkipWithError("graph differs after relation mutation");
				return;
			}
		}
	}
	state.SetItemsProcessed(state.iterations() * (Op == Operation::DestroyTarget ? 1 : edges));
	state.counters["edges"] = static_cast<double>(edges);
}

template <class Backend> bool registerRelations(const char* backend) {
	const auto add = [&](const char* operation, void (*run)(benchmark::State&)) {
		return benchmark::RegisterBenchmark(std::string("Relations/") + backend + "/" + operation, run)
			->UseRealTime()
			->ArgNames({ "sources", "incoming" });
	};
	add("Add", relationBenchmark<Backend, Operation::Add>)->Args({ 16384, 32 });
	add("VisitTargets", relationBenchmark<Backend, Operation::VisitTargets>)->Args({ 16384, 32 });
	add("DestroyTarget", relationBenchmark<Backend, Operation::DestroyTarget>)
		->Args({ 1024, 32 })
		->Args({ 16384, 32 })
		->Args({ 16384, 512 })
		->Iterations(16);
	return true;
}

const bool ikigaiRegistered = registerRelations<IkigaiRelations>("Ikigai");
const bool enttRegistered = registerRelations<EnTTRelations>("EnTT");
const bool flecsRegistered = registerRelations<FlecsRelations>("Flecs");
}
