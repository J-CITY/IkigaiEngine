#include "platform.hpp"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

using namespace IKIGAI;

Platform IKIGAI::GetCurrentPlatform() {
#if defined(__EMSCRIPTEN__)
	return Platform::EMSCRIPT;
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
	return Platform::IOS;
#else
	return Platform::MAC;
#endif
#elif __ANDROID__
	return Platform::ANDROIDOS;
#elif WIN32
	return Platform::WINDOWS;
#elif __linux__
	return Platform::LINUX;
#endif
}
