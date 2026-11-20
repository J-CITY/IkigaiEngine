#pragma once

namespace IKIGAI::CORE {
class EditorFrontend {
public:
	virtual ~EditorFrontend() = default;
	virtual void draw() = 0;
};
}
