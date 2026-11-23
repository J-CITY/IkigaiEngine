#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#ifndef IKIGAI_ENABLE_MEMORY_TRACKING
#define IKIGAI_ENABLE_MEMORY_TRACKING 0
#endif
#ifndef USE_PROFILER
#define USE_PROFILER 1
#endif

namespace IKIGAI::PROFILER {
enum class GpuBackend { Vulkan, OpenGL, DirectX12, Metal, Count };
enum class GpuResourceKind {
	Texture,
	Buffer,
	VertexBuffer,
	IndexBuffer,
	UniformBuffer,
	StorageBuffer,
	Staging,
	RenderTarget,
	AccelerationStructure,
	Shader,
	Pipeline,
	Count
};
// ReportedAllocation is an allocator-reported byte count, not physical residency.
enum class GpuMeasurement { ReportedAllocation, EstimatedStorage, Unavailable };
struct GpuAllocationInfo {
	GpuBackend backend = GpuBackend::Vulkan;
	std::uintptr_t device = 0;
	std::uint64_t handle = 0;
	GpuResourceKind kind = GpuResourceKind::Buffer;
	std::uint64_t bytes = 0, requestedBytes = 0;
	GpuMeasurement measurement = GpuMeasurement::ReportedAllocation;
	std::uint32_t heap = UINT32_MAX;
	bool deviceLocal = false, hostVisible = false;
	const char* name = nullptr;
};
struct GpuAllocationRecord {
	std::uint64_t id = 0;
	GpuAllocationInfo info;
	std::string name;
	bool pendingRelease = false;
};
struct GpuMemoryTotals {
	std::uint64_t reportedBytes = 0, estimatedBytes = 0, peakBytes = 0;
	std::uint64_t pendingBytes = 0, allocations = 0, pendingAllocations = 0;
	std::uint64_t unknownSizeAllocations = 0;
};
struct GpuHeapSnapshot {
	GpuBackend backend = GpuBackend::Vulkan;
	std::uintptr_t device = 0;
	std::uint32_t heap = 0;
	bool deviceLocal = false;
	std::uint64_t capacityBytes = 0, allocationBytes = 0, reservedBytes = 0;
	std::uint64_t allocationCount = 0, processUsageBytes = 0, budgetBytes = 0;
	// False means VMA's allocator-based fallback, not a driver memory-budget query.
	bool driverBudget = false;
	bool capacityAvailable = true, allocatorStatsAvailable = true;
	bool usageAvailable = true, budgetAvailable = true;
	// Metal device-wide resource allocations/recommended working set are not
	// OS residency measurements or an assigned DXGI/Vulkan process budget.
	std::optional<std::uint64_t> deviceAllocatedBytes, recommendedWorkingSetBytes;
	std::optional<bool> unifiedMemory;
	std::optional<std::uint64_t> residentBytes;
};
struct GpuMemorySnapshot {
	bool trackingEnabled = false;
	std::array<bool, static_cast<std::size_t>(GpuBackend::Count)> allocationTrackingAvailable {};
	GpuMemoryTotals totals;
	std::array<GpuMemoryTotals, static_cast<std::size_t>(GpuBackend::Count)> backends {};
	std::array<GpuMemoryTotals, static_cast<std::size_t>(GpuResourceKind::Count)> kinds {};
	std::vector<GpuAllocationRecord> allocations;
	std::vector<GpuHeapSnapshot> heaps;
	std::uint64_t droppedRecords = 0, failedSamplers = 0;
};

// Snapshot callbacks execute outside the allocation ledger lock. disable()
// waits for an in-flight sample before releasing the backend allocator/device.
class GpuMemorySampler {
public:
	using Callback = std::function<std::vector<GpuHeapSnapshot>()>;
	explicit GpuMemorySampler(Callback callback)
		: mCallback(std::move(callback)) { }
#if USE_PROFILER
	void disable();
	std::vector<GpuHeapSnapshot> capture();
#else
	void disable() { }
	std::vector<GpuHeapSnapshot> capture() {
		return {};
	}
#endif

private:
	std::mutex mMutex;
	Callback mCallback;
};

class GpuMemoryProfiler {
public:
#if USE_PROFILER
	static GpuMemoryProfiler& instance();
	std::uint64_t record(const GpuAllocationInfo&) noexcept;
	void update(std::uint64_t id, const GpuAllocationInfo&) noexcept;
	void release(std::uint64_t id) noexcept;
	void markPending(std::uint64_t id) noexcept;
	void setName(std::uint64_t id, const char* name) noexcept;
	void registerSampler(const std::shared_ptr<GpuMemorySampler>&);
	void noteDroppedRecord() noexcept;
	GpuMemorySnapshot captureSnapshot() const;
#else
	static GpuMemoryProfiler& instance() {
		static GpuMemoryProfiler profiler;
		return profiler;
	}
	std::uint64_t record(const GpuAllocationInfo&) noexcept {
		return 0;
	}
	void update(std::uint64_t, const GpuAllocationInfo&) noexcept { }
	void release(std::uint64_t) noexcept { }
	void markPending(std::uint64_t) noexcept { }
	void setName(std::uint64_t, const char*) noexcept { }
	void registerSampler(const std::shared_ptr<GpuMemorySampler>&) { }
	void noteDroppedRecord() noexcept { }
	GpuMemorySnapshot captureSnapshot() const {
		return {};
	}
#endif
};

// Moves transfer registration; they never allocate a second record. Native
// wrappers release the token at their real destruction point, after GPU fences.
class GpuAllocationToken {
public:
	GpuAllocationToken() = default;
	~GpuAllocationToken() {
		reset();
	}
	GpuAllocationToken(const GpuAllocationToken&) = delete;
	GpuAllocationToken& operator=(const GpuAllocationToken&) = delete;
#if USE_PROFILER
	GpuAllocationToken(GpuAllocationToken&& other) noexcept;
	GpuAllocationToken& operator=(GpuAllocationToken&& other) noexcept;
	void track(const GpuAllocationInfo&) noexcept;
	void reset() noexcept;
	void markPending() noexcept;
	void setName(const char*) noexcept;
	std::uint64_t id() const noexcept;
#else
	GpuAllocationToken(GpuAllocationToken&&) noexcept = default;
	GpuAllocationToken& operator=(GpuAllocationToken&&) noexcept = default;
	void track(const GpuAllocationInfo&) noexcept { }
	void reset() noexcept { }
	void markPending() noexcept { }
	void setName(const char*) noexcept { }
	std::uint64_t id() const noexcept {
		return 0;
	}
#endif

private:
#if IKIGAI_ENABLE_MEMORY_TRACKING
	std::uint64_t mId = 0;
#endif
};

template <class T> std::uint64_t GpuHandleValue(T handle) noexcept {
	if constexpr (std::is_pointer_v<T>) {
		return reinterpret_cast<std::uintptr_t>(handle);
	}
	else {
		return static_cast<std::uint64_t>(handle);
	}
}
inline GpuAllocationToken TrackGpuObject(
	GpuBackend backend, std::uintptr_t device, std::uint64_t handle, GpuResourceKind kind, const char* name = nullptr) noexcept {
	GpuAllocationToken token;
#if IKIGAI_ENABLE_MEMORY_TRACKING
	if (handle) {
		GpuAllocationInfo info;
		info.backend = backend;
		info.device = device;
		info.handle = handle;
		info.kind = kind;
		info.name = name;
		info.measurement = GpuMeasurement::Unavailable;
		token.track(info);
	}
#endif
	return token;
}
}
