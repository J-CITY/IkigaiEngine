#include "renderingDemos.h"

#ifdef USE_EDITOR
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/components/forceField2dComponent.h"
#include "coreModule/ecs/components/materialRenderer.h"
#include "coreModule/ecs/components/particleSystemComponent.h"
#include "coreModule/ecs/components/physics3dComponent.h"
#include "coreModule/ecs/components/transform.h"
#include "coreModule/ecs/components/virtualMeshRenderer.h"
#include "imgui.h"
#include "prefabEditor.h"
#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/gameRenderer.h"
#include "renderModule/virtualGeometry/virtualGeometryRenderer.h"
#include "renderModule/virtualGeometry/virtualMeshAsset.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/scene.h"
#include <algorithm>

namespace IKIGAI::EDITOR {
namespace {
	RENDER::GameRenderer* gameRenderer() {
		return dynamic_cast<RENDER::GameRenderer*>(&RESOURCES::ServiceManager::Get<RENDER::GameRendererInterface>());
	}

	constexpr char IndirectWeightUniform[] = "data_UBOIndirectWeight";

	RENDER::PipelineStage* screenProbeCompose(const RENDER::GameRendererInterface& renderer) {
		const auto& pipeline = renderer.getCurrentPipeline();
		if (pipeline.mDescriptor.Name != "hybrid-sp" || !pipeline.mDescriptor.ScreenProbes.Enabled) {
			return nullptr;
		}
		const auto stage = std::ranges::find_if(pipeline.mStages, [](const auto& entry) { return entry->mName == "Compose"; });
		return stage == pipeline.mStages.end() ? nullptr : stage->get();
	}

	void bypassProbeContribution(RENDER::PipelineStage& compose, bool bypass) {
		auto& constants = compose.mResolved.Constants;
		const auto override = std::ranges::find_if(constants, [](const auto& constant) { return constant.first == IndirectWeightUniform; });
		if (bypass && override == constants.end()) {
			constants.emplace_back(IndirectWeightUniform, 0.f);
		}
		else if (!bypass && override != constants.end()) {
			constants.erase(override);
		}
	}

	void placeDemoPanel() {
		const auto* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos({ viewport->Pos.x + 12, viewport->Pos.y + ImGui::GetFrameHeight() + 10 }, ImGuiCond_Appearing);
	}

	bool beginDemoPanel(const char* title) {
		placeDemoPanel();
		return ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
	}

	void resetDemoButton(const char* path) {
		if (ImGui::Button("Reset simulation")) {
			PREFABS::Try([path] { PREFABS::Manager().loadFromFileAsync(path); });
		}
	}

	template <typename Component> void drawWindControl(SCENE_SYSTEM::Scene& scene, const char* name, int axis) {
		const auto wind = scene.findObjectByName(name);
		if (!wind) {
			return;
		}
		if (const auto field = wind->getComponent<Component>()) {
			auto vector = field->getVector();
			ImGui::SetNextItemWidth(170.f);
			if (ImGui::SliderFloat("Wind speed", &vector[axis], -12.f, 12.f, "%.1f m/s")) {
				field->setVector(vector);
			}
		}
	}

