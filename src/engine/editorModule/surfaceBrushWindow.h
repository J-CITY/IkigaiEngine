#pragma once
#include "editorWindow.h"
#ifdef USE_EDITOR
#include <memory>
namespace IKIGAI::EDITOR {
class SurfaceBrushWindow : public EditorWindow {
public:
	SurfaceBrushWindow();
	~SurfaceBrushWindow() override;
	void draw() override;
	bool isPainting() const;

private:
	struct State;
	std::unique_ptr<State> mState = {};
};
}
#endif
