#include "coreModule/core/application.h"
#include "utilsModule/crashReport.h"
#ifndef OCULUS
#ifdef USE_SDL
#include <SDL3/SDL_main.h>
#endif

int main(int argc, char* argv[]) {
	IKIGAI::UTILS::CRASH::install();
	return IKIGAI::CORE::RunApplication(argc, argv);
}
#endif
