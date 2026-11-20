#include "viewHost.h"
#if USE_JSON_VIEWS && !defined(OCULUS)
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "utilsModule/log/loggerDefine.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

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
void ViewHost::registerWidget(const std::string& name, std::function<void(const Node&, Controller&)> draw) {
	if (name.empty() || !draw)
		throw std::runtime_error("Custom widget needs a name and callback");
	renderer.custom[name] = std::move(draw);
	store.customWidgets.insert(name);
	reload();
}
void ViewHost::scan(bool force) {
	auto& fs = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	auto read = [&](const std::string& path) {
		auto file = fs.getLiveFile(path);
		if (!file || !file->isValid())
			throw std::runtime_error("Cannot read " + path);
		return file->readStr();
	};
	// Content polling also works for debug hosts with USE_EDITOR=OFF and for
	// VFS mounts without native file timestamps. No work is done inside drawing.
	std::set<std::string> discovered;
	for (const auto& entry : fs.listLiveDirectory("/ui/windows")) {
		if (entry.isDirectory || !entry.path.ends_with(".json"))
			continue;
		discovered.insert(entry.path);
		const auto previousError = store.windows[entry.path].error;
		store.reload(entry.path, read, force);
		const auto& error = store.windows.at(entry.path).error;
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
