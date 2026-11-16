#pragma once

#include "components.h"
#include "ecsModule/world.h"

#include <entt/entity/registry.hpp>
#include <flecs.h>

#include <chrono>
#include <span>
#include <type_traits>
#include <utility>

namespace ECS_BENCHMARK {

template <int Alignment = alignof(float), bool SparseVelocity = false, IKIGAI::ECS::ArchetypeLayout Layout = IKIGAI::ECS::ArchetypeLayout::SharedRows>
class Ikigai {
public:
	using Entity = IKIGAI::ECS::Entity;
	using PositionType = Position<Alignment>;
	using VelocityType = Velocity<Alignment>;
	IKIGAI::ECS::World world { Layout };
	IKIGAI::ECS::ComponentManager& components = *world.getComponentManager();

	void registerMovement() {
		using IKIGAI::ECS::StorageType;
		components.registerComponent<PositionType>();
		components.registerComponent<VelocityType>(SparseVelocity ? StorageType::Sparse : StorageType::Archetype);
	}

	Entity create() {
		return world.createEntity();
	}
	void destroy(Entity entity) {
		world.destroyEntity(entity);
	}
	bool alive(Entity entity) const {
		return components.resolveEntity(components.getEntityHandle(entity)).has_value();
	}
	template <class T> void add(Entity entity, T value) {
		components.addComponent(entity, value);
	}
	template <class T> void remove(Entity entity) {
		components.removeComponents<T>(entity);
	}
	template <class T> const T* get(Entity entity) const {
		return components.tryGetComponent<T>(entity);
	}

	template <class Function> auto makePass(Function&& function) {
		return [this, function] {
			components.forEach<IKIGAI::ECS::Write<PositionType>, IKIGAI::ECS::Read<VelocityType>>(
				[&](Entity, PositionType& position, const VelocityType& velocity) { function(position, velocity); });
		};
	}
};

class EnTT {
public:
	using Entity = entt::entity;
	using PositionType = Position<alignof(float)>;
	using VelocityType = Velocity<alignof(float)>;

	void registerMovement() {
		registry.storage<PositionType>();
		registry.storage<VelocityType>();
	}
	Entity create() {
		return registry.create();
	}
	void destroy(Entity entity) {
		registry.destroy(entity);
	}
	bool alive(Entity entity) const {
		return registry.valid(entity);
	}
	template <class T> void add(Entity entity, T value) {
		registry.emplace<T>(entity, value);
	}
	template <class T> void addBatch(std::span<const Entity> entities, const T& value) {
		auto& storage = registry.storage<T>();
		storage.reserve(storage.size() + entities.size());
		registry.insert<T>(entities.begin(), entities.end(), value);
	}
	template <class T> void remove(Entity entity) {
		registry.remove<T>(entity);
	}
	template <class T> const T* get(Entity entity) const {
		return registry.try_get<T>(entity);
	}

	template <class Function> auto makePass(Function&& function) {
		return [view = registry.view<PositionType, const VelocityType>(), function] { view.each(function); };
	}

private:
	entt::registry registry;
};

template <flecs::query_cache_kind_t Cache = flecs::QueryCacheNone> class Flecs {
public:
	using Entity = flecs::entity;
	using PositionType = Position<alignof(float)>;
	using VelocityType = Velocity<alignof(float)>;

	void registerMovement() {
		world.component<PositionType>();
		world.component<VelocityType>();
	}
	Entity create() {
		return world.entity();
	}
	void destroy(Entity entity) {
		entity.destruct();
	}
	bool alive(Entity entity) const {
		return entity.is_alive();
	}

	template <class T> void add(Entity entity, T value) {
		if constexpr (std::is_empty_v<T>) {
			entity.add<T>();
		}
		else {
			entity.set(value);
		}
	}

	template <class T> void remove(Entity entity) {
		entity.remove<T>();
	}
	template <class T> const T* get(Entity entity) const {
		return entity.try_get<T>();
	}

	template <class Function> auto makePass(Function&& function) {
		auto query = world.query_builder<PositionType, const VelocityType>().cache_kind(Cache).build();
		return [query = std::move(query), function] { query.each(function); };
	}

	double queryBuildNs() {
		const auto start = std::chrono::steady_clock::now();
		auto query = world.query_builder<PositionType, const VelocityType>().cache_kind(Cache).build();
		const auto stop = std::chrono::steady_clock::now();
		return std::chrono::duration<double, std::nano>(stop - start).count();
	}

private:
	flecs::world world;
};

}
