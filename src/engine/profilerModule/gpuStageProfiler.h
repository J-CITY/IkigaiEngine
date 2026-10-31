#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace IKIGAI::PROFILER {
struct GpuStageTiming {
	std::string Name = {};
	std::uint64_t Samples = 0;
	std::size_t RetainedSamples = 0;
	double MeanMilliseconds = 0.0;
	double MinimumMilliseconds = 0.0;
	double MaximumMilliseconds = 0.0;
	double MedianMilliseconds = 0.0;
	double Percentile95Milliseconds = 0.0;
};
struct GpuProfileSnapshot {
	bool Supported = false;
	bool Enabled = false;
	std::size_t PendingSamples = 0;
	std::size_t AllocatedQueryPairs = 0;
	std::uint64_t DroppedSamples = 0;
	std::vector<GpuStageTiming> Stages = {};
};

// Render-thread API. Desktop GL timestamps and Vulkan/DX12 driver timestamp
// queries (DriverInterface::writeTimestamp) allow nested frame/pass scopes.
// Other backends report unsupported and submit no queries.
class GpuStageProfiler {
public:
	class Scope {
	public:
		Scope();
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
		Scope(Scope&& other) noexcept;
		Scope& operator=(Scope&&) = delete;

	private:
		friend class GpuStageProfiler;
		Scope(GpuStageProfiler* owner, std::size_t slot);
		GpuStageProfiler* mOwner = nullptr;
		std::size_t mSlot = 0;
	};
	GpuStageProfiler();
	~GpuStageProfiler();
	GpuStageProfiler(const GpuStageProfiler&) = delete;
	GpuStageProfiler& operator=(const GpuStageProfiler&) = delete;
	void setEnabled(bool enabled);
	void beginFrame();
	Scope measure(std::string_view name);
	// Pending samples from before reset are retired without entering new stats.
	void reset();
	GpuProfileSnapshot snapshot() const;

private:
	void end(std::size_t slot);
	struct Impl;
	std::unique_ptr<Impl> mImpl;
};
}
