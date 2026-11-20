#pragma once
#if USE_JSON_VIEWS && !defined(OCULUS)
#include "viewRenderer.h"
#include "viewStore.h"
#include <chrono>

namespace IKIGAI::UI {
class ViewHost {
	std::chrono::steady_clock::time_point nextScan;
	bool reloadRequested = true;
	bool showManager = false;
	ViewRenderer renderer;
	void scan(bool force);

public:
	ViewStore store;
	void draw();
	void reload() {
		reloadRequested = true;
	}
	void registerWidget(const std::string& name, std::function<void(const Node&, Controller&)> draw);
};
}
#endif
