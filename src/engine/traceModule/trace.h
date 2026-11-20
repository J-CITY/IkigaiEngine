#pragma once

#if defined(IKIGAI_ENABLE_TRACY)
#include <tracy/Tracy.hpp>

#define IKIGAI_PROFILE_SCOPE(name) ZoneScopedN(name)
#define IKIGAI_PROFILE_FRAME() FrameMark
#define IKIGAI_PROFILE_COUNTER(name, value)                                                                                                          \
	do {                                                                                                                                             \
		if (TracyIsConnected) {                                                                                                                      \
			TracyPlot(name, static_cast<double>(value));                                                                                             \
		}                                                                                                                                            \
	} while (false)
#define IKIGAI_PROFILE_ZONE_TEXT(text, size)                                                                                                         \
	do {                                                                                                                                             \
		if (ZoneIsActive) {                                                                                                                          \
			const auto ikigaiTraceTextSize = (size);                                                                                                 \
			constexpr auto maxTraceTextSize = 65534u;                                                                                                \
			ZoneText(text, ikigaiTraceTextSize < maxTraceTextSize ? ikigaiTraceTextSize : maxTraceTextSize);                                         \
		}                                                                                                                                            \
	} while (false)
#define IKIGAI_PROFILE_ZONE_VALUE(value)                                                                                                             \
	do {                                                                                                                                             \
		if (ZoneIsActive) {                                                                                                                          \
			ZoneValue(value);                                                                                                                        \
		}                                                                                                                                            \
	} while (false)
#else
#define IKIGAI_PROFILE_SCOPE(name) ((void)0)
#define IKIGAI_PROFILE_FRAME() ((void)0)
#define IKIGAI_PROFILE_COUNTER(name, value) ((void)0)
#define IKIGAI_PROFILE_ZONE_TEXT(text, size) ((void)0)
#define IKIGAI_PROFILE_ZONE_VALUE(value) ((void)0)
#endif

namespace IKIGAI::PROFILER::TRACE {
#if defined(IKIGAI_ENABLE_TRACY)
inline void setThreadName(const char* name) {
	tracy::SetThreadName(name);
}

[[nodiscard]] inline bool isConnected() {
	return TracyIsConnected;
}
#else
inline void setThreadName(const char*) { }

[[nodiscard]] constexpr bool isConnected() {
	return false;
}
#endif
}
