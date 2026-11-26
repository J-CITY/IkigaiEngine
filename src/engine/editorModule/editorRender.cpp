#include "editorRender.h"
#include "coreModule/ecs/components/environmentComponent.h"
#include "coreModule/ecs/components/waterComponent.h"
#include "coreModule/ecs/systems/environmentSystem.h"
#include "environmentWindow.h"
#include "prefabEditor.h"
#include "resourceModule/jsonLoader.h"
#include "surfaceBrushWindow.h"

#include "resourceEditorWindow.h"
#include "timelineAnimationWindow.h"

#include "utilsModule/log/loggerDefine.h"
#ifdef USE_EDITOR
#include "IconsFontAwesome5.h"
#include "cameraControlWindow.h"
#include "componentManagerWindow.h"
#include "coreModule/config.h"
#include "coreModule/ecs/components/facialAnimationComponent.h"
#include "coreModule/ecs/components/modelRenderer.h"
#include "destructionEditor.h"
#include "editorModule/imguiHelper/ImGuiFileBrowser.h"
#include "fileBrowser.h"
#include "guiAnimationGraphEditor.h"
#include "imgui.h"
#include "menuBar.h"
#include "misc/cpp/imgui_stdlib.h"
#include "pbrMaterialEditor.h"
#include "physicsDebugWindow.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/components/materialRenderer.h"
#include "coreModule/ecs/components/physics3dComponent.h"
#include "coreModule/ecs/components/vehicleComponent.h"
#include "resourceModule/materialManager.h"
#include <magic_enum.hpp>
#include <numbers>
#endif
#include "profillerWindow.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include "renderingDemos.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"
#include "sceneTransformTool.h"
#include "statWindow.h"
#include "textureWatcher.h"
#include "treeWindow.h"
#include "windowModule/inputManager/inputActions.h"
#include "windowModule/inputManager/inputManager.h"
#include <cstring>
#include <list>

using namespace IKIGAI::EDITOR;