	template <int Dimension> void replayParticles(SCENE_SYSTEM::Scene& scene, const char* name, bool eventsEnabled) {
		const auto object = scene.findObjectByName(name);
		if (!object) {
			return;
		}
		if (const auto system = object->getComponent<ECS::ParticleSystemComponent<Dimension>>()) {
			system->setEventSpawnsEnabled(eventsEnabled);
			system->restart(system->getDescriptor().Seed);
		}
	}
}

void ScreenProbeRoomControls::reset() {
	if (!mIndirect) {
		if (auto* game = gameRenderer()) {
			if (auto* compose = screenProbeCompose(*game)) {
				bypassProbeContribution(*compose, false);
			}
		}
	}
	mIndirect = true;
}

void ScreenProbeRoomControls::draw(SCENE_SYSTEM::Scene& scene) {
	auto* game = gameRenderer();
	if (!game) {
		return;
	}
	placeDemoPanel();
	if (ImGui::Begin("Lumen-style | Screen Probe GI", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		const auto features = RENDER::DriverInterface::Get()->gpuFeatures();
		if (auto* compose = screenProbeCompose(*game); compose && features.Compute && features.HalfFloatTextures) {
			const bool changed = ImGui::Checkbox("Indirect lighting", &mIndirect);
			// Keep the probe history warm during the comparison and bypass its contribution in composition.
			if (changed || !mIndirect) {
				bypassProbeContribution(*compose, !mIndirect);
			}
		}
		else {
			ImGui::TextUnformatted("Screen probes require the hybrid-sp pipeline and compute support.");
		}
		if (const auto light = scene.findObjectByName("Bounce light")) {
			auto* transform = light->tryGetTransform();
			auto position = transform->getLocalPosition();
			ImGui::SetNextItemWidth(170.f);
			if (ImGui::SliderFloat("Light position", &position.x, -3.3f, 3.3f, "%.2f m")) {
				transform->setLocalPosition(position);
			}
		}
	}
	ImGui::End();
}

void DrawVirtualGeometryGallery(SCENE_SYSTEM::Scene& scene) {
	auto* game = gameRenderer();
	const auto centerRing = scene.findObjectByName("Bronze ring");
	if (!game || !centerRing) {
		return;
	}
	const auto mesh = centerRing->getComponent<ECS::VirtualMeshRenderer>();
	if (!mesh) {
		return;
	}
	placeDemoPanel();
	if (ImGui::Begin("Nanite-style | Virtual Geometry", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		const auto& renderer = game->virtualGeometry();
		const auto& frame = renderer.lastFrame();
		if (const auto& asset = mesh->getAsset()) {
			ImGui::Text("Source triangles per ring  %u", asset->leafTriangleCount());
		}
		else {
			ImGui::TextWrapped("%s", mesh->getLoadError().c_str());
		}
		if (frame.ClusterInstances) {
			ImGui::Text("Visible  %u clusters / %u triangles", frame.VisibleClusters, frame.VisibleTriangles);
			ImGui::Text("Selection  %s", frame.GpuSelection ? "GPU" : "CPU");
		}
		else if (frame.FallbackInstances) {
			ImGui::TextUnformatted("Fallback meshes active. Cluster rendering requires Vulkan or DirectX 12.");
		}

		ImGui::BeginDisabled(!frame.ClusterInstances);
		int debug = static_cast<int>(mesh->getDebugMode());
		float threshold = mesh->getErrorThreshold();
		ImGui::SetNextItemWidth(155.f);
		const bool debugChanged = ImGui::Combo("Color by", &debug, "Material\0Cluster\0LOD level\0Group\0");
		ImGui::SetNextItemWidth(155.f);
		const bool thresholdChanged = ImGui::SliderFloat("LOD error", &threshold, 0.f, 12.f, "%.1f px");
		ImGui::EndDisabled();
		if (debugChanged || thresholdChanged) {
			for (const auto& object : scene.getObjects()) {
				if (const auto ring = object->getComponent<ECS::VirtualMeshRenderer>()) {
					ring->setDebugMode(static_cast<VG::VirtualMeshDebugMode>(debug));
					ring->setErrorThreshold(threshold);
				}
			}
		}

		if (const auto camera = scene.findMainCamera()) {
			const auto transform = camera->obj->tryGetTransform();
			if (ImGui::Button("Near")) {
				transform->setLocalPosition({ 0, 2.75f, -4.7f });
			}
			ImGui::SameLine();
			if (ImGui::Button("Gallery")) {
				transform->setLocalPosition({ 0, 3.3f, -9.8f });
			}
			ImGui::SameLine();
			if (ImGui::Button("Far")) {
				transform->setLocalPosition({ 0, 4.3f, -18.f });
			}
		}
	}
	ImGui::End();
}

void DrawFurRabbit(SCENE_SYSTEM::Scene& scene) {
	const auto rabbit = scene.findObjectByName("Rabbit");
	if (!rabbit) {
		return;
	}
	const auto materials = rabbit->getComponent<ECS::MaterialRenderer>();
	if (!materials) {
		return;
	}
	const auto& names = materials->getMaterialNames();
	const auto coat = std::ranges::find(names, "Coat");
	if (coat == names.end() || !materials->GetMaterialAtIndex(static_cast<unsigned>(coat - names.begin()))) {
		return;
	}
	if (beginDemoPanel("Rabbit | Shell Fur")) {
		const auto current = materials->GetMaterialAtIndex(static_cast<unsigned>(coat - names.begin()));
		auto deformation = current->deformation();
		bool enabled = deformation.Mode == RENDER::DeformationMode::Fur;
		bool changed = ImGui::Checkbox("Show fur", &enabled);
		ImGui::BeginDisabled(!enabled);
		ImGui::SetNextItemWidth(145.f);
		changed |= ImGui::SliderFloat("Fur length", &deformation.FurLength, 0.025f, 0.22f, "%.3f m");
		ImGui::SetNextItemWidth(145.f);
		changed |= ImGui::SliderFloat("Density", &deformation.FurDensity, 0.25f, 1.f);
		int shells = static_cast<int>(deformation.FurShells);
		ImGui::SetNextItemWidth(145.f);
		changed |= ImGui::SliderInt("Shells", &shells, 2, 32);
		ImGui::EndDisabled();
		if (changed) {
			deformation.Mode = enabled ? RENDER::DeformationMode::Fur : RENDER::DeformationMode::None;
			deformation.FurShells = static_cast<unsigned>(shells);
			PREFABS::Try([&] {
				for (const char* name : { "Coat", "Tail" }) {
					const auto slot = std::ranges::find(names, name);
					if (slot == names.end()) {
						continue;
					}
					const auto index = static_cast<unsigned>(slot - names.begin());
					if (const auto material = materials->GetMaterialAtIndex(index)) {
						auto descriptor = material->getDescriptor();
						descriptor.Deformation = deformation;
						materials->setMaterial(index, RENDER::DriverInterface::Get()->createMaterial(descriptor));
					}
				}
			});
		}
		auto rotation = rabbit->tryGetTransform()->getLocalRotationDeg();
		ImGui::SetNextItemWidth(145.f);
		if (ImGui::SliderFloat("Turntable", &rotation.y, -180.f, 180.f, "%.0f deg")) {
			rabbit->tryGetTransform()->setLocalRotationDeg(rotation);
		}
	}
	ImGui::End();
}

void DrawClothWind(SCENE_SYSTEM::Scene& scene, bool planar) {
	if (beginDemoPanel(planar ? "2D Cloth | Wind" : "3D Cloth | Wind")) {
		if (planar) {
#if USE_PHYSICS_2D
			drawWindControl<ECS::ForceField2DComponent>(scene, "2D Wind", 0);
#endif
		}
		else {
#if USE_PHYSICS_3D
			drawWindControl<ECS::ForceField3DComponent>(scene, "Wind", 2);
#endif
		}
		resetDemoButton(planar ? Cloth2DPath : Cloth3DPath);
	}
	ImGui::End();
}

void DrawSoftBodyDemo() {
	if (beginDemoPanel("3D Soft Body")) {
		resetDemoButton(SoftBodyPath);
	}
	ImGui::End();
}

void DrawParticleEvents(SCENE_SYSTEM::Scene& scene) {
	const auto firework = scene.findObjectByName("Firework events");
	if (!firework) {
		return;
	}
	const auto effect = firework->getComponent<ECS::ParticleSystem2DComponent>();
	if (!effect || !effect->runtime()) {
		return;
	}
	if (beginDemoPanel("Particle Events")) {
		bool enabled = effect->runtime()->eventSpawnsEnabled();
		const bool changed = ImGui::Checkbox("Secondary particles", &enabled);
		if (ImGui::Button("Replay effects") || changed) {
			replayParticles<2>(scene, "Firework events", enabled);
			replayParticles<3>(scene, "Mesh death sparks", enabled);
		}
		ImGui::TextUnformatted("Launch and fragment particles trigger bursts on death.");
	}
	ImGui::End();
}
}
#endif
