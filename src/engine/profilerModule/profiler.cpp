#include "profiler.h"
#include "profilerReport.h"
#include "profilerSpy.h"
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>

using namespace IKIGAI;
using namespace IKIGAI::PROFILER;

std::atomic<bool> Profiler::__ENABLED { false };
std::mutex Profiler::__SAVE_MUTEX;
std::unordered_map<std::string, double> Profiler::__ELPASED_HISTORY;
std::unordered_map<std::string, uint64_t> Profiler::__CALLS_COUNTER;
std::unordered_map<std::thread::id, int> Profiler::__WORKING_THREADS_LEVEL;
std::unordered_map<std::thread::id, std::vector<ProfilerReport::Node>> Profiler::__WORKING_THREADS_CALLS;
uint32_t Profiler::__ELAPSED_FRAMES;

Profiler::Profiler() {
	mLastTime = std::chrono::steady_clock::now();
	__ENABLED.store(false);
}

ProfilerReport Profiler::generateReportUnlocked() const {
	ProfilerReport report;

	if (__ELAPSED_FRAMES == 0) {
		return report;
	}

	std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - mLastTime;

	report.mFrames = __ELAPSED_FRAMES;
	report.mTime = elapsed.count();

	std::multimap<double, std::string, std::greater<double>> sortedHistory;

	for (auto& data : __ELPASED_HISTORY) {
		sortedHistory.insert(std::pair<double, std::string>(data.second, data.first));
	}
	for (auto& data : sortedHistory) {
		report.mActions.push_back({ data.second, data.first, (data.first / elapsed.count()) * 100.0, __CALLS_COUNTER[data.second] });
	}
	report.mCallsTree = __WORKING_THREADS_CALLS;
	return report;
}

ProfilerReport Profiler::GenerateReport() {
	std::lock_guard<std::mutex> lock(__SAVE_MUTEX);
	return generateReportUnlocked();
}

void Profiler::pushReportHistoryUnlocked(ProfilerReport report, float duration) {
	if (__REPORT_HISTORY.size() < kReportHistoryCapacity) {
		__REPORT_HISTORY.push_back(std::move(report));
		__REPORT_HISTORY_DURATIONS.push_back(duration);
		return;
	}
	__REPORT_HISTORY[__REPORT_HISTORY_NEXT] = std::move(report);
	__REPORT_HISTORY_DURATIONS[__REPORT_HISTORY_NEXT] = duration;
	__REPORT_HISTORY_NEXT = (__REPORT_HISTORY_NEXT + 1) % kReportHistoryCapacity;
}

std::vector<ProfilerReport> Profiler::copyReportHistoryUnlocked() {
	const auto count = __REPORT_HISTORY.size();
	std::vector<ProfilerReport> ordered;
	ordered.reserve(count);
	for (std::size_t i = 0; i < count; ++i) {
		const auto index = static_cast<std::size_t>((static_cast<std::size_t>(__REPORT_HISTORY_NEXT) + i) % kReportHistoryCapacity);
		ordered.push_back(__REPORT_HISTORY[index]);
	}
	return ordered;
}

std::vector<float> Profiler::copyReportHistoryDurationsUnlocked() {
	const auto count = __REPORT_HISTORY_DURATIONS.size();
	std::vector<float> ordered;
	ordered.reserve(count);
	for (std::size_t i = 0; i < count; ++i) {
		const auto index = static_cast<std::size_t>((static_cast<std::size_t>(__REPORT_HISTORY_NEXT) + i) % kReportHistoryCapacity);
		ordered.push_back(__REPORT_HISTORY_DURATIONS[index]);
	}
	return ordered;
}

void Profiler::ClearHistory() {
	std::lock_guard<std::mutex> lock(__SAVE_MUTEX);
	__ELPASED_HISTORY.clear();
	__CALLS_COUNTER.clear();
	__WORKING_THREADS_LEVEL.clear();
	__WORKING_THREADS_CALLS.clear();
	__ELAPSED_FRAMES = 0;
	mLastTime = std::chrono::steady_clock::now();
	__REPORT_HISTORY.clear();
	__REPORT_HISTORY_DURATIONS.clear();
	__REPORT_HISTORY_NEXT = 0;
}

void Profiler::Update(float p_deltaTime) {
	std::lock_guard<std::mutex> lock(__SAVE_MUTEX);
	if (!__ENABLED.load()) {
		return;
	}
	__WORKING_THREADS_CALLS.clear();
	mFrameStart = std::chrono::steady_clock::now();
	__ELAPSED_FRAMES++;
}

void Profiler::UpdateEnd() {
	std::lock_guard<std::mutex> lock(__SAVE_MUTEX);
	if (!__ENABLED.load()) {
		return;
	}
	const auto duration = static_cast<float>(std::chrono::duration<double>(std::chrono::steady_clock::now() - mFrameStart).count());
	pushReportHistoryUnlocked(generateReportUnlocked(), duration);
}

void Profiler::Save(ProfilerSpy& p_spy) {
	std::lock_guard<std::mutex> lock(__SAVE_MUTEX);

	auto delta = std::chrono::duration<double>(p_spy.mEnd - p_spy.mStart).count();

	if (__ELPASED_HISTORY.find(p_spy.mName) != __ELPASED_HISTORY.end()) {
		__ELPASED_HISTORY[p_spy.mName] += delta;
	}
	else {
		__ELPASED_HISTORY[p_spy.mName] = delta;
	}

	if (__CALLS_COUNTER.find(p_spy.mName) != __CALLS_COUNTER.end()) {
		__CALLS_COUNTER[p_spy.mName]++;
	}
	else {
		__CALLS_COUNTER[p_spy.mName] = 1;
	}
	std::chrono::duration<double> start = mFrameStart - p_spy.mStart;
	__WORKING_THREADS_CALLS[std::this_thread::get_id()].push_back(
		{ p_spy.mName, start.count(), delta, __WORKING_THREADS_LEVEL[std::this_thread::get_id()] });
}

bool Profiler::IsEnabled() {
	return __ENABLED.load();
}

void Profiler::ToggleEnable() {
	auto enabled = __ENABLED.load();
	while (!__ENABLED.compare_exchange_weak(enabled, !enabled)) { }
}

void Profiler::Enable() {
	__ENABLED.store(true);
}

void Profiler::Disable() {
	__ENABLED.store(false);
}

void Profiler::IncreaseLevel() {
	std::lock_guard<std::mutex> lock(__SAVE_MUTEX);
	if (__WORKING_THREADS_LEVEL.count(std::this_thread::get_id()) == 0) {
		__WORKING_THREADS_LEVEL[std::this_thread::get_id()] = 0;
		__WORKING_THREADS_CALLS[std::this_thread::get_id()] = {};
	}
	else {
		__WORKING_THREADS_LEVEL[std::this_thread::get_id()]++;
	}
}

void Profiler::DecreaseLevel() {
	std::lock_guard<std::mutex> lock(__SAVE_MUTEX);
	if (__WORKING_THREADS_LEVEL.count(std::this_thread::get_id()) == 0) {
		throw std::logic_error("Profiler::DecreaseLevel called without a matching IncreaseLevel");
	}
	__WORKING_THREADS_LEVEL[std::this_thread::get_id()]--;
}

std::vector<ProfilerReport> Profiler::getReportHistory() {
	std::lock_guard<std::mutex> lock(__SAVE_MUTEX);
	return copyReportHistoryUnlocked();
}

std::vector<float> Profiler::getReportHistoryDurations() {
	std::lock_guard<std::mutex> lock(__SAVE_MUTEX);
	return copyReportHistoryDurationsUnlocked();
}
