#include "viewHost.h"
#if USE_JSON_VIEWS && !defined(OCULUS)
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "utilsModule/log/loggerDefine.h"
#include <set>
#include <stdexcept>
#include <utility>

namespace IKIGAI::UI {
namespace {
	struct WindowScope {
		~WindowScope() {
			ImGui::End();
		}
	};
	struct IdScope {
		explicit IdScope(const char* id) {
			ImGui::PushID(id);
		}
		~IdScope() {
			ImGui::PopID();
		}
	};
}
ViewHost::ViewHost(ListViews listViews, ViewStore::Reader read)
	: mListViews(std::move(listViews))
	, mRead(std::move(read)) { }

void ViewHost::registerWidget(const std::string& name, std::function<void(const Node&, Controller&)> draw) {
	if (name.empty() || !draw) {
		throw std::runtime_error("Custom widget needs a name and callback");
	}
	renderer.custom[name] = std::move(draw);
	store.customWidgets.insert(name);
	reload();
}
void ViewHost::scan(bool force) {
	std::set<std::string> discovered;
	for (const auto& path : mListViews()) {
		discovered.insert(path);
		const auto previousError = store.windows[path].error;
		store.reload(path, mRead, force);
		const auto& error = store.windows.at(path).error;
		if (!error.empty() && error != previousError) {
			LOG_ERROR << error;
			showManager = true;
		}
	}
	for (auto it = store.windows.begin(); it != store.windows.end();) {
		if (discovered.contains(it->first)) {
			++it;
			continue;
		}
		auto& instance = it->second.instance;
		if (instance && !instance->faulted) {
			try {
				instance->finishEdits(instance->definition.children);
			} catch (const std::exception& e) {
				LOG_ERROR << it->first << ": " << e.what();
			}
		}
		it = store.windows.erase(it);
	}
}
void ViewHost::draw() {
	const auto now = std::chrono::steady_clock::now();
	if (reloadRequested || now >= nextScan) {
		try {
			scan(reloadRequested);
		} catch (const std::exception& e) {
			LOG_ERROR << "UI scan: " << e.what();
		}
		reloadRequested = false;
		nextScan = now + std::chrono::seconds(1);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_F8, false))
		showManager = !showManager;
	if (showManager) {
		const bool visible = ImGui::Begin("UI Views", &showManager);
		WindowScope window;
		if (visible) {
			if (ImGui::Button("Reload views"))
				reload();
			for (auto& [path, entry] : store.windows) {
				IdScope id(path.c_str());
				if (entry.instance)
					ImGui::Checkbox(entry.instance->definition.title.c_str(), &entry.instance->open);
				else
					ImGui::TextUnformatted(path.c_str());
				if (!entry.error.empty())
					ImGui::TextWrapped("%s", entry.error.c_str());
			}
		}
	}
	for (auto& [path, entry] : store.windows) {
		if (!entry.instance)
			continue;
		auto& instance = *entry.instance;
		try {
			renderer.draw(instance, entry.error);
		} catch (const std::exception& e) {
			entry.error = path + ": " + e.what();
			instance.faulted = true;
			LOG_ERROR << entry.error;
		}
	}
}
}
#endif
