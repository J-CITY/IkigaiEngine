#pragma once
#include <string>

#include "editorWindow.h"
#include "utilsModule/animation.h"

#ifdef USE_EDITOR

#include <memory>

namespace IKIGAI {
namespace ANIMATION {
	class Animation;
}
}

namespace IKIGAI::EDITOR {
class TimelineAnimationWindow : public EditorWindow {
	std::unique_ptr<IKIGAI::ANIMATION::Animation> mAnimation;
	std::string mResourcePath;
	bool mDirty = false;

	IKIGAI::ANIMATION::TimelineAnimationDescriptor toDescriptor() const;
	void fromDescriptor(const IKIGAI::ANIMATION::TimelineAnimationDescriptor& desc);
	bool saveToFile(const std::string& path);
	void markDirty();

public:
	TimelineAnimationWindow();
	void openResource(const std::string& path);
	virtual ~TimelineAnimationWindow() override = default;
	void draw() override;
};
}
#endif
