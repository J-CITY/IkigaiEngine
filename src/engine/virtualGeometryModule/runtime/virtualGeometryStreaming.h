#pragma once
#include "virtualGeometryPageFeedback.h"
#include "virtualGeometryResidency.h"
#include "virtualGeometrySelection.h"
#include "virtualMeshGpuPagePool.h"
#include "virtualMeshPageRequests.h"
#include <deque>
#include <memory>
#include <unordered_map>

namespace IKIGAI::VG {
class VirtualMeshAsset;
struct VirtualGeometryStreamingStatistics {
	VirtualGeometryPageFeedback::Statistics Feedback {};
	uint64_t ReadRequests = 0;
	uint64_t ReadBytes = 0; // Logical page payload bytes admitted to the I/O queue.
	uint64_t CompletedReadBytes = 0; // Successfully decoded payload bytes, including obsolete generations.
	uint64_t ReadFailures = 0; // Failed reads, excluding cancellations.
	uint64_t UploadBytes = 0; // Decoded VB+IB payload recorded for upload, excluding fence completion.
	uint64_t GpuRequests = 0;
	uint64_t StaleFeedback = 0;
	uint64_t DroppedFeedback = 0;
	uint64_t DeduplicatedRequests = 0;
	uint64_t CpuFeedbackFallbacks = 0;
	uint64_t PageMisses = 0;
	uint64_t Evictions = 0; // Optional pages retired for age within a live context.
	uint64_t ReadBytesHighWater = 0;
	uint64_t UploadBytesHighWater = 0;
	uint64_t StagingBytesHighWater = 0;
};

struct VirtualGeometryStreamingSettings {
	static constexpr uint64_t DefaultFrameBytes = 4u * 1024u * 1024u;
	static constexpr uint32_t DefaultFeedbackAge = 4;
	bool GpuFeedback = false;
	uint32_t MaxFeedbackAgeFrames = DefaultFeedbackAge;
	uint64_t MaxReadBytesPerFrame = DefaultFrameBytes;
	uint64_t MaxUploadBytesPerFrame = DefaultFrameBytes;
	VirtualMeshGpuPagePoolSettings Pool {};
	VirtualMeshPageRequestSettings Requests {};
	template <class C> static void serde(C& context, VirtualGeometryStreamingSettings& value) {
		using serde::attribute::default_;
		serde::serde_struct(context, value)
			.field(&VirtualGeometryStreamingSettings::GpuFeedback, "GpuFeedback", default_ { false })
			.field(&VirtualGeometryStreamingSettings::MaxFeedbackAgeFrames, "MaxFeedbackAgeFrames", default_ { DefaultFeedbackAge })
			.field(&VirtualGeometryStreamingSettings::MaxReadBytesPerFrame, "MaxReadBytesPerFrame", default_ { DefaultFrameBytes })
			.field(&VirtualGeometryStreamingSettings::MaxUploadBytesPerFrame, "MaxUploadBytesPerFrame", default_ { DefaultFrameBytes })
			.field(&VirtualGeometryStreamingSettings::Pool, "Pool", default_ { VirtualMeshGpuPagePoolSettings {} })
			.field(&VirtualGeometryStreamingSettings::Requests, "Requests", default_ { VirtualMeshPageRequestSettings {} });
	}
};
struct VirtualGeometryStreamCut {
	VirtualGeometryResidency::CutSnapshot Cut {};
	std::unordered_map<uint32_t, VirtualMeshGpuPagePool::SlotLease> Pages {};
};
// Serial owner of the bounded I/O and GPU pools. Selection never waits for disk or GPU.
class VirtualGeometryStreaming final {
public:
	explicit VirtualGeometryStreaming(RENDER::Renderer& renderer, VirtualGeometryStreamingSettings settings = {});
	~VirtualGeometryStreaming();
	void beginFrame(RENDER::Renderer& renderer, uint64_t frame);
	std::shared_ptr<const VirtualGeometryStreamCut> select(RENDER::Renderer& renderer, const std::shared_ptr<VirtualMeshAsset>& asset,
		const VirtualGeometryInstanceTransform& transform, const VirtualGeometryView& view, float threshold, uint64_t instanceId = 0,
		uint64_t viewId = 0, PROFILER::GpuStageProfiler* profiler = nullptr);
	uint64_t gpuBytes() const;
	uint64_t stagingBytes() const;
	[[nodiscard]] VirtualGeometryStreamingStatistics statistics() const;

private:
	struct Context;
	struct ReadyUpload {
		VirtualGeometryResidency::Ticket Ticket {};
		std::shared_ptr<const VMeshPageData> Data {};
		uint64_t Bytes = 0;
	};
	std::deque<ReadyUpload> mReady {};
	uint64_t mReadyBytes = 0;
	uint32_t mRootSlots = 0;
	bool mRootPressure = false;
	VirtualGeometryStreamingSettings mSettings {};
	VirtualMeshGpuPagePool mPool;
	VirtualMeshPageRequests mRequests;
	VirtualGeometryPageFeedback mFeedback {};
	std::vector<VirtualGeometryPageFeedback::Batch> mFeedbackBatches {};
	VirtualGeometryStreamingStatistics mStatistics {};
	uint64_t mReadBytesThisFrame = 0;
	uint64_t mUploadBytesThisFrame = 0;
	std::unordered_map<const VirtualMeshAsset*, std::unique_ptr<Context>> mContexts {};
	uint64_t mNextGeneration = 1;
	uint64_t mFrame = 0;
	uint64_t mCompleted = 0;
};
}
