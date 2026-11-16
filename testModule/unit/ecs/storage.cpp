#include "components.h"
#include "ecsModule/world.h"
#include "support/expect.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace IKIGAI::TEST::ECS_CASES {

ThrowingValue::ThrowingValue(ThrowingValue&& other)
	: failOnMove(other.failOnMove) {
	if (failOnMove) {
		throw std::runtime_error("test component move failed");
	}
}
namespace {
	using namespace IKIGAI::ECS;

	void checkHandles(ArchetypeLayout layout) {
		World world { layout };
		World otherWorld { layout };
		auto& components = *world.getComponentManager();
		const auto entity = world.createEntity();
		const Entity loaded { Entity::ID { entity.getUniqueId().value() + 4096 } };
		world.registerEntity(loaded);
		expect(world.createEntity().getUniqueId().value() > loaded.getUniqueId().value(), "generated IDs advance past imported IDs");
		expectThrows([&] { world.registerEntity(Entity {}); }, "invalid IDs cannot be registered");
		otherWorld.registerEntity(entity);
		const auto entityHandle = components.getEntityHandle(entity);
		expectThrows([&] { world.registerEntity(entity); }, "duplicate IDs cannot be registered in one world");
		const auto position = components.addComponent(entity, Position { 10 });
		expect(components.resolveEntity(entityHandle) == entity, "live entity resolves");
		expect(!otherWorld.getComponentManager()->resolveEntity(entityHandle), "foreign entity handle is rejected");
		expect(otherWorld.getComponentManager()->resolveComponent(position) == nullptr, "foreign component handle is rejected");

		components.addComponent(entity, Velocity { 20 });
		const auto* migrated = components.resolveComponent(position);
		expect(migrated && migrated->value == 10, "component handle survives an archetype transition");
		const auto replacement = components.addComponent(entity, Position { 30 });
		const auto* replaced = components.resolveComponent(replacement);
		expect(replaced && replaced->value == 30, "replacement holds the new value");
		expect(components.resolveComponent(position) == nullptr, "replacement invalidates the original component handle");

		components.removeComponents<Position>(entity);
		expect(components.resolveComponent(replacement) == nullptr, "removed component cannot be resolved");
		world.destroyEntity(entity);
		world.destroyEntity(entity);
		world.registerEntity(entity);
		expect(!components.resolveEntity(entityHandle), "old handle is stale after registering the same ID again");
		expect(components.resolveEntity(components.getEntityHandle(entity)) == entity, "new registration resolves");
	}

	void checkImportedIds(ArchetypeLayout layout) {
		World world { layout };
		auto& components = *world.getComponentManager();
		const Entity zero { Entity::ID { 0 } };
		const Entity low { Entity::ID { 255 } };
		const Entity nextPage { Entity::ID { 256 } };
		const Entity high { Entity::ID { 0x70000000u } };
		const Entity highNeighbor { Entity::ID { 0x70000001u } };
		const Entity absent { Entity::ID { 0x70000002u } };
		for (const auto entity : { high, zero, nextPage, low, highNeighbor }) {
			world.registerEntity(entity);
		}
		expect(!components.resolveEntity(components.getEntityHandle(absent)), "unregistered neighbor of a sparse ID remains absent");
		world.destroyEntity(absent);
		const auto highHandle = components.addComponent(high, Position { 70 });
		const auto lowHandle = components.addComponent(low, Position { 25 });
		const auto neighborHandle = components.getEntityHandle(highNeighbor);
		world.destroyEntity(high);
		expect(!components.resolveComponent(highHandle), "destroying a sparse loaded ID invalidates its component handle");
		expect(components.resolveEntity(neighborHandle) == highNeighbor, "removing a loaded ID preserves its neighbor");
		const auto* position = components.resolveComponent(lowHandle);
		expect(position && position->value == 25, "swap-removal across ID pages preserves the relocated component");
		world.destroyEntity(highNeighbor);
		world.registerEntity(highNeighbor);
		expect(!components.resolveEntity(neighborHandle), "registering a loaded ID again does not revive stale handles");
		expect(components.resolveEntity(components.getEntityHandle(highNeighbor)) == highNeighbor, "a deleted loaded ID can be registered again");
		expect(components.resolveEntity(components.getEntityHandle(zero)) == zero, "loaded zero is distinct from an unoccupied slot");
		expect(components.resolveEntity(components.getEntityHandle(nextPage)) == nextPage, "IDs on opposite sides of a page boundary are distinct");
		expect(world.createEntity().getUniqueId().value() > absent.getUniqueId().value(), "generation continues beyond sparse loaded IDs");
	}

