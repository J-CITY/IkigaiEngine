#pragma once
#include "editorWindow.h"
#ifdef USE_EDITOR
#include <memory>
#include <string>

namespace IKIGAI::EDITOR {
class EnvironmentWindow final : public EditorWindow {
public:
	EnvironmentWindow();
	void draw() override;

private:
	std::weak_ptr<void> mSceneLifetime = {};
	std::string mSaveStatus = {};
};
}
#endif
