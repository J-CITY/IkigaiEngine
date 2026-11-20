#pragma once
#include "cameraUiBindings.h"
#include <memory>

// Native process and Android app pointers without including a platform SDK.
// android_app stays incomplete; only the platform .cpp includes the real header.
struct android_app;

namespace IKIGAI::CORE {
class EditorFrontend;
using CreateEditorFrontend = std::unique_ptr<EditorFrontend> (*)();

struct PlatformStartup {
	void* nativeInstance = nullptr;
	android_app* android = nullptr;
	CreateEditorFrontend createEditor = nullptr;
#if USE_JSON_VIEWS && !defined(OCULUS)
	CameraEditing cameraEditing {};
#endif
};
}