	void checkHybridRegistry(ArchetypeLayout layout) {
		World world { layout };
		auto& components = *world.getComponentManager();
		constexpr std::uint32_t firstId = 0x04020000u;
		std::array<Entity, 12> entities {};
		for (std::uint32_t index = 0; index < entities.size(); ++index) {
			entities[index] = Entity { Entity::ID { firstId + index } };
			world.registerEntity(entities[index]);
		}
		const auto compact = entities.front();
		const auto dense = entities.back();
		expectThrows([&] { world.registerEntity(compact); }, "records remain unique when dense and compact storage share an ID page");
		expectThrows([&] { world.registerEntity(dense); }, "dense records remain unique after supplied IDs switch to pages");
		const auto compactHandle = components.addComponent(compact, Position { 1 });
		const auto denseHandle = components.addComponent(dense, Position { 12 });
		world.destroyEntity(compact);
		const auto* position = components.resolveComponent(denseHandle);
		expect(position && position->value == 12, "swap-removal from compact storage updates the dense registration");
		world.registerEntity(compact);
		expect(!components.resolveComponent(compactHandle), "reusing a compact ID in dense storage invalidates the old component handle");
		const auto survivingCompact = components.getEntityHandle(entities[1]);
		for (const auto entity : { compact, entities[7], entities[8], entities[9], entities[10], dense }) {
			world.destroyEntity(entity);
		}
		expect(components.resolveEntity(survivingCompact) == entities[1], "releasing a dense page preserves compact registrations in its range");
	}

	void checkComponentEvents(ArchetypeLayout layout) {
		World world { layout };
		auto& components = *world.getComponentManager();
		auto& events = components.getEventManager();
		const auto entity = world.createEntity();
		components.addComponents(entity, Position { 4 }, Velocity { 6 });
		components.removeComponents<Velocity>(entity);
		expect(!events.findSignal<OnAdd<Velocity>>() && !events.findSignal<OnRemove<Velocity>>(), "component edits do not create unused signals");

		std::vector<int> delivered;
		const auto added = events.subscribe<OnAdd<Velocity>>([&](const OnAdd<Velocity>& event) {
			expect(
				event.entity == entity && components.tryGetComponent<Velocity>(entity) == event.component, "OnAdd observes the inserted component");
			delivered.push_back(event.component->value);
		});
		const auto removed = events.subscribe<OnRemove<Velocity>>([&](const OnRemove<Velocity>& event) {
			expect(event.entity == entity && components.tryGetComponent<Velocity>(entity) == event.component,
				"OnRemove observes the original component");
			delivered.push_back(-event.component->value);
		});
		const auto original = components.addComponent(entity, Velocity { 8 });
		const auto replacement = components.addComponent(entity, Velocity { 9 });
		expect(!components.resolveComponent(original) && components.resolveComponent(replacement), "replacement changes the component generation");
		components.removeComponents<Velocity>(entity);
		components.removeComponents<Velocity>(entity);
		expect(delivered == std::vector<int> { 8, 9, -9 }, "listeners see insertion, replacement and genuine removal in order");

		events.unsubscribe<OnAdd<Velocity>>(added);
		events.unsubscribe<OnRemove<Velocity>>(removed);
		components.addComponents(entity, Velocity { 10 });
		components.removeComponents<Velocity>(entity);
		expect(delivered.size() == 3, "unsubscribed listeners receive no component events");
	}

