#include "surfaceEditor.h"
#ifdef USE_EDITOR
#include "coreModule/ecs/componentReference.inl"
#include "coreModule/ecs/components/chunkModelRenderer.h"
#include "coreModule/ecs/components/foliageComponent.h"
#include "coreModule/ecs/components/surfaceComponent.h"
#include "coreModule/ecs/object.h"
#include "prefabEditor.h"
#include <algorithm>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <vector>

namespace IKIGAI::EDITOR {
namespace {
	template <class T> struct Draft {
		UTILS::WeakPtr<T> Owner = {};
		typename T::Descriptor Value = {};
		std::string Error = {};
	};
	template <class T> Draft<T>& draft(T& component) {
		static std::vector<Draft<T>> drafts;
		std::erase_if(drafts, [](const auto& value) { return !value.Owner; });
		for (auto& value : drafts) {
			if (value.Owner.get() == &component) {
				return value;
			}
		}
		drafts.push_back({ UTILS::WeakPtr<T>(&component), component.getDescriptor() });
		return drafts.back();
	}
	void unsignedInput(const char* label, unsigned& value) {
		int number = int(value);
		if (ImGui::InputInt(label, &number)) {
			value = unsigned(std::max(0, number));
		}
	}
	void noiseEditor(SURFACE::NoiseSettings& settings) {
		unsignedInput("Seed", settings.Seed);
		ImGui::DragFloat("Base height", &settings.BaseHeight, .1f);
		ImGui::DragFloat("Amplitude", &settings.Amplitude, .1f, 0, 10000);
		ImGui::DragFloat("Frequency", &settings.Frequency, .0001f, .00001f, 1, "%.5f");
		unsignedInput("Octaves", settings.Octaves);
		ImGui::SliderFloat("Persistence", &settings.Persistence, 0, 1);
		ImGui::SliderFloat("Lacunarity", &settings.Lacunarity, 1, 3);
		ImGui::SliderFloat("Ridge", &settings.Ridge, 0, 1);
		ImGui::DragFloat("Domain warp", &settings.Warp, .1f, 0, 1000);
		ImGui::DragFloat("Terrace height", &settings.TerraceHeight, .1f, 0, 1000);
	}
	template <class T, class Apply> void actions(Draft<T>& state, T& component, Apply apply) {
		if (ImGui::Button("Apply and regenerate")) {
			try {
				apply();
				state.Value = component.getDescriptor();
				state.Error.clear();
			} catch (const std::exception& error) {
				state.Error = error.what();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Reload settings")) {
			state.Value = component.getDescriptor();
			state.Error.clear();
		}
		if (!state.Error.empty()) {
			ImGui::TextWrapped("%s", state.Error.c_str());
		}
	}
}
void DrawSurfaceEditor(ECS::SurfaceComponent& component) {
	ImGui::PushID(&component);
	auto& state = draft(component);
	auto& settings = state.Value.Surface;
	unsignedInput("Tiles X", settings.TilesX);
	unsignedInput("Tiles Z", settings.TilesZ);
	ImGui::DragFloat("Tile size", &settings.TileSize, .5f, 1, 1024);
	unsignedInput("Resolution (power of two)", settings.Resolution);
	if (ImGui::TreeNode("Geometry")) {
		noiseEditor(settings.Noise);
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Surface materials")) {
		ImGui::Checkbox("Smooth coating transitions", &settings.Blend.Enabled);
		if (settings.Blend.Enabled) {
			unsignedInput("Coverage map resolution", settings.Blend.MapResolution);
			ImGui::DragFloat("Boundary noise", &settings.Blend.BoundaryNoise, .05f, 0, 1000);
			ImGui::DragFloat("Boundary frequency", &settings.Blend.NoiseFrequency, .001f, .001f, 10);
			ImGui::SliderFloat("Texture height influence", &settings.Blend.HeightInfluence, 0, 1);
			ImGui::SliderFloat("Texture transition width", &settings.Blend.TransitionWidth, .01f, 1);
			ImGui::TextWrapped("Up to four opaque coatings. Paint uses stable coating IDs.");
		}
		for (size_t index = 0; index < settings.Layers.size(); ++index) {
			ImGui::PushID(int(index));
			auto& layer = settings.Layers[index];
			ImGui::InputText("Name", &layer.Name);
			unsignedInput("Stable coating ID", layer.Id);
			ImGui::InputText("Material asset", &layer.Material);
			ImGui::DragFloat("Minimum height", &layer.MinHeight, .5f);
			ImGui::DragFloat("Maximum height", &layer.MaxHeight, .5f);
			ImGui::SliderFloat("Minimum slope", &layer.MinSlope, 0, 90);
			ImGui::SliderFloat("Maximum slope", &layer.MaxSlope, 0, 90);
			if (settings.Blend.Enabled) {
				ImGui::DragFloat("Height transition", &layer.HeightTransition, .1f, 0, 10000);
				ImGui::SliderFloat("Slope transition", &layer.SlopeTransition, 0, 90);
			}
			ImGui::DragFloat("Texture repeats per unit", &layer.TextureScale, .01f, .001f, 100);
			if (index > 0 && ImGui::Button("Remove layer")) {
				settings.Layers.erase(settings.Layers.begin() + index);
				ImGui::PopID();
				break;
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (ImGui::Button("Add layer") && settings.Layers.size() < 32) {
			SURFACE::Layer layer;
			layer.Name = "Layer " + std::to_string(settings.Layers.size());
			for (unsigned index = 0; index < settings.Layers.size(); ++index) {
				layer.Id = std::max(layer.Id, settings.stableLayerId(index));
			}
			++layer.Id;
			settings.Layers.push_back(layer);
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Water")) {
		ImGui::Checkbox("Enabled", &settings.Water.Enabled);
		ImGui::DragFloat("Level", &settings.Water.Level, .1f);
		ImGui::DragFloat("Texture scale", &settings.Water.TextureScale, .01f, .001f, 100);
		ImGui::InputText("Water material", &settings.Water.Material);
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("LOD and visibility")) {
		for (size_t index = 0; index < settings.LodDistances.size(); ++index) {
			ImGui::PushID(int(index));
			ImGui::DragFloat("Switch distance", &settings.LodDistances[index], 1, 1, 10000);
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove")) {
				settings.LodDistances.erase(settings.LodDistances.begin() + index);
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		if (ImGui::Button("Add LOD") && settings.LodDistances.size() < 7) {
			settings.LodDistances.push_back(settings.LodDistances.empty() ? 50 : settings.LodDistances.back() * 2);
		}
		ImGui::DragFloat("View distance", &settings.ViewDistance, 1, 1, 10000);
		ImGui::DragFloat("Skirt depth", &settings.SkirtDepth, .1f, 0, 1000);
		unsignedInput("Mesh build budget", settings.BuildBudget);
		unsignedInput("Mesh cache (MiB)", settings.CacheMegabytes);
		ImGui::Checkbox("Background updates", &settings.BackgroundUpdates);
		unsignedInput("Pending mesh jobs", settings.PendingBuilds);
		ImGui::SliderFloat("Upload budget (ms)", &settings.UploadMilliseconds, .1f, 20);
		ImGui::TreePop();
	}
	actions(state, component, [&] {
		settings.Edits = component.settings().Edits;
		settings.validate();
		// Preserve the live terrain if a material-layer edit would invalidate foliage.
		for (const auto& object : PREFABS::Manager().getCurrentScene().getObjects()) {
			if (auto foliage = object->getComponent<ECS::FoliageComponent>()) {
				const auto descriptor = foliage->getDescriptor();
				const bool depends = descriptor.Surface.empty()
					? foliage->obj->getID() == component.obj->getID()
					: descriptor.Surface.ObjectId == static_cast<int>(component.obj->getID().getUniqueId());
				if (depends) {
					for (const auto& species : descriptor.Species) {
						SURFACE::validateScatter(settings, species);
					}
				}
			}
		}
		component.apply(settings);
	});
	ImGui::PopID();
}
void DrawSurfaceEditor(ECS::ChunkModelRenderer& component) {
	ImGui::PushID(&component);
	auto& state = draft(component);
	auto& settings = state.Value.Voxels;
	unsignedInput("Chunks X", settings.ChunksX);
	unsignedInput("Chunks Y", settings.ChunksY);
	unsignedInput("Chunks Z", settings.ChunksZ);
	unsignedInput("Chunk size (power of two)", settings.ChunkSize);
	ImGui::DragFloat("Block size", &settings.BlockSize, .1f, .01f, 100);
	if (ImGui::TreeNode("Geometry")) {
		noiseEditor(settings.Noise);
		ImGui::TreePop();
	}
	ImGui::Checkbox("Water blocks", &settings.Water);
	ImGui::DragFloat("Sea level", &settings.SeaLevel, .1f);
	ImGui::DragFloat("Sand height", &settings.SandHeight, .1f);
	ImGui::DragFloat("Snow height", &settings.SnowHeight, .1f);
	ImGui::DragFloat("Soil depth", &settings.SoilDepth, .1f, 0, 1000);
	ImGui::SliderFloat("Cave threshold (1 disables)", &settings.CaveThreshold, 0, 1);
	ImGui::DragFloat("Cave frequency", &settings.CaveFrequency, .001f, .001f, 1);
	ImGui::DragFloat("Texture scale", &settings.TextureScale, .01f, .001f, 100);
	ImGui::DragFloat("View distance", &settings.ViewDistance, 1, 1, 10000);
	unsignedInput("Mesh build budget", settings.BuildBudget);
	unsignedInput("Mesh cache (MiB)", settings.CacheMegabytes);
	ImGui::TextWrapped("MaterialRenderer slots: 0 ground, 1 rock, 2 sand, 3 snow, 4 water. Regeneration resets block edits.");
	actions(state, component, [&] { component.apply(settings); });
	ImGui::PopID();
}
void DrawSurfaceEditor(ECS::FoliageComponent& component) {
	ImGui::PushID(&component);
	auto& state = draft(component);
	auto& descriptor = state.Value;
	ImGui::InputInt("Surface object ID (0 = this object)", &descriptor.Surface.ObjectId);
	descriptor.Surface.Component = "SurfaceComponent";
	for (size_t index = 0; index < descriptor.Species.size(); ++index) {
		ImGui::PushID(int(index));
		auto& species = descriptor.Species[index];
		if (ImGui::TreeNode("Species", "%s", species.Name.c_str())) {
			ImGui::InputText("Name", &species.Name);
			unsignedInput("Seed", species.Seed);
			ImGui::DragFloat("Instances per square unit", &species.Density, .01f, 0, 100);
			unsignedInput("Maximum instances", species.MaxInstances);
			ImGui::DragFloat("Minimum height", &species.MinHeight, .1f);
			ImGui::DragFloat("Maximum height", &species.MaxHeight, .1f);
			ImGui::SliderFloat("Maximum slope", &species.MaxSlope, 0, 90);
			ImGui::DragFloat("Minimum scale", &species.MinScale, .01f, .01f, 1000);
			ImGui::DragFloat("Maximum scale", &species.MaxScale, .01f, .01f, 1000);
			ImGui::DragFloat("Normal offset", &species.Offset, .01f);
			ImGui::DragFloat("View distance", &species.ViewDistance, 1, 1, 10000);
			ImGui::Checkbox("Align to normal", &species.AlignToNormal);
			ImGui::Checkbox("Allow underwater", &species.AllowUnderwater);
			ImGui::Checkbox("GPU instancing", &species.Instancing);
			ImGui::SliderFloat3("Minimum tint", &species.MinTint.x, 0, 1);
			ImGui::SliderFloat3("Maximum tint", &species.MaxTint.x, 0, 1);
			ImGui::DragFloat("Distance fade", &species.FadeDistance, .5f, 0, species.ViewDistance);
			if (ImGui::TreeNode("Model material slots")) {
				for (size_t material = 0; material < species.Materials.size(); ++material) {
					ImGui::InputText(("Slot " + std::to_string(material)).c_str(), &species.Materials[material]);
				}
				if (ImGui::Button("Add material slot") && species.Materials.size() < 32) {
					species.Materials.emplace_back();
				}
				ImGui::SameLine();
				if (ImGui::Button("Remove last slot") && !species.Materials.empty()) {
					species.Materials.pop_back();
				}
				ImGui::TreePop();
			}
			for (size_t layer = 0; layer < species.Layers.size(); ++layer) {
				ImGui::PushID(int(layer));
				ImGui::InputText("Allowed surface layer", &species.Layers[layer]);
				ImGui::PopID();
			}
			if (ImGui::Button("Add layer filter")) {
				species.Layers.push_back("Ground");
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear layer filters")) {
				species.Layers.clear();
			}
			for (size_t lod = 0; lod < species.Lods.size(); ++lod) {
				ImGui::PushID(int(lod));
				auto& model = species.Lods[lod];
				ImGui::InputText("Model asset", &model.Model);
				ImGui::InputText("Material override", &model.Material);
				ImGui::DragFloat("Switch distance", &model.Distance, 1, 1, 10000);
				ImGui::Checkbox("Billboard / authored impostor", &model.Billboard);
				if (ImGui::Button("Remove model")) {
					species.Lods.erase(species.Lods.begin() + lod);
					ImGui::PopID();
					break;
				}
				ImGui::Separator();
				ImGui::PopID();
			}
			if (ImGui::Button("Add model LOD")) {
				SURFACE::FoliageLod lod;
				lod.Distance = species.Lods.empty() ? 50 : species.Lods.back().Distance * 2;
				species.Lods.push_back(lod);
			}
			if (ImGui::Button("Remove species")) {
				descriptor.Species.erase(descriptor.Species.begin() + index);
				ImGui::TreePop();
				ImGui::PopID();
				break;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (ImGui::Button("Add species")) {
		SURFACE::FoliageSpecies species;
		species.Name = "Species " + std::to_string(descriptor.Species.size());
		descriptor.Species.push_back(species);
	}
	actions(state, component, [&] {
		// Validate names against the actual target before replacing the live settings.
		auto& scene = PREFABS::Manager().getCurrentScene();
		auto target = descriptor.Surface.empty() ? component.obj->getComponent<ECS::SurfaceComponent>()
												 : ECS::Resolve<ECS::SurfaceComponent>(scene, descriptor.Surface);
		if (!target) {
			throw std::invalid_argument("Select an existing SurfaceComponent");
		}
		for (const auto& species : descriptor.Species) {
			for (const auto& layer : species.Layers) {
				target->settings().layerId(layer);
			}
			SURFACE::validateScatter(target->settings(), species);
		}
		component.apply(descriptor);
		component.bind(scene);
	});
	ImGui::PopID();
}
}
#endif
