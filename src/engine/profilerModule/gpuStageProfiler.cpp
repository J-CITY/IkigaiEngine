#include "gpuStageProfiler.h"
#include "coreModule/graphicsWrapper.hpp"
#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/render.h"
#include "resourceModule/serviceManager.h"
#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <map>
#include <utility>

namespace IKIGAI::PROFILER {
namespace {
	constexpr std::size_t MaximumQueryPairs = 2048;
	constexpr std::size_t MaximumStages = 512;
	constexpr std::size_t MaximumRetainedSamples = 4096;
	constexpr std::uint64_t MinimumFrameDelay = 3;
	void writeTimestamp(RENDER::DriverInterface* driver, uint32_t index) {
		if (RESOURCES::ServiceManager::Check<RENDER::Renderer>()) {
			RESOURCES::ServiceManager::Get<RENDER::Renderer>().execute([driver, index] { driver->writeTimestamp(index); });
		}
		else {
			driver->writeTimestamp(index);
		}
	}
	bool glSupported() {
#if defined(OPENGL_BACKEND) && !defined(USING_GLES)
		if (!RENDER::DriverInterface::Get() || RENDER::DriverInterface::settings.backend != RENDER::RenderSettings::Backend::OPENGL) {
			return false;
		}
#if !defined(__APPLE__)
		if (!glQueryCounter || !glGetQueryObjectui64v) {
			return false;
		}
#endif
		GLint bits = 0;
		glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
		return bits >= 64;
#else
		return false;
#endif
	}
	// Vulkan/DX12 path: DriverInterface timestamp queries (gpuFeatures.h contract).
	RENDER::DriverInterface* timestampDriver() {
		auto* driver = RENDER::DriverInterface::Get();
		if (!driver || RENDER::DriverInterface::settings.backend == RENDER::RenderSettings::Backend::OPENGL) {
			return nullptr;
		}
		const auto features = driver->gpuFeatures();
		if (!features.TimestampQueries || features.TimestampPeriodNanoseconds <= 0.0 || driver->timestampQueryCapacity() < 2) {
			return nullptr;
		}
		return driver;
	}
}
struct GpuStageProfiler::Impl {
	struct Query {
		std::array<unsigned, 2> Ids = {};
		std::string Name = {};
		std::uint64_t Frame = 0;
		std::uint64_t Generation = 0;
		bool Ended = false;
	};
	struct Statistics {
		std::uint64_t Count = 0;
		double Sum = 0.0;
		double Minimum = std::numeric_limits<double>::max();
		double Maximum = 0.0;
		std::vector<double> Recent = {};
	};
	bool Supported = false;
	bool Enabled = false;
	RENDER::DriverInterface* Driver = nullptr;
	double TimestampPeriodNanoseconds = 0.0;
	std::size_t QueryPairCapacity = MaximumQueryPairs;
	std::vector<std::uint64_t> Ticks = {};
	void record(const std::string& name, double milliseconds);
	std::uint64_t Frame = 0;
	std::uint64_t Generation = 0;
	std::uint64_t Dropped = 0;
	std::vector<Query> Queries = {};
	std::deque<std::size_t> Pending = {};
	std::vector<std::size_t> Free = {};
	std::map<std::string, Statistics, std::less<>> Stages = {};
};
void GpuStageProfiler::Impl::record(const std::string& name, double milliseconds) {
	if (!Stages.contains(name) && Stages.size() >= MaximumStages) {
		++Dropped;
		return;
	}
	auto& statistics = Stages[name];
	statistics.Sum += milliseconds;
	statistics.Minimum = std::min(statistics.Minimum, milliseconds);
	statistics.Maximum = std::max(statistics.Maximum, milliseconds);
	if (statistics.Recent.size() < MaximumRetainedSamples) {
		statistics.Recent.push_back(milliseconds);
	}
	else {
		statistics.Recent[statistics.Count % MaximumRetainedSamples] = milliseconds;
	}
	++statistics.Count;
}
GpuStageProfiler::Scope::Scope() = default;
GpuStageProfiler::Scope::Scope(GpuStageProfiler* owner, std::size_t slot)
	: mOwner(owner)
	, mSlot(slot) { }
GpuStageProfiler::Scope::Scope(Scope&& other) noexcept
	: mOwner(std::exchange(other.mOwner, nullptr))
	, mSlot(other.mSlot) { }
GpuStageProfiler::Scope::~Scope() {
	if (mOwner) {
		mOwner->end(mSlot);
	}
}
GpuStageProfiler::GpuStageProfiler()
	: mImpl(std::make_unique<Impl>()) { }
GpuStageProfiler::~GpuStageProfiler() {
#if defined(OPENGL_BACKEND) && !defined(USING_GLES)
	// Driver timestamp slots own no GL query objects.
	for (const auto& query : mImpl->Queries) {
		if (!mImpl->Driver) {
			glDeleteQueries(2, query.Ids.data());
		}
	}
#endif
}
void GpuStageProfiler::setEnabled(bool enabled) {
	if (!mImpl->Queries.empty()) {
		// Keep the backend chosen when the first query was allocated.
		mImpl->Enabled = enabled && mImpl->Supported;
		return;
	}
	mImpl->Driver = timestampDriver();
	if (mImpl->Driver) {
		mImpl->TimestampPeriodNanoseconds = mImpl->Driver->gpuFeatures().TimestampPeriodNanoseconds;
		mImpl->QueryPairCapacity = std::min<std::size_t>(MaximumQueryPairs, mImpl->Driver->timestampQueryCapacity() / 2);
	}
	mImpl->Supported = mImpl->Driver || glSupported();
	mImpl->Enabled = enabled && mImpl->Supported;
}
void GpuStageProfiler::reset() {
	++mImpl->Generation;
	mImpl->Stages.clear();
	mImpl->Dropped = 0;
}
void GpuStageProfiler::beginFrame() {
	++mImpl->Frame;
	if (mImpl->Driver) {
		// Results are read in submission order, at least MinimumFrameDelay frames
		// after recording, and only once the driver reports them available.
		while (!mImpl->Pending.empty()) {
			const auto slot = mImpl->Pending.front();
			auto& query = mImpl->Queries[slot];
			if (!query.Ended || mImpl->Frame - query.Frame < MinimumFrameDelay) {
				break;
			}
			if (!mImpl->Driver->readTimestamps(static_cast<std::uint32_t>(slot * 2), 2, mImpl->Ticks) || mImpl->Ticks.size() < 2) {
				break;
			}
			if (query.Generation == mImpl->Generation) {
				if (mImpl->Ticks[1] >= mImpl->Ticks[0]) {
					mImpl->record(query.Name, double(mImpl->Ticks[1] - mImpl->Ticks[0]) * mImpl->TimestampPeriodNanoseconds * 1e-6);
				}
				else {
					++mImpl->Dropped;
				}
			}
			mImpl->Pending.pop_front();
			mImpl->Free.push_back(slot);
		}
		return;
	}
#if defined(OPENGL_BACKEND) && !defined(USING_GLES)
	while (!mImpl->Pending.empty()) {
		const auto slot = mImpl->Pending.front();
		auto& query = mImpl->Queries[slot];
		if (!query.Ended || mImpl->Frame - query.Frame < MinimumFrameDelay) {
			break;
		}
		GLint available = GL_FALSE;
		glGetQueryObjectiv(query.Ids[1], GL_QUERY_RESULT_AVAILABLE, &available);
		if (!available) {
			break;
		}
		if (query.Generation == mImpl->Generation) {
			// End availability guarantees the earlier start timestamp is ready.
			GLuint64 start = 0;
			GLuint64 finish = 0;
			glGetQueryObjectui64v(query.Ids[0], GL_QUERY_RESULT, &start);
			glGetQueryObjectui64v(query.Ids[1], GL_QUERY_RESULT, &finish);
			if (finish >= start) {
				mImpl->record(query.Name, double(finish - start) * 1e-6);
			}
			else {
				++mImpl->Dropped;
			}
		}
		mImpl->Pending.pop_front();
		mImpl->Free.push_back(slot);
	}
#endif
}
GpuStageProfiler::Scope GpuStageProfiler::measure(std::string_view name) {
	if (!mImpl->Enabled) {
		return {};
	}
	if (mImpl->Driver) {
		std::size_t slot = 0;
		if (!mImpl->Free.empty()) {
			slot = mImpl->Free.back();
			mImpl->Free.pop_back();
		}
		else if (mImpl->Queries.size() < mImpl->QueryPairCapacity) {
			slot = mImpl->Queries.size();
			mImpl->Queries.emplace_back();
		}
		else {
			++mImpl->Dropped;
			return {};
		}
		auto& query = mImpl->Queries[slot];
		query.Name = name;
		query.Frame = mImpl->Frame;
		query.Generation = mImpl->Generation;
		query.Ended = false;
		mImpl->Pending.push_back(slot);
		writeTimestamp(mImpl->Driver, static_cast<std::uint32_t>(slot * 2));
		return Scope(this, slot);
	}
#if defined(OPENGL_BACKEND) && !defined(USING_GLES)
	std::size_t slot = 0;
	if (!mImpl->Free.empty()) {
		slot = mImpl->Free.back();
		mImpl->Free.pop_back();
	}
	else if (mImpl->Queries.size() < MaximumQueryPairs) {
		slot = mImpl->Queries.size();
		mImpl->Queries.emplace_back();
		glGenQueries(2, mImpl->Queries.back().Ids.data());
	}
	else {
		++mImpl->Dropped;
		return {};
	}
	auto& query = mImpl->Queries[slot];
	query.Name = name;
	query.Frame = mImpl->Frame;
	query.Generation = mImpl->Generation;
	query.Ended = false;
	mImpl->Pending.push_back(slot);
	glQueryCounter(query.Ids[0], GL_TIMESTAMP);
	return Scope(this, slot);
#else
	return {};
#endif
}
void GpuStageProfiler::end(std::size_t slot) {
	if (mImpl->Driver) {
		writeTimestamp(mImpl->Driver, static_cast<std::uint32_t>(slot * 2 + 1));
		mImpl->Queries[slot].Ended = true;
		return;
	}
#if defined(OPENGL_BACKEND) && !defined(USING_GLES)
	auto& query = mImpl->Queries[slot];
	glQueryCounter(query.Ids[1], GL_TIMESTAMP);
	query.Ended = true;
#endif
}
GpuProfileSnapshot GpuStageProfiler::snapshot() const {
	GpuProfileSnapshot result;
	result.Supported = mImpl->Supported;
	result.Enabled = mImpl->Enabled;
	result.AllocatedQueryPairs = mImpl->Queries.size();
	result.DroppedSamples = mImpl->Dropped;
	for (const auto slot : mImpl->Pending) {
		result.PendingSamples += mImpl->Queries[slot].Generation == mImpl->Generation ? 1 : 0;
	}
	for (const auto& [name, statistics] : mImpl->Stages) {
		auto sorted = statistics.Recent;
		std::sort(sorted.begin(), sorted.end());
		const auto percentile = [&](double fraction) {
			const double index = fraction * double(sorted.size() - 1);
			const auto lower = std::size_t(index);
			const auto upper = std::min(lower + 1, sorted.size() - 1);
			return sorted[lower] + (sorted[upper] - sorted[lower]) * (index - double(lower));
		};
		result.Stages.push_back({ name, statistics.Count, sorted.size(), statistics.Sum / double(statistics.Count), statistics.Minimum,
			statistics.Maximum, percentile(.5), percentile(.95) });
	}
	return result;
}
}
