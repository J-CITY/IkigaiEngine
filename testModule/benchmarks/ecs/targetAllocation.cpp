#include "utilsModule/chunkList.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace {
enum class StorageKind { FreeListChunks, HeapChunks, ContiguousGenerations, SegmentedGenerations, GeometricGenerations };

template <StorageKind Kind> void reserveTargetStorage(benchmark::State& state) {
	using Clock = std::chrono::steady_clock;
	using Chunk = IKIGAI::UTILS::ChunkList<int>;
	using GenerationPages = IKIGAI::UTILS::ChunkedVector<std::uint64_t, 256, std::allocator<std::uint64_t>>;
	constexpr bool contiguous = Kind == StorageKind::ContiguousGenerations || Kind == StorageKind::GeometricGenerations;
	using Storage = std::conditional_t<contiguous, std::vector<std::uint64_t>,
		std::conditional_t<Kind == StorageKind::SegmentedGenerations, GenerationPages, Chunk>>;
	const auto count = static_cast<std::size_t>(state.range(0));
	const auto columns = static_cast<std::size_t>(state.range(1)) + 1;
	std::size_t allocationSteps = 0;
	std::size_t copiedBytes = 0;
	for (auto _ : state) {
		std::vector<Storage> storage;
		storage.reserve(columns);
		for (std::size_t column = 0; column < columns; ++column) {
			if constexpr (Kind == StorageKind::HeapChunks) {
				using namespace IKIGAI::MEMORY;
				static auto heap = std::make_shared<OptionalProfiledAllocator<HeapAllocator, Category::Component>>();
				storage.emplace_back(typename Storage::allocator_type { heap });
			}
			else {
				storage.emplace_back();
			}
		}
		std::chrono::duration<double> reserveTime {};
		allocationSteps = 0;
		copiedBytes = 0;
		for (std::size_t row = 0; row < count;) {
			if constexpr (contiguous) {
				for (auto& column : storage) {
					column.resize(row, 1);
				}
				copiedBytes += row * columns * sizeof(std::uint64_t);
			}
			const auto extra = Kind == StorageKind::GeometricGenerations && row >= 4096 ? row : row / 2 + 1;
			const auto minimum = contiguous ? row + extra : row + 1;
			const auto start = Clock::now();
			for (auto& column : storage) {
				column.reserve(minimum);
			}
			reserveTime += Clock::now() - start;
			++allocationSteps;
			row = storage.front().capacity();
		}
		state.SetIterationTime(reserveTime.count());
	}
	state.SetItemsProcessed(state.iterations() * count);
	state.counters["reserve_calls"] = static_cast<double>(allocationSteps * columns);
	state.counters["copied_bytes"] = static_cast<double>(copiedBytes);
}

BENCHMARK_TEMPLATE(reserveTargetStorage, StorageKind::FreeListChunks)
	->Name("Ikigai/TargetAllocation/Chunks")
	->UseManualTime()
	->Args({ 100000, 1 })
	->Args({ 100000, 4 })
	->Args({ 100000, 8 });
BENCHMARK_TEMPLATE(reserveTargetStorage, StorageKind::ContiguousGenerations)
	->Name("Ikigai/TargetAllocation/Generations")
	->UseManualTime()
	->Args({ 100000, 1 })
	->Args({ 100000, 4 })
	->Args({ 100000, 8 });
BENCHMARK_TEMPLATE(reserveTargetStorage, StorageKind::HeapChunks)
	->Name("Ikigai/TargetAllocation/HeapChunks")
	->UseManualTime()
	->Args({ 100000, 1 })
	->Args({ 100000, 4 })
	->Args({ 100000, 8 });
BENCHMARK_TEMPLATE(reserveTargetStorage, StorageKind::SegmentedGenerations)
	->Name("Ikigai/TargetAllocation/GenerationPages")
	->UseManualTime()
	->Args({ 100000, 1 })
	->Args({ 100000, 4 })
	->Args({ 100000, 8 });
BENCHMARK_TEMPLATE(reserveTargetStorage, StorageKind::GeometricGenerations)
	->Name("Ikigai/TargetAllocation/GeometricGenerations")
	->UseManualTime()
	->Args({ 100000, 1 })
	->Args({ 100000, 4 })
	->Args({ 100000, 8 });
}
