#include "crashReport.h"

#include <cstdlib>

#if defined(_WIN32)
#include "crashReportWin.h"
#include <windows.h>

#include <cstddef>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>

#ifndef IKIGAI_CRASH_BUILD_CONFIGURATION
#define IKIGAI_CRASH_BUILD_CONFIGURATION "unknown"
#endif
#ifndef IKIGAI_CRASH_BUILD_REVISION
#define IKIGAI_CRASH_BUILD_REVISION "unknown"
#endif

namespace {

using namespace IKIGAI::UTILS::CRASH;
using ScopedHandle = std::unique_ptr<void, decltype(&CloseHandle)>;

CrashRequest request { .configuration = IKIGAI_CRASH_BUILD_CONFIGURATION, .revision = IKIGAI_CRASH_BUILD_REVISION };
HANDLE crashEvent = nullptr;
HANDLE reporterProcess = nullptr;
volatile LONG installed = 0;
volatile LONG reporterReady = 0;
volatile LONG crashClaimed = 0;

constexpr DWORD ReporterTimeoutMs = 10000;

bool launchReporter(const std::filesystem::path& reporterPath, HANDLE process, HANDLE event) {
	SIZE_T attributeSize = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
	if (attributeSize == 0) {
		return false;
	}

	auto storage = std::make_unique<std::byte[]>(attributeSize);
	auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.get());
	if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize)) {
		return false;
	}

	HANDLE inheritedHandles[] = { process, event };
	const bool handlesReady
		= UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritedHandles, sizeof(inheritedHandles), nullptr, nullptr);
	STARTUPINFOEXW startup {};
	startup.StartupInfo.cb = sizeof(startup);
	startup.lpAttributeList = attributes;
	wchar_t commandLine[128] {};
	std::swprintf(commandLine, std::size(commandLine), L"IkigaiCrashReporter %llx %llx %llx",
		static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(process)),
		static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(event)),
		static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&request)));
	PROCESS_INFORMATION child {};
	const bool launched = handlesReady
		&& CreateProcessW(reporterPath.c_str(), commandLine, nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr,
			nullptr, &startup.StartupInfo, &child);
	DeleteProcThreadAttributeList(attributes);
	if (!launched) {
		return false;
	}

	CloseHandle(child.hThread);
	reporterProcess = child.hProcess;
	return true;
}

void startReporter() {
	wchar_t executable[32768] {};
	const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
	if (length == 0 || length == std::size(executable)) {
		return;
	}

	const auto reporterPath = std::filesystem::path(executable).parent_path() / ReporterExecutable;
	const auto currentProcess = GetCurrentProcess();
	HANDLE processHandle = nullptr;
	if (!DuplicateHandle(
			currentProcess, currentProcess, currentProcess, &processHandle, PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, TRUE, 0)) {
		return;
	}
	const ScopedHandle inheritedProcess(processHandle, CloseHandle);
	ScopedHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr), CloseHandle);
	HANDLE eventHandle = nullptr;
	if (!event || !DuplicateHandle(currentProcess, event.get(), currentProcess, &eventHandle, SYNCHRONIZE, TRUE, 0)) {
		return;
	}
	const ScopedHandle inheritedEvent(eventHandle, CloseHandle);

	if (launchReporter(reporterPath, inheritedProcess.get(), inheritedEvent.get())) {
		crashEvent = event.release();
		InterlockedExchange(&reporterReady, 1);
	}
}

[[noreturn]] void finishCrash(EXCEPTION_POINTERS* exception, const char* reason) noexcept {
	const DWORD code = exception ? exception->ExceptionRecord->ExceptionCode : FatalExceptionCode;
	DWORD reporterExitCode = 1;
	if (InterlockedCompareExchange(&reporterReady, 1, 1) == 1) {
		if (InterlockedCompareExchange(&crashClaimed, 1, 0) == 0) {
			request.exceptionPointers = reinterpret_cast<std::uintptr_t>(exception);
			request.exceptionCode = code;
			request.threadId = GetCurrentThreadId();
			for (std::size_t index = 0; reason && index + 1 < sizeof(request.reason) && reason[index]; ++index) {
				request.reason[index] = reason[index];
			}
			SetEvent(crashEvent);
		}
		if (WaitForSingleObject(reporterProcess, ReporterTimeoutMs) == WAIT_OBJECT_0) {
			GetExitCodeProcess(reporterProcess, &reporterExitCode);
		}
	}
	if (reporterExitCode != 0 && reason && IsDebuggerPresent()) {
		OutputDebugStringA(reason);
	}

	TerminateProcess(GetCurrentProcess(), code);
	std::abort();
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* exception) noexcept {
	finishCrash(exception, "Unhandled Windows exception");
}

}
#else
#include <cstring>
#include <unistd.h>
#endif

namespace IKIGAI::UTILS::CRASH {

void install() noexcept {
#if defined(_WIN32)
	if (InterlockedCompareExchange(&installed, 1, 0) != 0) {
		return;
	}

	ULONG stackGuarantee = 32 * 1024;
	SetThreadStackGuarantee(&stackGuarantee);
	SetUnhandledExceptionFilter(onUnhandledException);
	std::set_terminate([] { terminate("std::terminate"); });
	try {
		startReporter();
	} catch (...) {
		// Reporting is optional. An initialization failure must not stop startup.
	}
#endif
}

[[noreturn]] void terminate(const char* reason) noexcept {
#if defined(_WIN32)
	__try {
		RaiseException(FatalExceptionCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
	} __except (finishCrash(GetExceptionInformation(), reason), EXCEPTION_EXECUTE_HANDLER) { }
	finishCrash(nullptr, reason);
#else
	if (reason) {
		(void)::write(STDERR_FILENO, reason, std::strlen(reason));
		(void)::write(STDERR_FILENO, "\n", 1);
	}
	std::abort();
#endif
}

}
