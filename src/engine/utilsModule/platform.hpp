#pragma once

#include <utilsModule/enum.h>

IKIGAI_ENUM_NS(IKIGAI, Platform, MAC, IOS, ANDROIDOS, EMSCRIPT, WINDOWS, LINUX)

namespace IKIGAI {
Platform GetCurrentPlatform();
}
