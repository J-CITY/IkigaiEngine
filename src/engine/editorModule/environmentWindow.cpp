#include "environmentWindow.h"
#ifdef USE_EDITOR
#include "coreModule/ecs/systems/environmentSystem.h"
#include "environmentEditor.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"
#include <algorithm>
#include <exception>
#include <imgui.h>

namespace IKIGAI::EDITOR {
EnvironmentWindow::EnvironmentWindow() {
	mIsOpen = false;
}

void EnvironmentWindow::draw() {
	auto& manager = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>();
	ECS::EnvironmentComponent* environment = nullptr;
	if (manager.hasCurrentScene() && !manager.isEditingLocked()) {
		auto& scene = manager.getCurrentScene();
		// Never retain component pointers across frames or scene transitions.
		environment = ECS::EnvironmentSystem::active(scene);
		if (mSceneLifetime.lock() != scene.lifetime) {
			mSceneLifetime = scene.lifetime;
			mIsOpen = environment != nullptr;
			mSaveStatus.clear();
		}
	}
	if (!mIsOpen) {
		return;
	}
	const auto* viewport = ImGui::GetMainViewport();
	const float width = std::min(410.0f, viewport->WorkSize.x);
	const float height = std::min(680.0f, viewport->WorkSize.y);
	ImGui::SetNextWindowSize({ width, height }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(
		{ viewport->WorkPos.x + std::max(0.0f, viewport->WorkSize.x - width - 12), viewport->WorkPos.y + 12 }, ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Environment", &mIsOpen)) {
		if (manager.isEditingLocked()) {
			ImGui::TextWrapped("Waiting for the scene operation to finish...");
		}
		else if (!environment) {
			ImGui::TextWrapped("This scene has no active Environment component. Open the Environment demo from Menu > Scenes, or add an Environment "
							   "component to the scene.");
		}
		else {
			ImGui::TextWrapped("Changes apply live. Sun azimuth rotates the lighting; time of day changes the sun's elevation.");
			ImGui::BeginDisabled(manager.getCurrentSceneSourcePath().empty());
			if (ImGui::Button("Save scene")) {
				try {
					manager.saveToFile();
					mSaveStatus = "Saved: " + manager.getCurrentSceneSourcePath();
				} catch (const std::exception& error) {
					mSaveStatus = "Save failed: " + std::string(error.what());
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("Saves all scene changes");
			if (!mSaveStatus.empty()) {
				ImGui::TextWrapped("%s", mSaveStatus.c_str());
			}
			ImGui::Separator();
			DrawEnvironmentEditor(*environment);
		}
	}
	ImGui::End();
}
}
#endif
