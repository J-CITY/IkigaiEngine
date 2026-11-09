#include "renderStatistics.h"
#include "renderModule/render.h"
#include "resourceModule/serviceManager.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <optional>
#include <ostream>
#include <utility>

namespace IKIGAI::RENDER {
namespace {
	constexpr std::string_view UnscopedStageName = "(unscoped)";
	std::optional<size_t> playbackStage {};
	void recordStageMarker(size_t index) {
		if (RESOURCES::ServiceManager::Check<Renderer>()) {
			RESOURCES::ServiceManager::Get<Renderer>().execute([index] { playbackStage = index; });
		}
	}

	void Accumulate(RenderDrawCounters& target, const RenderDrawCounters& source) {
		target.DrawCalls += source.DrawCalls;
		target.IndirectDraws += source.IndirectDraws;
		target.IndirectSubmissions += source.IndirectSubmissions;
		target.Instances += source.Instances;
		target.Triangles += source.Triangles;
		target.Dispatches += source.Dispatches;
	}

	bool IsEmpty(const RenderDrawCounters& counters) {
		return counters.DrawCalls == 0 && counters.IndirectDraws == 0 && counters.Dispatches == 0;
	}

	void PrintCounters(std::ostream& output, const RenderDrawCounters& counters, double frames) {
		const auto flags = output.flags();
		const auto precision = output.precision();
		output << std::fixed << std::setprecision(2);
		output << " draws=" << double(counters.DrawCalls) / frames << " indirect=" << double(counters.IndirectDraws) / frames
			   << " instances=" << double(counters.Instances) / frames << " triangles=" << double(counters.Triangles) / frames
			   << " dispatches=" << double(counters.Dispatches) / frames;
		output.flags(flags);
		output.precision(precision);
	}
}

std::uint64_t TrianglesForPrimitive(PrimitiveMode mode, std::uint32_t elementCount) {
	switch (mode) {
	case PrimitiveMode::TRIANGLES:
		return elementCount / 3;
	case PrimitiveMode::TRIANGLE_STRIP:
	case PrimitiveMode::TRIANGLE_FAN:
		return elementCount >= 3 ? elementCount - 2 : 0;
	case PrimitiveMode::TRIANGLES_ADJACENCY:
		return elementCount / 6;
	case PrimitiveMode::TRIANGLE_STRIP_ADJACENCY:
		return elementCount >= 6 ? (elementCount - 4) / 2 : 0;
	default:
		return 0;
	}
}

RenderStatistics::StageScope::StageScope(RenderStatistics* owner)
	: mOwner(owner) { }

RenderStatistics::StageScope::StageScope(StageScope&& other) noexcept
	: mOwner(std::exchange(other.mOwner, nullptr)) { }

RenderStatistics::StageScope::~StageScope() {
	if (mOwner) {
		mOwner->endStage();
	}
}

RenderStatistics& RenderStatistics::Get() {
	static RenderStatistics instance;
	return instance;
}

void RenderStatistics::setEnabled(bool enabled) {
	if (enabled != mEnabled) {
		mEnabled = enabled;
		mFrameOpen = false;
		mCurrent = {};
		mStageStack.clear();
		mUnscoped = {};
	}
}

bool RenderStatistics::isEnabled() const {
	return mEnabled;
}

void RenderStatistics::beginFrame() {
	playbackStage.reset();
	if (!mEnabled) {
		return;
	}
	if (mFrameOpen) {
		if (!IsEmpty(mUnscoped)) {
			RenderStageStatistics unscoped;
			unscoped.Name = UnscopedStageName;
			unscoped.Counters = mUnscoped;
			mCurrent.Stages.push_back(std::move(unscoped));
		}
		mCurrent.Frames = 1;
		for (auto& stage : mCurrent.Stages) {
			stage.Frames = 1;
		}
		++mSummary.Frames;
		Accumulate(mSummary.Total, mCurrent.Total);
		mSummary.VirtualGeometry.ClusterInstances += mCurrent.VirtualGeometry.ClusterInstances;
		mSummary.VirtualGeometry.FallbackInstances += mCurrent.VirtualGeometry.FallbackInstances;
		mSummary.VirtualGeometry.VisibleClusters += mCurrent.VirtualGeometry.VisibleClusters;
		mSummary.VirtualGeometry.VisibleTriangles += mCurrent.VirtualGeometry.VisibleTriangles;
		for (const auto& stage : mCurrent.Stages) {
			auto found = std::find_if(mSummary.Stages.begin(), mSummary.Stages.end(), [&](const auto& value) { return value.Name == stage.Name; });
			if (found == mSummary.Stages.end()) {
				mSummary.Stages.push_back({ stage.Name, 0, {} });
				found = std::prev(mSummary.Stages.end());
			}
			++found->Frames;
			Accumulate(found->Counters, stage.Counters);
		}
		mLastFrame = std::move(mCurrent);
	}
	mCurrent = {};
	mUnscoped = {};
	mStageStack.clear();
	mFrameOpen = true;
}

RenderStatistics::StageScope RenderStatistics::measureStage(std::string_view name) {
	if (!mEnabled || !mFrameOpen) {
		return {};
	}
	mStageStack.push_back(stageIndex(name));
	recordStageMarker(mStageStack.back());
	return StageScope(this);
}

void RenderStatistics::endStage() {
	if (!mStageStack.empty()) {
		mStageStack.pop_back();
		recordStageMarker(mStageStack.empty() ? std::numeric_limits<size_t>::max() : mStageStack.back());
	}
}

void RenderStatistics::reset() {
	mSummary = {};
	mLastFrame = {};
}

std::size_t RenderStatistics::stageIndex(std::string_view name) {
	const auto found = std::find_if(mCurrent.Stages.begin(), mCurrent.Stages.end(), [&](const auto& value) { return value.Name == name; });
	if (found != mCurrent.Stages.end()) {
		return std::size_t(found - mCurrent.Stages.begin());
	}
	mCurrent.Stages.push_back({ std::string(name), 0, {} });
	return mCurrent.Stages.size() - 1;
}

RenderDrawCounters& RenderStatistics::current() {
	if (playbackStage) {
		return *playbackStage < mCurrent.Stages.size() ? mCurrent.Stages[*playbackStage].Counters : mUnscoped;
	}
	return mStageStack.empty() ? mUnscoped : mCurrent.Stages[mStageStack.back()].Counters;
}

void RenderStatistics::setPrimitiveMode(PrimitiveMode mode) {
	mPrimitiveMode = mode;
}

void RenderStatistics::recordDraw(std::uint32_t elementCount, std::uint32_t instanceCount) {
	recordDraw(elementCount, instanceCount, mPrimitiveMode);
}

void RenderStatistics::recordDraw(std::uint32_t elementCount, std::uint32_t instanceCount, PrimitiveMode mode) {
	if (!mEnabled || !mFrameOpen || elementCount == 0 || instanceCount == 0) {
		return;
	}
	const auto triangles = TrianglesForPrimitive(mode, elementCount) * instanceCount;
	for (auto* counters : { &current(), &mCurrent.Total }) {
		++counters->DrawCalls;
		counters->Instances += instanceCount;
		counters->Triangles += triangles;
	}
}

void RenderStatistics::recordIndirectDraw(std::uint32_t maximumDrawCount) {
	if (!mEnabled || !mFrameOpen) {
		return;
	}
	for (auto* counters : { &current(), &mCurrent.Total }) {
		counters->IndirectDraws += maximumDrawCount;
		++counters->IndirectSubmissions;
	}
}

void RenderStatistics::recordDispatch() {
	if (!mEnabled || !mFrameOpen) {
		return;
	}
	for (auto* counters : { &current(), &mCurrent.Total }) {
		++counters->Dispatches;
	}
}

void RenderStatistics::recordVirtualGeometry(
	std::uint64_t clusterInstances, std::uint64_t fallbackInstances, std::uint64_t visibleClusters, std::uint64_t visibleTriangles) {
	if (!mEnabled || !mFrameOpen) {
		return;
	}
	mCurrent.VirtualGeometry.ClusterInstances += clusterInstances;
	mCurrent.VirtualGeometry.FallbackInstances += fallbackInstances;
	mCurrent.VirtualGeometry.VisibleClusters += visibleClusters;
	mCurrent.VirtualGeometry.VisibleTriangles += visibleTriangles;
}

const RenderFrameStatistics& RenderStatistics::lastFrame() const {
	return mLastFrame;
}

const RenderFrameStatistics& RenderStatistics::summary() const {
	return mSummary;
}

void RenderStatistics::print(std::ostream& output) const {
	const double frames = double(std::max<std::uint64_t>(1, mSummary.Frames));
	output << "RENDER_STATS frames=" << mSummary.Frames;
	PrintCounters(output, mSummary.Total, frames);
	output << '\n';
	const auto& virtualGeometry = mSummary.VirtualGeometry;
	if (virtualGeometry.ClusterInstances || virtualGeometry.FallbackInstances) {
		const auto flags = output.flags();
		const auto precision = output.precision();
		output << std::fixed << std::setprecision(2) << "RENDER_STATS_VG clusterInstances=" << double(virtualGeometry.ClusterInstances) / frames
			   << " fallbackInstances=" << double(virtualGeometry.FallbackInstances) / frames
			   << " clusters=" << double(virtualGeometry.VisibleClusters) / frames
			   << " triangles=" << double(virtualGeometry.VisibleTriangles) / frames << '\n';
		output.flags(flags);
		output.precision(precision);
	}
	for (const auto& stage : mSummary.Stages) {
		// Per-stage values are means over the frames in which the stage ran.
		output << "RENDER_STATS_STAGE " << stage.Name << " frames=" << stage.Frames;
		PrintCounters(output, stage.Counters, double(std::max<std::uint64_t>(1, stage.Frames)));
		output << '\n';
	}
	output.flush();
}
}
