#pragma once
#include <Windows.h>

namespace IKIGAI::RENDER {
// DXGI's presentation thread can send synchronous messages to the SDL HWND.
// Service these while waiting, without removing SDL's queued input events.
inline DWORD WaitWithWindowMessages(HANDLE event, DWORD milliseconds) {
	MSG message {};
	PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
	return MsgWaitForMultipleObjectsEx(1, &event, milliseconds, QS_SENDMESSAGE, MWMO_INPUTAVAILABLE);
}
}
