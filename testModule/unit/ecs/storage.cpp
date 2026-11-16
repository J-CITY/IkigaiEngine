#include "components.h"
#include "ecsModule/world.h"
#include "support/expect.h"

#include <algorithm>
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

	void checkIndexedPair() {
		World world { ArchetypeLayout::SharedRows };
		auto& components = *world.getComponentManager();
		components.registerComponent<Health>(StorageType::Sparse);
		std::vector<Entity> entities;
		for (int index = 0; index < 600; ++index) {
			const auto entity = world.createEntity();
			entities.push_back(entity);
			components.addComponent(entity, Position { index });
		}
		std::unordered_map<Entity, int> expected;
		for (int index = 596; index >= 0; index -= 4) {
			components.addComponent(entities[index], Health { 10 });
			expected.emplace(entities[index], index);
		}
		const auto lastId = entities.back().getUniqueId().value();
		const Entity loaded { Entity::ID { lastId + 65537 } };
		const Entity sparseOnly { Entity::ID { lastId + 131074 } };
		world.registerEntity(loaded);
		world.registerEntity(sparseOnly);
		components.addComponents(loaded, Health { 10 }, Position { 700 });
		components.addComponent(sparseOnly, Health { 10 });
		expected.emplace(loaded, 700);

		std::unordered_set<Entity> visited;
		components.forEach<Write<Health>, Read<Position>>([&](Entity entity, Health& health, const Position& position) {
			expect(visited.insert(entity).second && expected.at(entity) == position.value,
				"indexed pair uses the correct row across chunks and ID pages");
			health.value += position.value;
		});
		expect(visited.size() == expected.size(), "indexed pair also resolves compact IDs");

		components.removeComponents<Position>(loaded);
		expected.erase(loaded);
		visited.clear();
		components.forEach<Read<Position>, Read<Health>>([&](Entity entity, const Position& position, const Health& health) {
			expect(
				visited.insert(entity).second && expected.at(entity) == position.value, "indexed pair rejects rows outside the matching archetype");
			expect(health.value == 10 + position.value, "indexed pair preserves parameter order and writes to the original sparse pool");
		});
		expect(visited.size() == expected.size(), "indexed pair remains correct after an archetype transition");
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

void checkIndexedTransitionPlans() {
	using namespace ECS;
	World world { ArchetypeLayout::SharedRows };
	auto& components = *world.getComponentManager();
	std::array<Entity, 257> entities {};
	std::array<ComponentHandle<Position>, 257> positions {};
	for (std::size_t index = 0; index < entities.size(); ++index) {
		entities[index] = world.createEntity();
		positions[index] = components.addComponent(entities[index], Position { static_cast<int>(index) });
	}
	for (std::size_t index = 0; index < entities.size(); ++index) {
		const auto entity = entities[index];
		if (index % 3 == 0) {
			components.addComponent(entity, Shield { 5 });
			components.removeComponents<Shield>(entity);
		}
		components.addComponent(entity, Velocity { 7 });
	}
	for (std::size_t index = 0; index < entities.size(); ++index) {
		const auto* position = components.resolveComponent(positions[index]);
		expect(position && position->value == static_cast<int>(index), "cached transitions preserve handles across column growth and swap-removal");
		const auto* velocity = components.tryGetComponent<Velocity>(entities[index]);
		expect(velocity && velocity->value == 7, "cached transitions choose the correct destination after alternating edges");
	}

	auto& events = components.getEventManager();
	const auto token = events.subscribe<OnAdd<Shield>>([&](const OnAdd<Shield>& event) { components.removeComponents<Shield>(event.entity); });
	const auto removed = components.addComponent(entities.front(), Shield { 9 });
	expect(!removed && !components.checkComponent<Shield>(entities.front()), "add returns the current handle after an OnAdd listener removes it");
	events.unsubscribe<OnAdd<Shield>>(token);
	const auto retained = components.addComponent(entities.front(), Shield { 10 });
	expect(components.resolveComponent(retained) != nullptr, "add returns a live handle after the listener is removed");
}

void checkArchetypeBatch(ECS::ArchetypeLayout layout) {
	using namespace ECS;
	World world { layout };
	auto& components = *world.getComponentManager();
	std::array<Entity, 257> entities {};
	std::array<ComponentHandle<Position>, 257> positions {};
	for (std::size_t index = 0; index < entities.size(); ++index) {
		entities[index] = world.createEntity();
		positions[index] = components.addComponent(entities[index], Position { static_cast<int>(index) });
		if (index % 2 == 0) {
			components.addComponent(entities[index], Velocity { 5 });
		}
	}
	const auto previous = components.addComponent(entities.front(), Shield { 1 });
	const std::array missing { entities.back(), Entity {} };
	expectThrows([&] { components.addArchetypeComponentBatch(missing, Shield { 10 }); }, "invalid entity is detected before changing the batch");
	expect(!components.checkComponent<Shield>(entities.back()), "batch preflight leaves valid candidates unchanged on failure");

	std::vector<Entity> edits(entities.rbegin(), entities.rend());
	edits.push_back(entities.back());
	components.addArchetypeComponentBatch(edits, Shield { 10 });
	expect(!components.resolveComponent(previous), "batch replacement supersedes the old handle");
	for (std::size_t index = 0; index < entities.size(); ++index) {
		const auto* position = components.resolveComponent(positions[index]);
		const auto* shield = components.tryGetComponent<Shield>(entities[index]);
		expect(position && position->value == static_cast<int>(index) && shield && shield->value == 10,
			"batch retains component handles and maps values across source tables");
		expect(components.checkComponent<Velocity>(entities[index]) == (index % 2 == 0), "batch keeps the retained signature");
	}

	std::vector<Entity> events;
	auto& signals = components.getEventManager();
	const auto token = signals.subscribe<OnAdd<Health>>([&](const OnAdd<Health>& event) {
		events.push_back(event.entity);
		components.removeComponents<Health>(event.entity);
	});
	const std::array ordered { entities[10], entities[11], entities[10] };
	components.addArchetypeComponentBatch(ordered, Health { 20 });
	expect(
		std::ranges::equal(events, ordered) && !components.checkComponent<Health>(entities[10]), "batch honors callback order and structural edits");
	signals.unsubscribe<OnAdd<Health>>(token);

	Signature signature;
	const auto positionType = components.getComponentType<Position>();
	signature.set(positionType.value(), true);
	signature.set(components.getComponentType<Shield>().value(), true);
	const auto source = components.getArchetype(signature);
	const auto sourceRows = source->getComponents<Position>(positionType)->getEntities();
	expect(sourceRows.size() == entities.size() / 2, "fixture spans the odd-numbered source rows");
	components.addArchetypeComponentBatch(sourceRows, Health { 42 });
	for (std::size_t index = 0; index < entities.size(); ++index) {
		expect(components.checkComponent<Health>(entities[index]) == (index % 2 != 0), "batch can take a span into its source table");
	}

	const auto custom = world.createEntity();
	TrackedComponent tracked;
	tracked.value = 31;
	const auto weak = components.addComponent(custom, std::move(tracked));
	const std::array withCustomLifetime { custom, entities[2] };
	components.addArchetypeComponentBatch(withCustomLifetime, Health { 42 });
	expect(weak && weak->value == 31 && components.checkComponent<Health>(custom), "batch preserves weak references for retained components");
}

void checkChunkedArchetypeBatch() {
	using namespace ECS;
	World world { ArchetypeLayout::SharedRows };
	auto& components = *world.getComponentManager();
	const auto existing = world.createEntity();
	components.addComponents(existing, Position { -1 }, Velocity { -2 }, Health { 3 });
	const auto existingHealth = components.getComponentHandle<Health>(existing);

	std::vector<Entity> entities;
	std::vector<ComponentHandle<Position>> positions;
	for (int index = 0; index < 535; ++index) {
		const auto entity = world.createEntity();
		entities.push_back(entity);
		components.addComponents(entity, Position { index }, Velocity { -index });
		positions.push_back(components.getComponentHandle<Position>(entity));
	}
	Signature signature;
	const auto positionType = components.getComponentType<Position>();
	signature.set(positionType.value(), true);
	signature.set(components.getComponentType<Velocity>().value(), true);
	const auto source = components.getArchetype(signature);
	const auto column = source->getComponents<Position>(positionType);
	std::unordered_set<Entity> moved;
	for (const auto [first, count] : { std::pair { 37, 137 }, std::pair { 12, 329 } }) {
		const auto slice = column->getEntities().subspan(first, count);
		moved.insert(slice.begin(), slice.end());
		components.addArchetypeComponentBatch(slice, Health { 11 });
	}
	expect(source->size() == entities.size() - moved.size(), "chunked batches fill the source holes and trim its tail");
	for (std::size_t index = 0; index < entities.size(); ++index) {
		const auto* position = components.resolveComponent(positions[index]);
		const auto* velocity = components.tryGetComponent<Velocity>(entities[index]);
		const auto* health = components.tryGetComponent<Health>(entities[index]);
		expect(position && position->value == static_cast<int>(index) && velocity && velocity->value == -static_cast<int>(index),
			"chunked batches keep components and generations on the owning entity");
		expect(moved.contains(entities[index]) ? health && health->value == 11 : health == nullptr,
			"only selected rows move to the destination archetype");
	}
	const auto replaced = components.getComponentHandle<Health>(*moved.begin());
	components.addComponent(*moved.begin(), Health { 12 });
	expect(!components.resolveComponent(replaced), "single replacement advances the generation after chunked insertion");
	expect(components.resolveComponent(existingHealth) && components.resolveComponent(existingHealth)->value == 3,
		"existing destination rows keep their values and generations");
}

void checkGenerationRange() {
	struct TestGeneration;
	IdSequence<TestGeneration, std::uint8_t, 1, 5> sequence;
	expect(sequence.nextRange(3).value() == 1, "generation reservation starts at the next free generation");
	expectThrows([&] { sequence.nextRange(3); }, "generation reservation checks the entire range");
	expect(sequence.next().value() == 4 && sequence.nextRange(1).value() == 5, "failed generation reservation leaves the sequence intact");
	expectThrows([&] { sequence.next(); }, "sequence is exhausted after reserving its last generation");
}

void checkScatteredArchetypeBatch() {
	using namespace ECS;
	World world { ArchetypeLayout::SharedRows };
	auto& components = *world.getComponentManager();
	std::vector<Entity> entities;
	std::vector<ComponentHandle<Position>> positions;
	for (int index = 0; index < 1200; ++index) {
		const auto entity = world.createEntity();
		entities.push_back(entity);
		components.addComponents(entity, Position { index }, AlignedPosition { index + 1 }, AlignedVelocity { -index }, Owns {});
		positions.push_back(components.getComponentHandle<Position>(entity));
		if (index % 2 == 0) {
			components.addComponent(entity, Velocity { -index });
		}
	}
	std::array<std::vector<Entity>, 2> expectedOrder;
	std::vector<Entity> batch;
	for (int index = 1199; index >= 0; --index) {
		if (index % 3 == 0) {
			batch.push_back(entities[index]);
			expectedOrder[index % 2].push_back(entities[index]);
		}
	}
	components.addArchetypeComponentBatch(batch, Health { 20 });

	const auto positionType = components.getComponentType<Position>();
	Signature signature;
	signature.set(positionType.value(), true);
	signature.set(components.getComponentType<AlignedPosition>().value(), true);
	signature.set(components.getComponentType<AlignedVelocity>().value(), true);
	signature.set(components.getComponentType<Owns>().value(), true);
	signature.set(components.getComponentType<Health>().value(), true);
	for (const auto parity : { 1, 0 }) {
		signature.set(components.getComponentType<Velocity>().value(), parity == 0);
		const auto target = components.getArchetype(signature);
		expect(std::ranges::equal(target->getComponents<Position>(positionType)->getEntities(), expectedOrder[parity]),
			"grouping by source chunks preserves input order within each destination");
	}
	for (std::size_t index = 0; index < entities.size(); ++index) {
		const auto* position = components.resolveComponent(positions[index]);
		const auto* aligned = components.tryGetComponent<AlignedPosition>(entities[index]);
		const auto* velocity = components.tryGetComponent<Velocity>(entities[index]);
		const auto* health = components.tryGetComponent<Health>(entities[index]);
		expect(position && position->value == static_cast<int>(index), "scattered copy and source compaction preserve existing generations");
		expect(aligned && aligned->value == static_cast<int>(index) + 1, "scattered copy preserves values in an aligned column");
		expect(index % 2 == 0 ? velocity && velocity->value == -static_cast<int>(index) : velocity == nullptr,
			"source compaction preserves columns across different archetypes");
		expect(index % 3 == 0 ? health && health->value == 20 : health == nullptr, "scattered batch changes only selected entities");
	}

	auto repeated = expectedOrder[0];
	repeated.resize(150);
	repeated.push_back(repeated.front());
	components.addArchetypeComponentBatch(repeated, Shield { 5 });
	std::size_t visited = 0;
	components.forEach<Read<Shield>>([&](Entity, const Shield&) { ++visited; });
	expect(visited == repeated.size() - 1, "repeated IDs fall back to replacement without adding duplicate rows");
}

void checkWideSparseChunkBatch() {
	using namespace ECS;
	World world { ArchetypeLayout::SharedRows };
	auto& components = *world.getComponentManager();
	const auto addSource = [&](int value) {
		const auto entity = world.createEntity();
		components.addComponents(entity, Position { value }, Velocity { value }, Shield { value }, Follows {}, Owns {}, AlignedPosition { value + 1 },
			AlignedVelocity { -value }, static_cast<std::uint32_t>(value));
		return entity;
	};
	const auto existing = addSource(0);
	const auto oldHealth = components.addComponent(existing, Health { 7 });
	std::vector<Entity> entities;
	std::vector<ComponentHandle<Position>> positions;
	for (int index = 0; index < 1277; ++index) {
		const auto entity = addSource(index);
		entities.push_back(entity);
		positions.push_back(components.getComponentHandle<Position>(entity));
	}
	std::vector<Entity> selected;
	for (int index = 1270; index >= 0; index -= 10) {
		selected.push_back(entities[index]);
	}
	components.addArchetypeComponentBatch(selected, Health { 11 });
	Signature signature;
	[&]<class... T> {
		(signature.set(components.getComponentType<T>().value(), true), ...);
	}.template operator()<Position, Velocity, Shield, Follows, Owns, AlignedPosition, AlignedVelocity, std::uint32_t, Health>();
	const auto positionType = components.getComponentType<Position>();
	const auto targetRows = components.getArchetype(signature)->getComponents<Position>(positionType)->getEntities();
	expect(targetRows.size() == selected.size() + 1 && targetRows.front() == existing && std::ranges::equal(targetRows.subspan(1), selected),
		"wide sparse batches preserve the batch order across a partially filled destination chunk");
	expect(components.resolveComponent(oldHealth) && components.resolveComponent(oldHealth)->value == 7,
		"wide sparse batches preserve live rows preceding the batch");
	for (std::size_t index = 0; index < entities.size(); ++index) {
		const auto* position = components.resolveComponent(positions[index]);
		const auto* aligned = components.tryGetComponent<AlignedVelocity>(entities[index]);
		const auto* health = components.tryGetComponent<Health>(entities[index]);
		expect(position && position->value == static_cast<int>(index) && aligned && aligned->value == -static_cast<int>(index),
			"wide sparse batches retain component values and generations");
		expect(index % 10 == 0 ? health && health->value == 11 : health == nullptr, "bitmap compaction preserves the selected rows");
	}
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
		checkArchetypeBatch(layout);
	}
	checkIndexedTransitionFailure();
	checkIndexedWeakRelocation();
	checkIndexedTransitionPlans();
	checkIndexedPair();
	checkChunkedArchetypeBatch();
	checkScatteredArchetypeBatch();
	checkWideSparseChunkBatch();
	checkGenerationRange();
}
}