	void checkQueries(ArchetypeLayout layout) {
		World world { layout };
		auto& components = *world.getComponentManager();
		components.registerComponent<Health>(StorageType::Sparse);
		components.registerComponent<Shield>(StorageType::Sparse);
		int emptyVisits = 0;
		components.forEach<Read<Position>>([&](Entity, const Position&) { ++emptyVisits; });
		expect(emptyVisits == 0, "unregistered query has no matches");

		const auto first = world.createEntity();
		const auto second = world.createEntity();
		const auto third = world.createEntity();
		components.addComponents(first, Position { 1 }, Velocity { 11 });
		components.addComponents(second, Position { 2 }, Velocity { 22 }, Health { 50 });
		components.addComponents(third, Position { 3 }, Health { 75 }, Shield { 6 });
		components.addComponent(first, Shield { 4 });
		components.removeComponents<Velocity>(first);
		components.addComponent(third, Velocity { 33 });
		components.addComponent(second, Position { 4 });

		std::unordered_map<Entity, int> positions;
		components.forEach<Read<Position>, Read<Velocity>>([&](Entity e, const Position& position, const Velocity& velocity) {
			expect(positions.emplace(e, position.value).second, "archetype query visits each entity once");
			expect((e == second && velocity.value == 22) || (e == third && velocity.value == 33), "archetype columns retain entity ownership");
		});
		expect(positions == std::unordered_map<Entity, int> { { second, 4 }, { third, 3 } }, "archetype query returns the expected rows");

		std::unordered_set<Entity> sparseMatches;
		components.forEach<Read<Health>, Read<Shield>>([&](Entity e, const Health& health, const Shield& shield) {
			expect(sparseMatches.insert(e).second && health.value == 75 && shield.value == 6, "sparse join returns matching values once");
		});
		expect(sparseMatches == std::unordered_set<Entity> { third }, "sparse join filters both missing components");

		std::unordered_set<Entity> mixedMatches;
		components.forEach<Write<Position>, Read<Shield>>([&](Entity e, Position& position, const Shield& shield) {
			expect(mixedMatches.insert(e).second, "mixed query visits each entity once");
			position.value += shield.value;
		});
		expect(mixedMatches == std::unordered_set<Entity> { first, third }, "mixed query filters missing sparse components");
		const auto* updated = components.tryGetComponent<Position>(third);
		expect(updated && updated->value == 9, "mixed query writes the owning component");

		components.forEach<Read<Position>>([&](Entity e, const Position&) {
			expectThrows([&] { world.destroyEntity(e); }, "immediate structural edit during a query is rejected");
		});
	}

	void checkMixedJoin(ArchetypeLayout layout) {
		World world { layout };
		auto& components = *world.getComponentManager();
		components.registerComponent<Health>(StorageType::Sparse);
		components.registerComponent<Shield>(StorageType::Sparse);
		std::vector<Entity> entities;
		std::unordered_set<Entity> expected;
		for (int index = 0; index < 60; ++index) {
			const auto entity = world.createEntity();
			entities.push_back(entity);
			components.addComponent(entity, Position { index });
			if (index % 2 != 0) {
				components.addComponent(entity, Velocity { index });
			}
			if (index != 20) {
				components.addComponent(entity, Health { index });
			}
			if (index % 10 == 0 || index == 21) {
				components.addComponent(entity, Shield { 7 });
				if (index != 20) {
					expected.insert(entity);
				}
			}
		}
		const auto sparseOnly = world.createEntity();
		components.addComponents(sparseOnly, Health { 61 }, Shield { 7 });
		const auto verify = [&] {
			std::unordered_set<Entity> visited;
			components.forEach<Read<Health>, Write<Position>, Read<Shield>>([&](Entity entity, const Health& health, Position& position,
																				const Shield& shield) {
				expect(visited.insert(entity).second, "mixed join visits each entity once");
				const auto index = static_cast<std::size_t>(health.value);
				expect(index < entities.size() && entities[index] == entity && shield.value == 7, "mixed join resolves the correct sparse values");
				position.value = health.value + shield.value;
			});
			expect(visited == expected, "mixed join follows both archetype and sparse filters");
			for (const auto entity : expected) {
				const auto* health = components.tryGetComponent<Health>(entity);
				const auto* position = components.tryGetComponent<Position>(entity);
				expect(health && position && position->value == health->value + 7, "mixed join writes through to the right archetype row");
			}
		};
		verify();

		components.removeComponents<Position>(entities[0]);
		world.destroyEntity(entities[10]);
		components.removeComponents<Shield>(entities[30]);
		components.addComponent(entities[40], Velocity { 40 });
		expected.erase(entities[0]);
		expected.erase(entities[10]);
		expected.erase(entities[30]);
		verify();

		// Populate the sparse driver until a scan of the matching archetypes is cheaper.
		for (std::size_t index = 1; index < entities.size(); ++index) {
			if (index == 10 || index == 20) {
				continue;
			}
			const auto entity = entities[index];
			if (!components.checkComponent<Shield>(entity)) {
				components.addComponent(entity, Shield { 7 });
			}
			expected.insert(entity);
		}
		verify();
	}

