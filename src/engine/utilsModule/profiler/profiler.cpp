#include "profiler.h"
#include <map>
#include <string>
#include "profilerSpy.h"
#include "profilerReport.h"

using namespace IKIGAI;
using namespace IKIGAI::PROFILER;

bool Profiler::__ENABLED = false;
std::mutex Profiler::__SAVE_MUTEX;
std::unordered_map<std::string, double> Profiler::__ELPASED_HISTORY;
std::unordered_map<std::string, uint64_t> Profiler::__CALLS_COUNTER;
std::unordered_map<std::thread::id, int> Profiler::__WORKING_THREADS_LEVEL;
std::unordered_map<std::thread::id, std::vector<ProfilerReport::Node>> Profiler::__WORKING_THREADS_CALLS;
uint32_t Profiler::__ELAPSED_FRAMES;

Profiler::Profiler() {
	mLastTime = std::chrono::steady_clock::now();
	__ENABLED = false;
}

ProfilerReport Profiler::GenerateReport() {
	ProfilerReport report;

	if (__ELAPSED_FRAMES == 0)
		return report;

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

void Profiler::ClearHistory() {
	__ELPASED_HISTORY.clear();
	__CALLS_COUNTER.clear();
	__WORKING_THREADS_LEVEL.clear();
	__WORKING_THREADS_CALLS.clear();
	__ELAPSED_FRAMES = 0;
	mLastTime = std::chrono::steady_clock::now();
	__REPORT_HISTORY.clear();
}

void Profiler::Update(float p_deltaTime) {
	if (IsEnabled()) {
		__WORKING_THREADS_CALLS.clear();
		mFrameStart = std::chrono::steady_clock::now();
		__ELAPSED_FRAMES++;
	}
}

void Profiler::UpdateEnd() {
	if (IsEnabled()) {
		__REPORT_HISTORY.push_back(GenerateReport());
		std::chrono::duration<double> dur = std::chrono::steady_clock::now() - mFrameStart;
		__REPORT_HISTORY_DURATIONS.push_back(dur.count());
	}
}

void Profiler::Save(ProfilerSpy& p_spy) {
	__SAVE_MUTEX.lock();

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
	__WORKING_THREADS_CALLS[std::this_thread::get_id()].push_back({ p_spy.mName , start.count(), delta,  __WORKING_THREADS_LEVEL[std::this_thread::get_id()]});

	__SAVE_MUTEX.unlock();
}

bool Profiler::IsEnabled() {
	return __ENABLED;
}

void Profiler::ToggleEnable() {
	__ENABLED = !__ENABLED;
}

void Profiler::Enable() {
	__ENABLED = true;
}

void Profiler::Disable() {
	__ENABLED = false;
}

void Profiler::IncreaseLevel() {
	__SAVE_MUTEX.lock();
	if (__WORKING_THREADS_LEVEL.count(std::this_thread::get_id()) == 0) {
		__WORKING_THREADS_LEVEL[std::this_thread::get_id()] = 0;
		__WORKING_THREADS_CALLS[std::this_thread::get_id()] = {};
	}
	else {
		__WORKING_THREADS_LEVEL[std::this_thread::get_id()]++;
	}
	__SAVE_MUTEX.unlock();
}

void Profiler::DecreaseLevel() {
	__SAVE_MUTEX.lock();
	if (__WORKING_THREADS_LEVEL.count(std::this_thread::get_id()) == 0) {
		throw;
	}
	else {
		__WORKING_THREADS_LEVEL[std::this_thread::get_id()]--;
	}
	__SAVE_MUTEX.unlock();
}

const std::vector<ProfilerReport>& Profiler::getReportHistory() {
	return __REPORT_HISTORY;
}

const std::vector<float>& Profiler::getReportHistoryDurations() {
	return __REPORT_HISTORY_DURATIONS;
}
