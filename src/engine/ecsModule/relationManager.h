#pragma once
#include "entityManager.h"
#include "utilsModule/idGenerator.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

// Forward declaration if ComponentManager is invalid here, but we need ComponentType definition.
// ComponentType is Id<ComponentManager>. We know this.
namespace IKIGAI::ECS {
class ComponentManager;
using ComponentType = Id<ComponentManager>;

class RelationManager {
public:
	using RelationPair = std::pair<ComponentType, Entity>;

	void addRelation(Entity entity, ComponentType relation, Entity target) {
		auto& rels = mRelations[entity];
		// Check for duplicate
		for (const auto& pair : rels) {
			if (pair.first == relation && pair.second == target) {
				return;
			}
		}
		rels.emplace_back(relation, target);
	}

	void removeRelation(Entity entity, ComponentType relation, Entity target) {
		if (!mRelations.contains(entity)) {
			return;
		}

		auto& rels = mRelations[entity];
		rels.erase(
			std::remove_if(rels.begin(), rels.end(), [&](const RelationPair& pair) { return pair.first == relation && pair.second == target; }),
			rels.end());

		if (rels.empty()) {
			mRelations.erase(entity);
		}
	}

	[[nodiscard]] bool hasRelation(Entity entity, ComponentType relation, Entity target) const {
		if (!mRelations.contains(entity)) {
			return false;
		}
		const auto& rels = mRelations.at(entity);
		for (const auto& pair : rels) {
			if (pair.first == relation && pair.second == target) {
				return true;
			}
		}
		return false;
	}

	// Visit matching targets in insertion order. Callbacks must leave relation storage intact.
	template <class Visitor> void forEachTarget(Entity entity, ComponentType relation, Visitor&& visit) const {
		const auto entry = mRelations.find(entity);
		if (entry == mRelations.end()) {
			return;
		}

		for (const auto& [type, target] : entry->second) {
			if (type == relation) {
				visit(target);
			}
		}
	}

	[[nodiscard]] std::vector<Entity> getTargets(Entity entity, ComponentType relation) const {
		std::vector<Entity> targets;
		forEachTarget(entity, relation, [&](Entity target) { targets.push_back(target); });
		return targets;
	}

	// Removes all relations for a destroyed entity (either as source or target)
	// This can be expensive if we strictly need to remove 'target' references.
	// For now, let's implement removeOwner. RemoveTarget requires reverse map or global scan.
	void onEntityDestroyed(Entity entity) {
		if (mRelations.empty()) {
			return;
		}
		mRelations.erase(entity);
		// Optional: Remove where entity is target?
		// To support that efficiently we would need a reverse map: Target -> Vector<(Source, Relation)>
		// Let's implement Reverse Map for completeness and safety.
		removeAllWhereTarget(entity);
	}

private:
	std::unordered_map<Entity, std::vector<RelationPair>> mRelations;
	// TODO: Reverse map for fast "find parents" or "cleanup target"
	// std::unordered_map<Entity, std::vector<std::pair<Entity, ComponentType>>> mReverseRelations;

	void removeAllWhereTarget(Entity target) {
		// Naive implementation for now as we don't store reverse map
		// If performance becomes issue, add mReverseRelations
		for (auto& [source, rels] : mRelations) {
			rels.erase(std::remove_if(rels.begin(), rels.end(), [&](const RelationPair& pair) { return pair.second == target; }), rels.end());
		}
	}
};
}
