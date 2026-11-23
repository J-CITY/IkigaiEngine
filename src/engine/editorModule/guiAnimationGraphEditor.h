#pragma once

#ifdef USE_EDITOR
#include <string>

namespace IKIGAI::EDITOR {
void openGuiAnimationGraphEditor(const std::string& path);
void drawGuiAnimationGraphEditor();
}
#endif
