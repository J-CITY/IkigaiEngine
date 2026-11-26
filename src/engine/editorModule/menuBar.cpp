#include "menuBar.h"
#include "captureModule/screenshot.h"
#include "prefabEditor.h"
#ifdef USE_EDITOR
#include "imgui.h"
#include "renderingDemos.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"

using namespace IKIGAI::EDITOR;

void MenuBar::draw() {

	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("Capture")) {
			auto& screenshots = CAPTURE::ScreenshotService::Get();
			if (ImGui::MenuItem("Save screenshot", "F12", false, !screenshots.busy())) {
				screenshots.request();
			}
			const auto& status = screenshots.status();
			if (status.state == CAPTURE::ScreenshotService::State::PENDING) {
				ImGui::TextUnformatted("Waiting for frame...");
			}
			if (status.state == CAPTURE::ScreenshotService::State::SAVING) {
				ImGui::TextUnformatted("Saving PNG...");
			}
			if (status.state == CAPTURE::ScreenshotService::State::SAVED) {
				const auto utf8 = status.path.u8string();
				ImGui::TextWrapped("Saved: %s", reinterpret_cast<const char*>(utf8.c_str()));
			}
			if (status.state == CAPTURE::ScreenshotService::State::FAILED) {
				ImGui::TextWrapped("Screenshot failed: %s", status.error.c_str());
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Menu")) {
			if (ImGui::MenuItem("Save Current Scene", "CTRL+S")) {
				if (RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().hasCurrentScene()) {
					PREFABS::Try([] { PREFABS::Manager().saveToFile(); });
				}
			}
			if (ImGui::MenuItem("Create new scene")) {
				// popupStates["create_new_scene"] = true;
			}
			if (ImGui::BeginMenu("Scenes")) {
				auto& manager = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>();
				const auto demo = [&](const char* label, const char* path, bool available = true) {
					const auto& current = manager.getCurrentSceneSourcePath();
					if (ImGui::MenuItem(label, nullptr, current == path || current == path + 1, available && !manager.isEditingLocked())) {
						PREFABS::Try([&] { manager.loadFromFileAsync(path); });
					}
				};
				demo("Startup - Cube", "/scenes/scene.json");
				demo("Rocket Lander", "/scenes/rocket-lander.json", USE_LUA);
				demo("Ragdoll", "/scenes/ragdoll-demo.json", USE_PHYSICS_3D);
				demo("Environment - Landscape and Weather", "/scenes/environment-demo.json");
				demo("Drifting Boat", "/scenes/drifting-boat.json", USE_PHYSICS_3D);
				demo("Vehicle proving ground", "/scenes/vehicle-proving-ground.json", USE_PHYSICS_3D);
				demo("3D Physics", "/scenes/physics-3d-demo.json", USE_PHYSICS_3D);
				demo("Destruction Playground", "/scenes/destruction-playground.json", USE_PHYSICS_3D);
				demo("Layered Materials", "/scenes/layered-materials.json");
				demo("Facial Expressions", "/scenes/facial-expressions.json");
				demo("2D Physics", "/scenes/physics-2d-demo.json", USE_PHYSICS_2D);
				demo("2D Cloth - Wind", Cloth2DPath, USE_PHYSICS_2D);
				demo("3D Cloth - Wind", Cloth3DPath, USE_PHYSICS_3D);
				demo("3D Soft Body", SoftBodyPath, USE_PHYSICS_3D);
				demo("Voxel Landscape", "/scenes/voxel-coast.json");
				demo("GUI - Widget Playground", "/scenes/gui-widgets.json");
				demo("GUI - Widgets and Curves", "/scenes/gui-demo.json");
				demo("Particles - 2D and 3D", "/scenes/particles-demo.json");
				demo("Particles - Events", ParticleEventPath);
				const auto& content = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
				demo("Lumen-style - Screen Probe GI", ScreenProbeRoomPath, content.isFileExist(ScreenProbeRoomPath));
				demo("Nanite-style - Virtual Geometry", VirtualGeometryGalleryPath, content.isFileExist(VirtualGeometryGalleryPath));
				demo("Fur - Rabbit", FurRabbitPath, content.isFileExist(FurRabbitPath));
				ImGui::Separator();
#if USE_PHYSICS_3D
				if (ImGui::MenuItem("Third Person - Parkour Playground", nullptr,
						manager.getCurrentSceneSourcePath() == "/third-person-traversal-demo.json"
							|| manager.getCurrentSceneSourcePath() == "third-person-traversal-demo.json",
						!manager.isEditingLocked())) {
					PREFABS::Try([&] { manager.loadFromFileAsync("/third-person-traversal-demo.json"); });
				}
#endif
				if (ImGui::MenuItem("Pica Pica - Hybrid PBR", nullptr, manager.getCurrentSceneSourcePath() == "/scenes/pica-pica-demo.json",
						!manager.isEditingLocked())) {
					PREFABS::Try([&] { manager.loadFromFileAsync("/scenes/pica-pica-demo.json"); });
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Examples")) {
			// ImGui::MenuItem("Main menu bar", NULL, &[](){});
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
}
#endif
