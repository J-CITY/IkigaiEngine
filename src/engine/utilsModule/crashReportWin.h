#pragma once

#include <cstdint>

namespace IKIGAI::UTILS::CRASH {

inline constexpr wchar_t ReporterExecutable[] = L"IkigaiCrashReporter.exe";
inline constexpr unsigned int FatalExceptionCode = 0xe1494b01;
inline constexpr std::uint32_t RequestVersion = 1;

// Read by the reporter using ReadProcessMemory. The exception pointers also
// belong to the target, and remain live until the reporter exits.
struct CrashRequest {
	std::uint32_t version = RequestVersion;
	std::uintptr_t exceptionPointers = 0;
	std::uint32_t exceptionCode = 0;
	std::uint32_t threadId = 0;
	char configuration[64] {};
	char revision[80] {};
	char reason[1024] {};
};

}
