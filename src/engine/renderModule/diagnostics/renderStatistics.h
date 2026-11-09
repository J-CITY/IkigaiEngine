#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "renderModule/backends/interface/renderEnums.h"

namespace IKIGAI::RENDER {
struct RenderDrawCounters {
	std::uint64_t DrawCalls = 0;
	std::uint64_t IndirectDraws = 0;
	std::uint64_t Instances = 0;
	std::uint64_t Triangles = 0;
	std::uint64_t Dispatches = 0;
	std::uint64_t IndirectSubmissions = 0;
};

struct RenderStageStatistics {
	std::string Name = {};
	// Number of frames in which the stage ran (summary only; 1 for a single frame).
	std::uint64_t Frames = 0;
	// Exclusive counters: work recorded by nested stage scopes is attributed to the innermost stage.
	RenderDrawCounters Counters = {};
};

// Virtual geometry selection results (indirect draws carry no CPU-visible triangle counts).
struct RenderVirtualGeometryCounters {
	std::uint64_t ClusterInstances = 0;
	std::uint64_t FallbackInstances = 0;
	std::uint64_t VisibleClusters = 0;
	std::uint64_t VisibleTriangles = 0;
};

struct RenderFrameStatistics {
	std::uint64_t Frames = 0;
	RenderDrawCounters Total = {};
	std::vector<RenderStageStatistics> Stages = {};
	RenderVirtualGeometryCounters VirtualGeometry = {};
};

// CPU-side draw/dispatch counters fed by the render command dispatcher.
// Render-thread only. Counters are only collected while enabled.
// Indirect draws count the recorded commands (maximum draw count), not the
// GPU-generated draws; instances and triangles are unknown for them.
class RenderStatistics {
public:
	class StageScope {
	public:
		StageScope() = default;
		~StageScope();
		StageScope(const StageScope&) = delete;
		StageScope& operator=(const StageScope&) = delete;
		StageScope(StageScope&& other) noexcept;
		StageScope& operator=(StageScope&&) = delete;

	private:
		friend class RenderStatistics;
		explicit StageScope(RenderStatistics* owner);
		RenderStatistics* mOwner = nullptr;
	};

	static RenderStatistics& Get();

	void setEnabled(bool enabled);
	bool isEnabled() const;
	// Closes the current frame and accumulates it into the summary.
	void beginFrame();
	StageScope measureStage(std::string_view name);
	// Clears the summary and the last completed frame.
	void reset();

	void setPrimitiveMode(PrimitiveMode mode);
	void recordDraw(std::uint32_t elementCount, std::uint32_t instanceCount);
	void recordDraw(std::uint32_t elementCount, std::uint32_t instanceCount, PrimitiveMode mode);
	void recordIndirectDraw(std::uint32_t maximumDrawCount);
	void recordDispatch();
	void recordVirtualGeometry(
		std::uint64_t clusterInstances, std::uint64_t fallbackInstances, std::uint64_t visibleClusters, std::uint64_t visibleTriangles);

	const RenderFrameStatistics& lastFrame() const;
	// Sums over all frames since reset(); divide by Frames for per-frame means.
	const RenderFrameStatistics& summary() const;
	void print(std::ostream& output) const;

private:
	void endStage();
	RenderDrawCounters& current();
	std::size_t stageIndex(std::string_view name);

	bool mEnabled = false;
	bool mFrameOpen = false;
	PrimitiveMode mPrimitiveMode = PrimitiveMode::TRIANGLES;
	RenderFrameStatistics mCurrent = {};
	RenderFrameStatistics mLastFrame = {};
	RenderFrameStatistics mSummary = {};
	std::vector<std::size_t> mStageStack = {};
	RenderDrawCounters mUnscoped = {};
};

std::uint64_t TrianglesForPrimitive(PrimitiveMode mode, std::uint32_t elementCount);
}
