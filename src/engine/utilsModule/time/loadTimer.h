#pragma once
#include <chrono>
#include <string>

namespace IKIGAI::UTILS {
// Opt-in timings include suspension time for asynchronous loading operations.
class LoadTimer {
	using Clock = std::chrono::steady_clock;
	const char* mStage;
	std::string mPath;
	bool mEnabled;
	Clock::time_point mStart;

public:
	LoadTimer(const char* stage, bool enabled, std::string path = {});
	~LoadTimer();
};
}
