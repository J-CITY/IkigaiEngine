#pragma once

#include <string>

namespace IKIGAI::CORE {
std::string NormalizeRootPath(std::string path);

// Finds assets/engine and sets Config::ROOT unless startup supplied a root.
void InitContentRoot();
}
