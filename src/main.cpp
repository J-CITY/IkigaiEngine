#include "coreModule/core/application.h"
#include "utilsModule/crashReport.h"
#ifdef USE_EDITOR
#include "editorModule/cameraTransformEdit.h"
#include "editorModule/editorRender.h"
#endif
#ifndef OCULUS
#ifdef USE_SDL
#include <SDL3/SDL_main.h>
#endif

int main(int argc, char* argv[]) {
	IKIGAI::UTILS::CRASH::install();
	IKIGAI::CORE::PlatformContext platform;
#ifdef USE_EDITOR
	platform.createEditor = []() -> std::unique_ptr<IKIGAI::CORE::EditorFrontend> { return std::make_unique<IKIGAI::EDITOR::EditorRender>(); };
#if USE_JSON_VIEWS
	platform.cameraEditing = { IKIGAI::EDITOR::IsCameraTransformLocked, IKIGAI::EDITOR::RecordCameraTransform };
#endif
#endif
	return IKIGAI::CORE::RunApplication(argc, argv, platform);
}
#endif
