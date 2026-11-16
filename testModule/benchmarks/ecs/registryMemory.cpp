#include "registryFixture.h"

#include "ecsModule/world.h"

#include <Windows.h>
#include <cstdio>
#include <malloc.h>
#include <stdexcept>
#include <vector>

namespace {
std::size_t liveHeapBytes() {
	const auto heap = reinterpret_cast<HANDLE>(_get_heap_handle());
	if (!HeapLock(heap)) {
		throw std::runtime_error("Could not lock the CRT heap");
	}

	std::size_t bytes = 0;
	PROCESS_HEAP_ENTRY entry {};
	while (HeapWalk(heap, &entry)) {
		if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0) {
			bytes += entry.cbData;
		}
	}
	const auto walkError = GetLastError();
	if (!HeapUnlock(heap) || walkError != ERROR_NO_MORE_ITEMS) {
		throw std::runtime_error("Could not walk the CRT heap");
	}
	return bytes;
}

void measure(const char* name, std::size_t count, bool supplied) {
	using namespace IKIGAI::ECS;
	World world { ArchetypeLayout::SharedRows };
	std::vector<Entity> entities(count);
	if (supplied) {
		for (std::size_t index = 0; index < count; ++index) {
			entities[index] = IKIGAI::BENCHMARKS::sparseRegistryEntity(index);
		}
	}
	auto& components = *world.getComponentManager();
	components.getArchetype(Signature {});
	const auto before = liveHeapBytes();
	for (auto& entity : entities) {
		if (supplied) {
			world.registerEntity(entity);
		}
		else {
			entity = world.createEntity();
		}
	}
	const auto live = liveHeapBytes();
	for (const auto entity : entities) {
		world.destroyEntity(entity);
	}
	const auto after = liveHeapBytes();
	if (live < before || after < before) {
		throw std::runtime_error("Unexpected change to allocations outside the registry");
	}
	std::printf("%s,%zu,%zu,%zu\n", name, count, live - before, after - before);
}
}

int main() {
	std::puts("scenario,entities,live_heap_bytes,retained_heap_bytes");
	measure("register_sparse_ids", 1000, true);
	measure("create_dense_ids", 100000, false);
}
