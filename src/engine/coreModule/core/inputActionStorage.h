#pragma once

#include <string>

namespace IKIGAI::INPUT_SYSTEM {
class InputActions;
}

namespace IKIGAI::CORE {
inline constexpr const char* DefaultInputResourcePath = "/Configs/input.input";
inline constexpr const char* DefaultBindingOverridesPath = "input.bindings.json";

bool LoadInputActions(INPUT_SYSTEM::InputActions& actions, const std::string& path = DefaultInputResourcePath);
bool LoadInputBindingOverrides(INPUT_SYSTEM::InputActions& actions, const std::string& path = DefaultBindingOverridesPath);
bool SaveInputBindingOverrides(const INPUT_SYSTEM::InputActions& actions, const std::string& path = DefaultBindingOverridesPath);
}
