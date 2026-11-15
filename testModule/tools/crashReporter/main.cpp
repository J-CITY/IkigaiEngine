#include "utilsModule/crashReportWin.h"

// clang-format off
#include <windows.h>
#include <dbghelp.h>
// clang-format on

#include <charconv>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using namespace IKIGAI::UTILS::CRASH;
namespace fs = std::filesystem;

std::uintptr_t parseAddress(std::string_view argument) {
	std::uintptr_t address = 0;
	const auto* end = argument.data() + argument.size();
	const auto result = std::from_chars(argument.data(), end, address, 16);
	return result.ec == std::errc {} && result.ptr == end ? address : 0;
}

std::wstring toWide(std::string_view text) {
	if (text.empty()) {
		return {};
	}
	const auto size = static_cast<int>(text.size());
	const int wideSize = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
	std::wstring result(wideSize, L'\0');
	if (wideSize > 0) {
		MultiByteToWideChar(CP_UTF8, 0, text.data(), size, result.data(), wideSize);
	}
	return result;
}

std::wstring metadata(HANDLE process, DWORD processId, const CrashRequest& request) {
	wchar_t executable[32768] {};
	DWORD length = static_cast<DWORD>(std::size(executable));
	const bool haveExecutable = QueryFullProcessImageNameW(process, 0, executable, &length);

	std::wostringstream text;
	text << L"Executable: " << (haveExecutable ? executable : L"unknown") << L"\nBuild configuration: " << toWide(request.configuration)
		 << L"\nGit revision at configure time: " << toWide(request.revision) << L"\nProcess: " << processId << L"\nThread: " << request.threadId
		 << L"\nException: 0x" << std::hex << request.exceptionCode << L"\nReason: " << toWide(request.reason) << L'\n';
	return text.str();
}

HANDLE openDump(const fs::path& root, const wchar_t* name, fs::path& finalPath, fs::path& temporaryPath) {
	if (!root.is_absolute()) {
		return INVALID_HANDLE_VALUE;
	}
	const auto directory = root / L"IkigaiEngine" / L"crashes";
	std::error_code error;
	fs::create_directories(directory, error);
	if (error) {
		return INVALID_HANDLE_VALUE;
	}

	finalPath = directory / name;
	temporaryPath = finalPath;
	temporaryPath += L".tmp";
	return CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool writeDump(HANDLE process, const CrashRequest& request) {
	const DWORD processId = GetProcessId(process);
	auto comment = metadata(process, processId, request);
	SYSTEMTIME time {};
	GetSystemTime(&time);
	wchar_t name[128] {};
	std::swprintf(name, std::size(name), L"Ikigai-%04u%02u%02u-%02u%02u%02u-%03uZ-%lu.dmp", static_cast<unsigned int>(time.wYear),
		static_cast<unsigned int>(time.wMonth), static_cast<unsigned int>(time.wDay), static_cast<unsigned int>(time.wHour),
		static_cast<unsigned int>(time.wMinute), static_cast<unsigned int>(time.wSecond), static_cast<unsigned int>(time.wMilliseconds), processId);

	fs::path finalPath;
	fs::path temporaryPath;
	wchar_t localData[32768] {};
	const DWORD localLength = GetEnvironmentVariableW(L"LOCALAPPDATA", localData, static_cast<DWORD>(std::size(localData)));
	HANDLE file = INVALID_HANDLE_VALUE;
	if (localLength > 0 && localLength < std::size(localData)) {
		file = openDump(localData, name, finalPath, temporaryPath);
	}
	if (file == INVALID_HANDLE_VALUE) {
		wchar_t temp[32768] {};
		const DWORD tempLength = GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
		if (tempLength > 0 && tempLength < std::size(temp)) {
			file = openDump(temp, name, finalPath, temporaryPath);
		}
	}
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}

	MINIDUMP_EXCEPTION_INFORMATION exception {};
	exception.ThreadId = request.threadId;
	exception.ExceptionPointers = reinterpret_cast<EXCEPTION_POINTERS*>(request.exceptionPointers);
	exception.ClientPointers = TRUE;

	MINIDUMP_USER_STREAM stream {};
	stream.Type = CommentStreamW;
	stream.BufferSize = static_cast<ULONG>((comment.size() + 1) * sizeof(wchar_t));
	stream.Buffer = comment.data();
	MINIDUMP_USER_STREAM_INFORMATION streams {};
	streams.UserStreamCount = 1;
	streams.UserStreamArray = &stream;

	const auto dumpType = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
	const bool dumped = MiniDumpWriteDump(process, processId, file, dumpType, exception.ExceptionPointers ? &exception : nullptr, &streams, nullptr);
	CloseHandle(file);
	if (dumped && MoveFileW(temporaryPath.c_str(), finalPath.c_str())) {
		return true;
	}
	DeleteFileW(temporaryPath.c_str());
	return false;
}

int runReporter(int argc, char* argv[]) {
	if (argc != 4) {
		return 2;
	}
	const auto process = reinterpret_cast<HANDLE>(parseAddress(argv[1]));
	const auto event = reinterpret_cast<HANDLE>(parseAddress(argv[2]));
	const auto* address = reinterpret_cast<const void*>(parseAddress(argv[3]));
	if (!process || !event || !address) {
		return 2;
	}

	const HANDLE handles[] = { process, event };
	const DWORD signalled = WaitForMultipleObjects(static_cast<DWORD>(std::size(handles)), handles, FALSE, INFINITE);
	if (signalled == WAIT_OBJECT_0) {
		return 0; // Engine exited normally.
	}
	if (signalled != WAIT_OBJECT_0 + 1) {
		return 2;
	}

	CrashRequest request {};
	if (!ReadProcessMemory(process, address, &request, sizeof(request), nullptr) || request.version != RequestVersion) {
		return 2;
	}
	request.configuration[sizeof(request.configuration) - 1] = '\0';
	request.revision[sizeof(request.revision) - 1] = '\0';
	request.reason[sizeof(request.reason) - 1] = '\0';
	return writeDump(process, request) ? 0 : 1;
}

}

int main(int argc, char* argv[]) {
	try {
		return runReporter(argc, argv);
	} catch (...) {
		return 2;
	}
}
