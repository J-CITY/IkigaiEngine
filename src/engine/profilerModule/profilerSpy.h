#pragma once

#include <chrono>
#include <memory>
#include <string>

#ifndef USE_PROFILER
#define USE_PROFILER 1
#endif

#define PROFILER_EVENT()                                                                                                                             \
	std::unique_ptr<IKIGAI::PROFILER::ProfilerSpy> __profiler_spy__                                                                                  \
		= IKIGAI::PROFILER::Profiler::IsEnabled() ? std::make_unique<IKIGAI::PROFILER::ProfilerSpy>(__FUNCTION__) : nullptr

namespace IKIGAI::PROFILER {
struct ProfilerSpy {
#if USE_PROFILER
	ProfilerSpy(const std::string& p_name);
	~ProfilerSpy();
#else
	explicit ProfilerSpy(const std::string& name)
		: mName(name)
		, mStart(std::chrono::steady_clock::now()) { }
	~ProfilerSpy() = default;
#endif

	const std::string mName;
	const unsigned mThreadId = 0;
	const std::chrono::steady_clock::time_point mStart;
	std::chrono::steady_clock::time_point mEnd;
};
}
