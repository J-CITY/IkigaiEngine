#pragma once
#if USE_JSON_VIEWS && !defined(OCULUS)
#include "viewRenderer.h"
#include "viewStore.h"
#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace IKIGAI::UI {
class ViewHost {
public:
	using ListViews = std::function<std::vector<std::string>()>;

private:
	ListViews mListViews;
	ViewStore::Reader mRead;
	std::chrono::steady_clock::time_point nextScan;
	bool reloadRequested = true;
	bool showManager = false;
	ViewRenderer renderer;
	void scan(bool force);

public:
	ViewStore store;
	ViewHost(ListViews listViews, ViewStore::Reader read);
	void draw();
	void reload() {
		reloadRequested = true;
	}
	void registerWidget(const std::string& name, std::function<void(const Node&, Controller&)> draw);
};
}
#endif
