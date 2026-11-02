#pragma once

// Native process and Android app pointers without including a platform SDK.
// android_app stays incomplete; only the platform .cpp includes the real header.
struct android_app;

namespace IKIGAI::CORE {
struct PlatformStartup {
	void* nativeInstance = nullptr;
	android_app* android = nullptr;
};
}