namespace {
#if USE_PHYSICS_3D
template <class E> bool selectVehiclePart(const char* label, E& current) {
	bool changed = false;
	if (ImGui::BeginCombo(label, magic_enum::enum_name(current).data())) {
		for (auto option : magic_enum::enum_values<E>()) {
			const bool selected = option == current;
			if (ImGui::Selectable(magic_enum::enum_name(option).data(), selected)) {
				current = option;
				changed = true;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	return changed;
}
void drawCircuitSurface(IKIGAI::SCENE_SYSTEM::Scene& scene) {
	using namespace IKIGAI;
	using PHYSICS::RoadSurface;
	const auto track = scene.findObjectByName("Circuit road");
	if (!track) {
		return;
	}
	auto collider = track->getComponent<ECS::Physics3DComponent>();
	auto renderer = track->getComponent<ECS::MaterialRenderer>();
	if (!collider || !renderer) {
		return;
	}
	struct SurfaceOption {
		RoadSurface surface;
		const char* name;
		const char* material;
	};
	static constexpr SurfaceOption options[] { { RoadSurface::Asphalt, "Asphalt", "/materials/vehicle/Asphalt.mat" },
		{ RoadSurface::WetAsphalt, "Wet asphalt", "/materials/vehicle/WetAsphalt.mat" }, { RoadSurface::Ice, "Ice", "/materials/vehicle/Ice.mat" },
		{ RoadSurface::Mud, "Mud", "/materials/vehicle/Mud.mat" }, { RoadSurface::Sand, "Sand", "/materials/vehicle/Sand.mat" },
		{ RoadSurface::Snow, "Snow", "/materials/vehicle/Snow.mat" }, { RoadSurface::DirtRoad, "Dirt road", "/materials/vehicle/DirtRoad.mat" } };
	const auto current = std::ranges::find(options, collider->settings.Surface, &SurfaceOption::surface);
	if (ImGui::BeginCombo("Track surface", current == std::end(options) ? "Collider" : current->name)) {
		for (const auto& option : options) {
			const bool selected = option.surface == collider->settings.Surface;
			if (ImGui::Selectable(option.name, selected) && !selected) {
				if (auto material = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(option.material)) {
					renderer->setMaterial(0, material);
					collider->settings.Surface = option.surface;
					collider->settings.Friction = PHYSICS::tireResponse(PHYSICS::TireStyle::Road, option.surface, 1.f).grip;
				}
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
}
void drawVehicleWorkshop(IKIGAI::SCENE_SYSTEM::Scene& scene, bool& open) {
	using namespace IKIGAI;
	using namespace PHYSICS;
	ImGui::SetNextWindowSize({ 425, 600 }, ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Vehicle workshop", &open)) {
		drawCircuitSurface(scene);
		bool found = false;
		for (auto& driver : scene.getWorld().getComponentManager()->getComponentsArray<ECS::VehicleComponent>()) {
			if (!scene.getObjectIndex(driver.obj.get())) {
				continue;
			}
			found = true;
			ImGui::PushID(&driver);
			ImGui::TextUnformatted(driver.obj->getName().c_str());
			const auto& settings = driver.getDescriptor();
			float assistPercent = settings.Assistance * 100.f;
			auto steering = settings.Steering;
			auto transmission = settings.Transmission;
			ImGui::TextUnformatted("Handling assist: Simulation to Arcade");
			ImGui::SetNextItemWidth(-1.f);
			bool controlsChanged = ImGui::SliderFloat("##HandlingAssist", &assistPercent, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Arcade increases launch torque, cornering grip and steering response.");
			}
			float highSpeedSteering = settings.HighSpeedSteering * 100.f;
			ImGui::SetNextItemWidth(-150.f);
			if (ImGui::SliderFloat("High-speed steering", &highSpeedSteering, Vehicle::minHighSpeedSteering * 100.f,
					Vehicle::maxHighSpeedSteering * 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
				driver.setHighSpeedSteering(highSpeedSteering / 100.f);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Higher values tighten fast turns. 100%% uses the original steering.");
			}
			DriftTuning tuning = settings;
			if (ImGui::Checkbox("Arcade drift", &tuning.driftEnabled)) {
				driver.setDriftTuning(tuning);
			}
			if (!tuning.driftEnabled) {
				float strength = tuning.antiSkidStrength * 100.f;
				if (ImGui::SliderFloat("Anti-skid assist", &strength, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
					tuning.antiSkidStrength = strength / 100.f;
					driver.setDriftTuning(tuning);
				}
			}
			if (tuning.driftEnabled && ImGui::TreeNode("Drift tuning")) {
				ImGui::PushItemWidth(-165.f);
				float sensitivity = tuning.breakawaySensitivity * 100.f;
				bool edited = ImGui::SliderFloat("Handbrake sensitivity", &sensitivity, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
				if (edited) {
					tuning.breakawaySensitivity = sensitivity / 100.f;
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Higher values start a slide with less steering and a shorter handbrake tap.");
				}
				edited |= ImGui::SliderFloat(
					"Power angle", &tuning.powerAngleDegrees, DriftTuning::minAngle, DriftTuning::maxAngle, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Target slip angle at full throttle with centred steering.");
				}
				edited |= ImGui::SliderFloat("Recovery time", &tuning.recoverySeconds, DriftTuning::minRecovery, DriftTuning::maxRecovery, "%.2f s",
					ImGuiSliderFlags_AlwaysClamp);
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Time for drift assistance to fade after lifting the throttle.");
				}
				if (edited) {
					driver.setDriftTuning(tuning);
				}
				ImGui::PopItemWidth();
				ImGui::TreePop();
			}
			controlsChanged |= selectVehiclePart("Steering", steering);
			controlsChanged |= selectVehiclePart("Transmission", transmission);
			if (controlsChanged) {
				driver.setControls(assistPercent / 100.f, steering, transmission);
			}
			if (transmission == TransmissionMode::Direct) {
				ImGui::TextUnformatted("W forward / S reverse");
			}
			else {
				const int gear = driver.gear();
				if (transmission == TransmissionMode::ForwardReverse) {
					ImGui::Text("W throttle   Q reverse / E drive   [%s]", gear < 0 ? "R" : "D");
				}
				else {
					ImGui::Text("W throttle   Q down / E up   [%s]", gear < 0 ? "R" : gear == 0 ? "N" : std::to_string(gear).c_str());
				}
			}
			ImGui::TextUnformatted("A/D steer   Space handbrake   LCtrl brake");
			if (tuning.driftEnabled && assistPercent > 0.f) {
				ImGui::TextDisabled("Drift: tap Space in a turn, then hold W.");
			}
			if (auto rigidBody = driver.obj->getComponent<ECS::Physics3DComponent>(); rigidBody && rigidBody->runtime) {
				if (auto world = rigidBody->runtime->world.lock()) {
					if (auto* car = world->vehicle(rigidBody->simulationId())) {
						ImGui::Text("%.0f km/h    Nitro (LShift + W)   %.0f%%", car->state.speed * 3.6f, car->state.nitroCharge * 100.f);
						if (std::abs(car->state.driftEngagement) > .05f) {
							ImGui::Text("Drift angle   %.0f deg", std::abs(car->state.driftAngle) * 180.f / std::numbers::pi_v<float>);
						}
					}
				}
			}
			ImGui::Separator();
			auto parts = driver.config();
			bool changed = false;
			if (ImGui::Button("Sports car")) {
				parts = vehiclePreset(ChassisStyle::Sport);
				changed = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Crossover")) {
				parts = vehiclePreset(ChassisStyle::Crossover);
				changed = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Truck")) {
				parts = vehiclePreset(ChassisStyle::Truck);
				changed = true;
			}
			changed |= selectVehiclePart("Chassis", parts.chassis);
			auto rigid = driver.obj->getComponent<ECS::Physics3DComponent>();
			if (rigid) {
				const float chassisMass = parts.chassis == ChassisStyle::Authored ? rigid->settings.Mass : chassisDimensions(parts.chassis).mass;
				float mass = parts.massOverrideKg > 0 ? parts.massOverrideKg : chassisMass;
				if (ImGui::DragFloat("Mass (kg)", &mass, 5.f, 1.f, 100000.f, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
					parts.massOverrideKg = mass;
					changed = true;
				}
				if (parts.massOverrideKg > 0 && ImGui::SmallButton("Use chassis mass")) {
					parts.massOverrideKg = 0;
					changed = true;
				}
			}
			changed |= selectVehiclePart("Suspension", parts.suspension);
			changed |= selectVehiclePart("Tires", parts.tires);
			changed |= selectVehiclePart("Brakes", parts.brakes);
			changed |= selectVehiclePart("Engine", parts.engine);
			changed |= selectVehiclePart("Drive", parts.drive);
			ImGui::Separator();
			if (ImGui::TreeNode("Fine tuning")) {
				auto effective = parts.resolved();
				if (ImGui::SliderFloat("Drive torque", &effective.driveTorque, 200, 6000, "%.0f Nm")) {
					parts.engine = EngineStyle::Authored;
					parts.driveTorque = effective.driveTorque;
					changed = true;
				}
				bool adjustSpring = ImGui::SliderFloat("Spring rate", &effective.stiffness, 15000, 140000, "%.0f N/m");
				adjustSpring |= ImGui::SliderFloat("Compression damping", &effective.compressionDamping, 500, 15000, "%.0f Ns/m");
				adjustSpring |= ImGui::SliderFloat("Rebound damping", &effective.reboundDamping, 500, 20000, "%.0f Ns/m");
				if (adjustSpring) {
					parts.suspension = SuspensionStyle::Authored;
					parts.restLength = effective.restLength;
					parts.maxCompression = effective.maxCompression;
					parts.maxDroop = effective.maxDroop;
					parts.antiRollStiffness = effective.antiRollStiffness;
					parts.maxSuspensionForce = effective.maxSuspensionForce;
					parts.stiffness = effective.stiffness;
					parts.compressionDamping = effective.compressionDamping;
					parts.reboundDamping = effective.reboundDamping;
					changed = true;
				}
				ImGui::TreePop();
			}
			ImGui::TextUnformatted("Centre of mass (metres)");
			changed |= ImGui::SliderFloat("Lateral X", &parts.centerOfMass.x, -.65f, .65f, "%.2f");
			changed |= ImGui::SliderFloat("Height Y", &parts.centerOfMass.y, -.45f, .65f, "%.2f");
			changed |= ImGui::SliderFloat("Forward Z", &parts.centerOfMass.z, -1.f, 1.f, "%.2f");
			if (ImGui::Button("Centre on chassis")) {
				parts.centerOfMass = {};
				changed = true;
			}
			if (changed) {
				driver.configure(parts);
			}
			if (rigid && rigid->runtime) {
				if (auto world = rigid->runtime->world.lock()) {
					if (auto* car = world->vehicle(rigid->simulationId())) {
						ImGui::Text("Mass %.0f kg   Drive torque %.0f Nm", rigid->body()->getMass(), car->config.driveTorque);
						constexpr std::array names { "Front left", "Front right", "Rear left", "Rear right" };
						for (size_t i = 0; i < names.size(); ++i) {
							const auto& wheel = car->state.wheels[i];
							ImGui::Text("%s: %s   %.0f N", names[i], wheel.normalLoad > 0 ? magic_enum::enum_name(wheel.surface).data() : "Air",
								wheel.normalLoad);
						}
					}
				}
			}
			ImGui::PopID();
		}
		if (!found) {
			ImGui::TextUnformatted("Open a scene with a VehicleComponent.");
		}
		// The workshop stays open while driving. An idle window can hold ImGui navigation
		// focus even when no widget consumes the driving keys.
		const auto& io = ImGui::GetIO();
		if (found && ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && !io.WantTextInput && !io.NavVisible
			&& !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
			ImGui::SetNextFrameWantCaptureKeyboard(false);
		}
	}
	ImGui::End();
}
#endif
void drawWaterWindow(IKIGAI::SCENE_SYSTEM::Scene& scene, bool& open) {
	using namespace IKIGAI;
	const auto* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos({ viewport->WorkPos.x + 10, viewport->WorkPos.y + 10 }, ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Water and Wind", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
		bool found = false;
		for (auto& water : scene.getWorld().getComponentManager()->getComponentsArray<ECS::WaterComponent>()) {
			if (!scene.getObjectIndex(water.obj.get())) {
				continue;
			}
			found = true;
			ImGui::PushItemWidth(138.f);
			ImGui::PushID(&water);
			ImGui::TextUnformatted(water.obj->getName().c_str());
			auto settings = water.getDescriptor();
			auto preset = ENVIRONMENT::Preset::Custom;
			bool changed = false;
			if (ImGui::Button("Calm")) {
				settings.Amplitude = .012f;
				settings.Wavelength = 8;
				settings.WindSpeed = .3f;
				settings.Current = 0;
				settings.Foam = 0;
				settings.Roughness = .06f;
				preset = ENVIRONMENT::Preset::Clear;
			}
			ImGui::SameLine();
			if (ImGui::Button("Breeze")) {
				settings.Amplitude = .16f;
				settings.Wavelength = 7;
				settings.WindSpeed = 4;
				settings.Current = .12f;
				settings.Foam = .35f;
				settings.Roughness = .14f;
				preset = ENVIRONMENT::Preset::WhiteClouds;
			}
			ImGui::SameLine();
			if (ImGui::Button("Storm")) {
				settings.Amplitude = 1.05f;
				settings.Wavelength = 11;
				settings.WindSpeed = 19;
				settings.Current = .35f;
				settings.Foam = .9f;
				settings.Roughness = .25f;
				preset = ENVIRONMENT::Preset::Storm;
			}
			changed |= ImGui::SliderFloat("Amplitude (m)", &settings.Amplitude, 0, 2);
			changed |= ImGui::SliderFloat("Wavelength (m)", &settings.Wavelength, 2, 40);
			bool windChanged = ImGui::SliderFloat("Wind (m/s)", &settings.WindSpeed, 0, 40);
			windChanged |= ImGui::SliderFloat("Heading (deg)", &settings.Heading, -180, 180);
			changed |= ImGui::SliderFloat("Current (m/s)", &settings.Current, -3, 3);
			changed |= ImGui::ColorEdit3("Water color", &settings.Color.x);
			changed |= ImGui::SliderFloat("Roughness", &settings.Roughness, .04f, 1);
			changed |= ImGui::SliderFloat("Whitecaps", &settings.Foam, 0, 1);
			changed |= ImGui::SliderFloat("Hull foam", &settings.HullFoam, 0, 1);
			if (changed || windChanged || preset != ENVIRONMENT::Preset::Custom) {
				water.configure(settings);
			}
			if (windChanged || preset != ENVIRONMENT::Preset::Custom) {
				if (auto* env = ECS::EnvironmentSystem::active(scene)) {
					auto weather = env->settings();
					if (preset != ENVIRONMENT::Preset::Custom) {
						weather = ENVIRONMENT::presetSettings(preset, weather);
					}
					const float angle = settings.Heading * .01745329252f;
					weather.WindDirection = { std::cos(angle), std::sin(angle) };
					weather.WindSpeed = settings.WindSpeed;
					env->apply(weather);
				}
			}
			ImGui::PopItemWidth();
			ImGui::PopID();
		}
		if (!found) {
			ImGui::TextUnformatted("Add a WaterComponent to this scene.");
		}
	}
	ImGui::End();
}
void drawFacialExpressions(IKIGAI::SCENE_SYSTEM::Scene& scene) {
	using namespace IKIGAI;
	const auto object = scene.findObjectByName("Ceramic face");
	if (!object) {
		return;
	}
	const auto facial = object->getComponent<ECS::FacialAnimationComponent>();
	const auto renderer = object->getComponent<ECS::ModelRenderer>();
	if (!facial || !renderer || !renderer->getModel()) {
		return;
	}
	const auto* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos({ viewport->Pos.x + 12, viewport->Pos.y + ImGui::GetFrameHeight() + 8 }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Facial Expressions", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}
	try {
		facial->bind(*renderer->getModel());
		if (ImGui::Button("Neutral")) {
			facial->clearWeights();
		}
		for (const auto& clip : facial->availableClips()) {
			ImGui::SameLine();
			if (ImGui::Button(clip.c_str())) {
				facial->clearWeights();
				facial->play(clip);
			}
		}
		for (const auto& target : facial->availableTargets()) {
			float value = facial->weight(target);
			if (ImGui::SliderFloat(target.c_str(), &value, 0.f, 1.f)) {
				facial->setWeight(target, value);
			}
		}
	} catch (const std::exception& error) {
		ImGui::TextWrapped("%s", error.what());
	}
	ImGui::End();
}
}

struct EditorRender::Internal {
	std::unique_ptr<MenuBar> mMenu;
	ProfillerWindow mProfiler;
	SurfaceBrushWindow mSurfaceBrushes;
	EnvironmentWindow mEnvironment;
	SceneTransformTool mSceneTransform;
	ScreenProbeRoomControls mScreenProbeRoom;
	bool mWaterOpen = false;
	bool mVehicleOpen = false;
	bool mWorkspaceVisible = true;
	TimelineAnimationWindow* mTimelineAnimation = nullptr;

	std::list<std::unique_ptr<EditorWindow>> mWindows;
	std::list<std::unique_ptr<EditorWindow>> mResWindows;

	Internal() {
		mMenu = std::make_unique<MenuBar>();
		mProfiler.setOpen(false);

		mWindows.push_back(std::make_unique<CameraControlWindow>());
		mWindows.push_back(std::make_unique<ComponentManagerWindow>());
		mWindows.push_back(std::make_unique<ComponentInspectorWindow>());
		mWindows.push_back(std::make_unique<TreeWindow>());
		mWindows.push_back(std::make_unique<StatWindow>());
		{
			auto timeline = std::make_unique<TimelineAnimationWindow>();
			mTimelineAnimation = timeline.get();
			mWindows.push_back(std::move(timeline));
		}
		mWindows.push_back(std::make_unique<FileBrowserWindow>("/"));
		mWindows.push_back(std::make_unique<TextureWatcherWindow>());

		// RENDER::ShaderResource res;
		// mWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::ShaderResource>>(res));
	}
};

EditorRender::~EditorRender() {
	if (IKIGAI::RESOURCES::ServiceManager::Check<IKIGAI::INPUT_SYSTEM::InputManager>()) {
		IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::INPUT_SYSTEM::InputManager>().setUiCaptureProvider(nullptr);
	}
}
void EditorRender::openSurfaceBrushes() {
	mData->mSurfaceBrushes.setOpen(true);
}

EditorRender::EditorRender() {
	mData = std::make_unique<Internal>();

	// Init imgui style

	ImGuiIO& io = ImGui::GetIO();
	(void)io;

	const float baseFontSize = 14.0f;
	ImFontConfig config;
	config.SizePixels = baseFontSize;
	io.Fonts->AddFontDefault(&config);

	const float iconFontSize = baseFontSize * 2.0f / 3.0f;

	// merge in icons from Font Awesome
	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
	ImFontConfig icons_config;
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;
	icons_config.GlyphMinAdvanceX = iconFontSize;

	const std::string fontPath = std::string("/fonts/") + FONT_ICON_FILE_NAME_FAS;
	auto& fs = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::FileSystem>();
	auto fontFile = fs.getFile(fontPath, IKIGAI::RESOURCES::FileMode::READ);
	if (!fontFile || !fontFile->isValid()) {
		LOG_ERROR << "Icon font not found: " << fontPath;
	}
	else {
		auto bytes = fontFile->read();
		if (bytes.empty()) {
			LOG_ERROR << "Icon font is empty: " << fontPath;
		}
		else {
			void* fontData = IM_ALLOC(bytes.size());
			std::memcpy(fontData, bytes.data(), bytes.size());
			io.Fonts->AddFontFromMemoryTTF(fontData, static_cast<int>(bytes.size()), iconFontSize, &icons_config, icons_ranges);
		}
	}

	// ImGui_ImplVulkan_CreateFontsTexture();
	// #ifdef VULKAN_BACKEND
	//	auto& render =
	// reinterpret_cast<IKIGAI::RENDER::GameRendererVk&>(IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RENDER::GameRendererInterface>());
	//	render.initForVk();
	// #endif

	// #ifdef DX12_BACKEND
	//	auto& render =
	// reinterpret_cast<IKIGAI::RENDER::GameRendererDx12&>(IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RENDER::GameRendererInterface>()); #endif
	if (IKIGAI::RESOURCES::ServiceManager::Check<IKIGAI::INPUT_SYSTEM::InputManager>()) {
		IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::INPUT_SYSTEM::InputManager>().setUiCaptureProvider([] {
			if (ImGui::GetCurrentContext()) {
				const auto& io = ImGui::GetIO();
				return IKIGAI::INPUT_SYSTEM::InputManager::UiCapture { io.WantCaptureKeyboard, io.WantCaptureMouse || GlobalState.mSceneEditing };
			}
			return IKIGAI::INPUT_SYSTEM::InputManager::UiCapture {};
		});
	}
}

void EditorRender::draw() {
	static std::weak_ptr<void> historyScene;
	auto& manager = PREFABS::Manager();
	const auto sourcePath = manager.getCurrentSceneSourcePath();
	const std::string_view path = sourcePath.starts_with('/') ? std::string_view(sourcePath).substr(1) : std::string_view(sourcePath);
	const bool destructionPlayground = path == "scenes/destruction-playground.json";
	const bool layeredPlayground = path == "scenes/layered-materials.json";
	const bool facialPlayground = path == "scenes/facial-expressions.json";
	const bool screenProbeRoom = path == std::string_view(ScreenProbeRoomPath).substr(1);
	const bool virtualGeometryGallery = path == std::string_view(VirtualGeometryGalleryPath).substr(1);
	const bool furRabbit = path == std::string_view(FurRabbitPath).substr(1);
	const bool cloth2D = path == std::string_view(Cloth2DPath).substr(1);
	const bool cloth3D = path == std::string_view(Cloth3DPath).substr(1);
	const bool softBody = path == std::string_view(SoftBodyPath).substr(1);
	const bool particleEvents = path == std::string_view(ParticleEventPath).substr(1);
	const bool weatherDemo = path == "scenes/environment-demo.json";
	const bool rocketLander = path == "scenes/rocket-lander.json";
	const bool vehicleDemo = path == "scenes/vehicle-proving-ground.json";
	const bool boatDemo = path == "scenes/drifting-boat.json";
	const bool focusedDemo = screenProbeRoom || virtualGeometryGallery || furRabbit || cloth2D || cloth3D || softBody || particleEvents || weatherDemo
		|| boatDemo || vehicleDemo || rocketLander;
	if (manager.hasCurrentScene() && historyScene.lock() != manager.getCurrentScene().lifetime) {
		GlobalState.mCommandHistory.clear();
		GlobalState.mSelectObject.reset();
		historyScene = manager.getCurrentScene().lifetime;
		mData->mScreenProbeRoom.reset();
		mData->mWaterOpen = boatDemo;
		mData->mVehicleOpen = vehicleDemo;
		mData->mWorkspaceVisible
			= path != "scenes/gui-widgets.json" && !destructionPlayground && !layeredPlayground && !facialPlayground && !focusedDemo;
		if (focusedDemo) {
			ImGui::SetWindowCollapsed("Camera Tools###json-view/camera_tools", true);
		}
		if (!mData->mWorkspaceVisible) {
			mData->mSceneTransform.enabled = false;
		}
	}
	if (GlobalState.mSelectObject && !GlobalState.mSelectObject->isAlive()) {
		GlobalState.mSelectObject.reset();
	}
	PREFABS::CompletePending();
	if (!mData->mSceneTransform.active() && !mData->mSurfaceBrushes.isPainting() && ImGui::IsKeyPressed(ImGuiKey_Z, false) && ImGui::GetIO().KeyCtrl
		&& !ImGui::GetIO().KeyShift) {
		PREFABS::Try([] { GlobalState.mCommandHistory.undo(); });
	}
	if (!mData->mSceneTransform.active() && !mData->mSurfaceBrushes.isPainting()
		&& ((ImGui::IsKeyPressed(ImGuiKey_Y, false) && ImGui::GetIO().KeyCtrl)
			|| (ImGui::IsKeyPressed(ImGuiKey_Z, false) && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift))) {
		PREFABS::Try([] { GlobalState.mCommandHistory.redo(); });
	}

	mData->mMenu->draw();
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("Window")) {
			ImGui::MenuItem("Scene Editing", nullptr, &mData->mSceneTransform.enabled);
			ImGui::MenuItem("Object Markers", nullptr, &mData->mSceneTransform.showMarkers);
			ImGui::MenuItem("Editor Panels", nullptr, &mData->mWorkspaceVisible);
			bool profilerOpen = mData->mProfiler.isOpen();
			if (ImGui::MenuItem("Profiler", nullptr, &profilerOpen)) {
				mData->mProfiler.setOpen(profilerOpen);
			}
			bool surfaceOpen = mData->mSurfaceBrushes.isOpen();
			if (ImGui::MenuItem("Surface Brushes", nullptr, &surfaceOpen)) {
				mData->mSurfaceBrushes.setOpen(surfaceOpen);
			}
#if USE_PHYSICS_3D
			ImGui::MenuItem("Vehicle workshop", nullptr, &mData->mVehicleOpen);
#endif
			ImGui::MenuItem("Water and Wind", nullptr, &mData->mWaterOpen);
			bool environmentOpen = mData->mEnvironment.isOpen();
			if (ImGui::MenuItem("Environment", nullptr, &environmentOpen)) {
				mData->mEnvironment.setOpen(environmentOpen);
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		const auto fps = ImGui::GetIO().Framerate;
		ImGui::Text("%.1f FPS | %.1f ms", fps, fps > 0 ? 1000.0f / fps : 0.0f);
		ImGui::EndMainMenuBar();
	}
	if ((mData->mWorkspaceVisible || cloth2D) && manager.hasCurrentScene()) {
		DrawPhysicsDebug(manager.getCurrentScene(), cloth2D);
	}
#if USE_PHYSICS_3D
	if (mData->mVehicleOpen && manager.hasCurrentScene()) {
		drawVehicleWorkshop(manager.getCurrentScene(), mData->mVehicleOpen);
	}
#endif
	if (mData->mWaterOpen && manager.hasCurrentScene()) {
		drawWaterWindow(manager.getCurrentScene(), mData->mWaterOpen);
	}
	if (mData->mWorkspaceVisible || weatherDemo) {
		mData->mEnvironment.draw();
	}
	if (mData->mWorkspaceVisible) {
		mData->mProfiler.draw();
		mData->mSurfaceBrushes.draw();
		for (auto& win : mData->mWindows) {
			win->draw();
		}
		drawGuiAnimationGraphEditor();
	}
	if (manager.hasCurrentScene()) {
		if (!rocketLander || mData->mWorkspaceVisible || mData->mSceneTransform.enabled) {
			mData->mSceneTransform.draw(manager.getCurrentScene(), destructionPlayground);
		}
#if USE_PHYSICS_3D
		if (destructionPlayground) {
			DrawDestructionPlayground(manager.getCurrentScene(), !mData->mSceneTransform.capturesMouse());
		}
#endif
		if (layeredPlayground) {
			DrawLayeredMaterialPlayground(manager.getCurrentScene());
		}
		if (facialPlayground) {
			drawFacialExpressions(manager.getCurrentScene());
		}
		if (screenProbeRoom) {
			mData->mScreenProbeRoom.draw(manager.getCurrentScene());
		}
		if (virtualGeometryGallery) {
			DrawVirtualGeometryGallery(manager.getCurrentScene());
		}
		if (furRabbit) {
			DrawFurRabbit(manager.getCurrentScene());
		}
		if (cloth2D || cloth3D) {
			DrawClothWind(manager.getCurrentScene(), cloth2D);
		}
		if (softBody) {
			DrawSoftBodyDemo();
		}
		if (particleEvents) {
			DrawParticleEvents(manager.getCurrentScene());
		}
	}
	GlobalState.mSceneEditing = mData->mSceneTransform.capturesMouse();

	if (!GlobalState.mResPath.empty()) {
		switch (GlobalState.mResType) {
		case File::FileType::MATERIAL: {
			auto res = UTILS::FromJson<RENDER::MaterialResource>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::MaterialResource>>(GlobalState.mResPath, data));
			break;
		}
		case File::FileType::TEXTURE_RES: {
			auto res = UTILS::FromJson<RENDER::TextureResource>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::TextureResource>>(GlobalState.mResPath, data));
			break;
		}
		case File::FileType::SHADER_RES: {
			auto res = UTILS::FromJson<RENDER::ShaderResource>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::ShaderResource>>(GlobalState.mResPath, data));
			break;
		}
		case File::FileType::AUDIO_RES: {
			auto res = UTILS::FromJson<AUDIO::SoundConfig>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<AUDIO::SoundConfig>>(GlobalState.mResPath, data));
			break;
		}
		case File::FileType::MODEL_RES: {
			auto res = UTILS::FromJson<RENDER::ModelResource>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<RENDER::ModelResource>>(GlobalState.mResPath, data));
			break;
		}
		case File::FileType::INPUT_RES: {
			auto res = UTILS::FromJson<INPUT_SYSTEM::InputConfig>(GlobalState.mResPath);
			if (res.isErr()) {
				LOG_ERROR << "Cant open: " << GlobalState.mResType;
				break;
			}
			auto data = res.unwrap();
			mData->mResWindows.push_back(std::make_unique<ResourceEditorWindow<INPUT_SYSTEM::InputConfig>>(GlobalState.mResPath, data));
			break;
		}
		case File::FileType::TRACK_ANIM: {
			if (mData->mTimelineAnimation) {
				mData->mTimelineAnimation->openResource(GlobalState.mResPath);
				mData->mTimelineAnimation->setOpen(true);
			}
			break;
		}
		default:;
		}
		GlobalState.mResPath = "";
	}
	std::erase_if(mData->mResWindows, [](auto& win) { return !win->isOpen(); });
	for (auto& win : mData->mResWindows) {
		win->draw();
	}

	drawPopup();
}

imgui_addons::ImGuiFileBrowser file_dialog;

void EditorRender::drawPopup() {
	// TODO: move to init
	// file_dialog.current_path = Config::ROOT + Config::ASSETS_PATH;

	bool open = true;
	if (GlobalState.mPopupStates.contains("create_new_scene") && GlobalState.mPopupStates["create_new_scene"]) {
		ImGui::OpenPopup("create_new_scene");
		GlobalState.mPopupStates["create_new_scene"] = false;
	}
	if (ImGui::BeginPopupModal("create_new_scene", &open)) {
		ImGui::TextWrapped("Write name for scene");
		static std::string name;
		if (ImGui::InputText("Scene name", &name)) { }

		if (ImGui::Button("OK", ImVec2(120, 0))) {
			RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().loadEmptyScene();
			RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().setCurrentSceneSourcePath(name);
			name = "";
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (GlobalState.mPopupStates.contains("create_new_material") && GlobalState.mPopupStates["create_new_material"]) {
		ImGui::OpenPopup("create_new_material");
		GlobalState.mPopupStates["create_new_material"] = false;
	}
	if (ImGui::BeginPopupModal("create_new_material", &open)) {
		ImGui::TextWrapped("Write name for material");
		static std::string name;
		if (ImGui::InputText("Material name", &name, ImGuiInputTextFlags_EnterReturnsTrue)) {
			auto path = Config::ROOT + Config::USER_ASSETS_PATH + "Materials/" + name + ".mat";
			std::ofstream outfile(path);
			const static std::string materialConf = R"(
{
	"shaderFragment": "Shaders/gl/deferredGBuffer.fs.glsl",
	"shaderVertex": "Shaders/gl/deferredGBuffer.vs.glsl",
	"blendable": false,
	"backfaceCulling": true,
	"frontfaceCulling": false,
	"depthTest": true,
	"depthWriting": true,
	"colorWriting": true,
	"gpuInstances": 1,
	"isDeferred": true,
	"uniforms": {
		"u_AlbedoMap": "textures\\brick_albedo.jpg",
		"u_NormalMap": "textures\\brick_normal.jpg",
		"u_TextureTiling": [ 1.0, 1.0 ],
		"u_TextureOffset": [ 0.0, 0.0 ],
		"u_Albedo": [ 1.0, 0.0, 0.0, 1.0 ],
		"u_Specular": [ 1.0, 1.0, 1.0 ],
		"u_Shininess": 100.0,
		"u_HeightScale": 0.0,
		"u_EnableNormalMapping": true
	}
}
)";
			outfile << materialConf << std::endl;
			outfile.close();
		}

		if (ImGui::Button("OK", ImVec2(120, 0))) {
			RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().loadEmptyScene();
			RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().setCurrentSceneSourcePath(name);
			name = "";
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (GlobalState.mPopupStates.contains("file_chooser") && GlobalState.mPopupStates["file_chooser"]) {
		ImGui::OpenPopup("Open File");
		GlobalState.mPopupStates["file_chooser"] = false;
	}
	if (GlobalState.mFileFormatsCb
		&& file_dialog.showFileDialog(
			"Open File", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), GlobalState.mFileFormatsCb())) {
		GlobalState.mFileChooserCb(file_dialog.selected_path);
		std::cout << file_dialog.selected_fn << std::endl; // The name of the selected file or directory in case of Select Directory dialog mode
		std::cout << file_dialog.selected_path << std::endl; // The absolute path to the selected file
		GlobalState.mFileFormatsCb = nullptr;
		GlobalState.mFileChooserCb = nullptr;
	}
}
#endif