	void checkDenseChunks(ArchetypeLayout layout) {
		World world { layout };
		auto& components = *world.getComponentManager();
		std::unordered_map<Entity, int> expected;
		std::vector<Entity> entities;
		for (int index = 0; index < 257; ++index) {
			const auto entity = world.createEntity();
			entities.push_back(entity);
			components.addComponents(entity, Position { index }, Velocity { 2 * index });
			expected.emplace(entity, index);
		}
		for (std::size_t index = 0; index < 117; ++index) {
			components.addComponent(entities[index], Shield { 1 });
		}
		world.destroyEntity(entities[10]);
		expected.erase(entities[10]);

		std::unordered_set<Entity> visited;
		components.forEach<Read<Velocity>, Write<Position>>([&](Entity entity, const Velocity& velocity, Position& position) {
			const auto entry = expected.find(entity);
			expect(entry != expected.end() && position.value == entry->second && velocity.value == 2 * entry->second,
				"dense iteration matches entity IDs and components across chunk boundaries");
			expect(visited.insert(entity).second, "dense iteration visits a row only once");
			position.value += velocity.value;
		});
		expect(visited.size() == expected.size(), "dense iteration visits full and partial chunks in both archetypes");
		for (const auto& [entity, value] : expected) {
			const auto* position = components.tryGetComponent<Position>(entity);
			expect(position && position->value == 3 * value, "dense query writes through to the original column");
		}
	}

	void checkAlignment(ArchetypeLayout layout) {
		World world { layout };
		auto& components = *world.getComponentManager();
		components.registerComponent<AlignedVelocity>(StorageType::Sparse);
		std::vector<Entity> entities;
		constexpr int entityCount = 257;
		for (int index = 0; index < entityCount; ++index) {
			const auto entity = world.createEntity();
			entities.push_back(entity);
			components.addComponents(entity, AlignedPosition { index }, AlignedVelocity { index });
		}

		const auto verify = [&] {
			for (int index = 1; index < entityCount; ++index) {
				const auto entity = entities[index];
				const auto* position = components.tryGetComponent<AlignedPosition>(entity);
				const auto* velocity = components.tryGetComponent<AlignedVelocity>(entity);
				expect(position && reinterpret_cast<std::uintptr_t>(position) % alignof(AlignedPosition) == 0 && position->value == index,
					"archetype element retains alignment and value");
				expect(velocity && reinterpret_cast<std::uintptr_t>(velocity) % alignof(AlignedVelocity) == 0 && velocity->value == index,
					"sparse element retains alignment and value");
			}
		};
		verify();
		components.addComponent(entities[100], Position { 1 });
		world.destroyEntity(entities.front());
		verify();
	}

