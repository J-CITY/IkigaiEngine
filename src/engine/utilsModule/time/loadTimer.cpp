#include "loadTimer.h"

#include <iostream>
#include <utility>

namespace IKIGAI::UTILS {
LoadTimer::LoadTimer(const char* stage, bool enabled, std::string path)
	: mStage(stage)
	, mPath(std::move(path))
	, mEnabled(enabled)
	, mStart(enabled ? Clock::now() : Clock::time_point {}) { }

LoadTimer::~LoadTimer() {
	if (mEnabled) {
		std::cout << "LOAD_TIMING stage=" << mStage << " ms=" << std::chrono::duration<double, std::milli>(Clock::now() - mStart).count()
				  << " path=" << mPath << std::endl;
	}
}
}
