#pragma once
#include <cstdint>
#include <optional>

#ifndef USE_PROFILER
#define USE_PROFILER 1
#endif

namespace IKIGAI::PROFILER {
struct ProcessMemorySnapshot {
	std::optional<std::uint64_t> residentBytes, peakResidentBytes;
	std::optional<std::uint64_t> privateCommittedBytes, physicalFootprintBytes;
	std::optional<std::uint64_t> totalPhysicalBytes, availablePhysicalBytes;
};
// OS counters, sampled only when requested; independent of engine allocators.
#if USE_PROFILER
ProcessMemorySnapshot CaptureProcessMemory() noexcept;
#else
inline ProcessMemorySnapshot CaptureProcessMemory() noexcept {
	return {};
}
#endif
}