	void checkRelationCleanup(ArchetypeLayout layout) {
		World world { layout };
		auto& components = *world.getComponentManager();
		auto& relations = components.getRelationManager();
		const auto follows = components.getComponentType<Follows>();
		const auto owns = components.getComponentType<Owns>();
		const auto owner = world.createEntity();
		const auto target = world.createEntity();
		const auto other = world.createEntity();
		const auto secondTarget = world.createEntity();
		relations.addRelation(owner, follows, target);
		relations.addRelation(owner, follows, target);
		relations.addRelation(owner, owns, other);
		relations.addRelation(owner, follows, secondTarget);
		const std::vector<Entity> expected { target, secondTarget };
		expect(relations.getTargets(owner, follows) == expected, "duplicate and unrelated relation types are filtered");
		std::vector<Entity> visited;
		relations.forEachTarget(owner, follows, [&](Entity entity) { visited.push_back(entity); });
		relations.forEachTarget(other, follows, [&](Entity entity) { visited.push_back(entity); });
		expect(visited == expected, "relation visitor preserves order and skips entities without outgoing relations");
		world.destroyEntity(target);
		expect(relations.getTargets(owner, follows) == std::vector<Entity> { secondTarget }, "destroying the target removes its incoming relations");
		expect(relations.hasRelation(owner, owns, other), "destroying a target preserves unrelated relations");
		world.destroyEntity(owner);
		expect(relations.getTargets(owner, owns).empty(), "destroying the owner removes outgoing relations");
	}
}

void checkIndexedTransitionFailure() {
	using namespace ECS;
	World world { ArchetypeLayout::SharedRows };
	auto& components = *world.getComponentManager();
	expectThrows([&] { components.registerComponent<ThrowingValue>(); }, "shared rows reject potentially throwing dense relocation");
	components.registerComponent<ThrowingValue>(StorageType::Sparse);
	components.registerComponent<Health>(StorageType::Sparse);
	const auto entity = world.createEntity();
	const auto position = components.addComponent(entity, Position { 17 });
	expectThrows([&] { components.addComponents(entity, Velocity { 5 }, Health { 10 }, ThrowingValue { true }); },
		"a failed sparse insertion aborts the prepared indexed transition");
	expect(components.resolveComponent(position) && components.resolveComponent(position)->value == 17, "the original row survives a failed append");
	expect(!components.checkComponent<Health>(entity) && !components.checkComponent<Velocity>(entity), "failed append leaves no added components");
	int visited = 0;
	components.forEach<Read<Velocity>>([&](Entity, const Velocity&) { ++visited; });
	expect(visited == 0, "failed append publishes no destination row");
	components.addComponents(entity, Velocity { 5 }, Health { 10 }, ThrowingValue { false });
	expect(components.checkComponent<Velocity>(entity) && components.checkComponent<Health>(entity), "the prepared archetype remains usable");
}

void checkIndexedWeakRelocation() {
	using namespace ECS;
	World world { ArchetypeLayout::SharedRows };
	auto& components = *world.getComponentManager();
	const auto first = world.createEntity();
	const auto second = world.createEntity();
	TrackedComponent initial;
	initial.value = 7;
	const auto firstWeak = components.addComponent(first, std::move(initial));
	TrackedComponent other;
	other.value = 11;
	const auto secondWeak = components.addComponent(second, std::move(other));
	const auto handle = components.getComponentHandle<TrackedComponent>(second);

	components.addComponent(first, Velocity { 3 });
	expect(firstWeak && firstWeak->value == 7, "migration relocates weak references to the destination column");
	expect(secondWeak && secondWeak->value == 11 && components.resolveComponent(handle) == secondWeak.get(),
		"swap-remove relocates the source tail and preserves its generation");
	TrackedComponent replacement;
	replacement.value = 21;
	components.addComponent(second, std::move(replacement));
	expect(!secondWeak && !components.resolveComponent(handle), "replacement invalidates weak references and the old component generation");
	world.destroyEntity(first);
	expect(!firstWeak, "destroying an indexed row invalidates weak references");
}

void runStorageTests() {
	for (const auto layout : { ECS::ArchetypeLayout::PerColumn, ECS::ArchetypeLayout::SharedRows }) {
		checkHandles(layout);
		checkImportedIds(layout);
		checkHybridRegistry(layout);
		checkComponentEvents(layout);
		checkQueries(layout);
		checkMixedJoin(layout);
		checkDenseChunks(layout);
		checkAlignment(layout);
		checkRelationCleanup(layout);
	}
	checkIndexedTransitionFailure();
	checkIndexedWeakRelocation();
}
}
