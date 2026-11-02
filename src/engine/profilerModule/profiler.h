#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>

#include "profilerReport.h"

namespace IKIGAI::PROFILER {
struct ProfilerSpy;

class Profiler final {
public:
	Profiler();
	ProfilerReport GenerateReport();

	void ClearHistory();
	void Update(float p_deltaTime);
	void UpdateEnd();
	static void Save(ProfilerSpy& p_spy);
	static bool IsEnabled();
	static void ToggleEnable();
	static void Enable();
	static void Disable();

	static void IncreaseLevel();
	static void DecreaseLevel();

	static std::vector<ProfilerReport> getReportHistory();
	static std::vector<float> getReportHistoryDurations();

private:
	static constexpr uint32_t kReportHistoryCapacity = 256;

	ProfilerReport generateReportUnlocked() const;
	static void pushReportHistoryUnlocked(ProfilerReport report, float duration);
	static std::vector<ProfilerReport> copyReportHistoryUnlocked();
	static std::vector<float> copyReportHistoryDurationsUnlocked();

	inline static std::chrono::steady_clock::time_point mFrameStart;
	std::chrono::steady_clock::time_point mLastTime;

	static std::atomic<bool> __ENABLED;
	static std::mutex __SAVE_MUTEX;
	static std::unordered_map<std::string, double> __ELPASED_HISTORY;
	static std::unordered_map<std::string, uint64_t> __CALLS_COUNTER;
	static std::unordered_map<std::thread::id, int> __WORKING_THREADS_LEVEL;
	static uint32_t __ELAPSED_FRAMES;

	static std::unordered_map<std::thread::id, std::vector<ProfilerReport::Node>> __WORKING_THREADS_CALLS;

	static inline std::vector<ProfilerReport> __REPORT_HISTORY;
	static inline std::vector<float> __REPORT_HISTORY_DURATIONS;
	static inline uint32_t __REPORT_HISTORY_NEXT = 0;
};
}
