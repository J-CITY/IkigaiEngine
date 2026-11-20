#pragma once
#include "viewStore.h"
namespace IKIGAI::UI {
// Usable with any ImGui host, including without the engine editor or a scene.
class ViewRenderer {
	void drawNode(const Node& node, ViewInstance& instance);

public:
	std::map<std::string, std::function<void(const Node&, Controller&)>> custom;
	void draw(ViewInstance& instance, const std::string& error = { });
};
}
