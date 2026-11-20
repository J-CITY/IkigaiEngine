#pragma once
#include <optional>
#include <string>

namespace IKIGAI::SCRIPTING {
// The host supplies raw text by path. Runtimes normalize it before evaluation.
using ReadScriptText = std::optional<std::string>(const std::string& path);
}
