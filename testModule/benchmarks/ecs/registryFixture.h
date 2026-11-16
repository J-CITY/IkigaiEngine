#pragma once

#include "ecsModule/entityManager.h"

#include <cstddef>
#include <cstdint>

namespace IKIGAI::BENCHMARKS {
inline ECS::Entity sparseRegistryEntity(std::size_t index) {
	return ECS::Entity { ECS::Entity::ID { 0x10000000u + static_cast<std::uint32_t>(index) * 65537u } };
}
}
