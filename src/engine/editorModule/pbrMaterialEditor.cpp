#include "pbrMaterialEditor.h"
#ifdef USE_EDITOR
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "renderModule/backends/interface/materialInterface.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "utilsModule/jsonLoader.h"
#include <algorithm>
#include <future>

namespace IKIGAI::EDITOR {
namespace {
	void drawLayer(RENDER::PbrLayerDescriptor& layer) {
		ImGui::Checkbox("Enabled", &layer.Enabled);
		ImGui::ColorEdit4("Color", &layer.Color.x);
		ImGui::SliderFloat("Weight", &layer.Weight, 0.0f, 1.0f);
		ImGui::SliderFloat("Metallic", &layer.Metallic, 0.0f, 1.0f);
		ImGui::SliderFloat("Roughness", &layer.Roughness, 0.0f, 1.0f);
		ImGui::SliderFloat("Material AO", &layer.AmbientOcclusion, 0.0f, 1.0f);
		ImGui::SliderFloat("Normal strength", &layer.NormalStrength, 0.0f, 4.0f);
		ImGui::InputText("Albedo map", &layer.AlbedoMap);
		ImGui::InputText("Normal map", &layer.NormalMap);
		ImGui::InputText("ORM map (R=AO, G=roughness, B=metallic)", &layer.OrmMap);
		ImGui::InputText("Height map", &layer.HeightMap);
		ImGui::InputText("Mask map", &layer.MaskMap);
		ImGui::DragFloat2("Tiling", &layer.Tiling.x, 0.05f, 0.001f, 1000.0f);
		ImGui::DragFloat2("Offset", &layer.Offset.x, 0.01f);
		ImGui::DragFloat2("Mask tiling", &layer.MaskTiling.x, 0.05f, 0.001f, 1000.0f);
		ImGui::DragFloat2("Mask offset", &layer.MaskOffset.x, 0.01f);
		int channel = static_cast<int>(layer.MaskChannel);
		if (ImGui::Combo("Mask channel", &channel, "R\0G\0B\0A\0")) {
			layer.MaskChannel = static_cast<RENDER::PbrMaskChannel>(channel);
		}
		ImGui::Checkbox("Invert mask", &layer.InvertMask);
		int mode = static_cast<int>(layer.BlendMode);
		if (ImGui::Combo("Blend mode", &mode, "Linear\0Height\0")) {
			layer.BlendMode = static_cast<RENDER::PbrBlendMode>(mode);
		}
		int normals = static_cast<int>(layer.NormalMode);
		if (ImGui::Combo("Normal mode", &normals, "Replace\0Detail (RNM)\0")) {
			layer.NormalMode = static_cast<RENDER::PbrNormalMode>(normals);
		}
		ImGui::DragFloat("Height scale", &layer.HeightScale, .01f, 0.0f, 65536.0f);
		ImGui::DragFloat("Height bias", &layer.HeightBias, .01f);
		ImGui::SliderFloat("Height influence", &layer.HeightInfluence, 0.0f, 16.0f);
		ImGui::SliderFloat("Transition width", &layer.TransitionWidth, .0001f, 1.0f);
	}
}

void DrawPbrMaterialEditor(RENDER::MaterialInterface& material) {
	static const RENDER::MaterialInterface* selected = nullptr;
	static RENDER::MaterialResource draft;
	static std::string error;
	static std::shared_future<void> save;
	if (selected != &material) {
		selected = &material;
		draft = material.getDescriptor();
		error.clear();
	}
	if (save.valid() && save.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		try {
			save.get();
		} catch (const std::exception& exception) {
			error = exception.what();
		}
		save = {};
	}
	ImGui::TextUnformatted(material.getPath().c_str());
	ImGui::TextUnformatted("Apply previews the material on the selected object. Save writes the .mat file.");
	const bool apply = ImGui::Button("Apply");
	ImGui::SameLine();
	const bool write = ImGui::Button("Save") && !save.valid();
	ImGui::SameLine();
	const bool reload = ImGui::Button("Reload");
	try {
		if (reload) {
			auto loaded = UTILS::FromJson<RENDER::MaterialResource>(material.getPath());
			if (loaded.isErr()) {
				throw std::runtime_error(loaded.unwrapErr().text);
			}
			draft = loaded.unwrap();
			draft.path = material.getPath();
		}
		if (apply || write || reload) {
			material.create(draft);
			error.clear();
			if (write) {
				auto serialized = UTILS::ToJson(material.getDescriptor());
				if (serialized.isErr()) {
					throw std::runtime_error(serialized.unwrapErr().text);
				}
				save = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().writeTextAtomic(material.getPath(), serialized.unwrap().dump(2));
			}
		}
	} catch (const std::exception& exception) {
		error = exception.what();
	}
	if (!error.empty()) {
		ImGui::TextWrapped("%s", error.c_str());
	}

	if (ImGui::CollapsingHeader("Base PBR", ImGuiTreeNodeFlags_DefaultOpen)) {
		auto& color = draft.Uniforms["data_UBOu_Albedo"];
		if (!std::holds_alternative<MATH::Vector4f>(color)) {
			color = MATH::Vector4f { 1, 1, 1, 1 };
		}
		ImGui::ColorEdit4("Base color", &std::get<MATH::Vector4f>(color).x);
		for (const auto* property : { "Metallic", "Roughness" }) {
			const auto key = std::string("data_UBOu_") + property;
			auto& value = draft.Uniforms[key];
			if (!std::holds_alternative<float>(value)) {
				value = 1.0f;
			}
			ImGui::SliderFloat(property, &std::get<float>(value), 0.0f, 1.0f);
		}
		for (const auto* name : { "u_AlbedoMap", "u_NormalMap", "u_MetallicMap", "u_RoughnessMap", "u_AmbientOcclusionMap", "u_HeightMap" }) {
			std::string path;
			if (const auto found = draft.Uniforms.find(name); found != draft.Uniforms.end()) {
				if (const auto* value = std::get_if<std::string>(&found->second)) {
					path = *value;
				}
			}
			if (ImGui::InputText(name, &path)) {
				if (path.empty()) {
					draft.Uniforms.erase(name);
				}
				else {
					draft.Uniforms[name] = path;
				}
			}
		}
		bool normalMapping = false;
		if (const auto found = draft.Uniforms.find("data_UBOu_EnableNormalMapping"); found != draft.Uniforms.end()) {
			if (const auto* value = std::get_if<bool>(&found->second)) {
				normalMapping = *value;
			}
		}
		if (ImGui::Checkbox("Base normal mapping", &normalMapping)) {
			draft.Uniforms["data_UBOu_EnableNormalMapping"] = normalMapping;
		}
	}
	ImGui::Checkbox("Deferred", &draft.IsDeferred);
	ImGui::Checkbox("Depth test", &draft.DepthTest);
	ImGui::Checkbox("Depth writing", &draft.DepthWriting);
	ImGui::Checkbox("Back-face culling", &draft.BackfaceCulling);
	if (ImGui::TreeNode("Wind, interaction and fur")) {
		int deformation = int(draft.Deformation.Mode);
		if (ImGui::Combo("Deformation", &deformation, "None\0Grass\0Foliage\0Fur\0")) {
			draft.Deformation.Mode = RENDER::DeformationMode(deformation);
		}
		ImGui::SliderFloat("Plant height", &draft.Deformation.Height, .01f, 10);
		ImGui::DragFloat("Root height", &draft.Deformation.RootHeight, .01f);
		ImGui::SliderFloat("Stiffness", &draft.Deformation.Stiffness, 0, 1);
		ImGui::SliderFloat("Maximum bend", &draft.Deformation.MaxBend, 0, 3);
		ImGui::SliderFloat("Wind strength", &draft.Deformation.WindStrength, 0, 2);
		ImGui::SliderFloat("Leaf flutter", &draft.Deformation.Flutter, 0, 1);
		ImGui::DragFloat3("Branch pivot", &draft.Deformation.Pivot.x, .01f);
		ImGui::SliderFloat("Fur length", &draft.Deformation.FurLength, 0, 1);
		int shells = int(draft.Deformation.FurShells);
		if (ImGui::SliderInt("Fur shell budget", &shells, 2, 32)) {
			draft.Deformation.FurShells = unsigned(shells);
		}
		ImGui::SliderFloat("Fur density", &draft.Deformation.FurDensity, 0, 1);
		ImGui::SliderFloat("Weather response", &draft.Deformation.WeatherResponse, 0, 1);
		ImGui::TreePop();
	}
	int alpha = static_cast<int>(draft.PbrLayers.AlphaMode);
	if (ImGui::Combo("Alpha mode", &alpha, "Opaque\0Masked\0Blend\0")) {
		draft.PbrLayers.AlphaMode = static_cast<RENDER::PbrAlphaMode>(alpha);
	}
	ImGui::SliderFloat("Alpha cutoff", &draft.PbrLayers.AlphaCutoff, 0.0f, 1.0f);
	ImGui::DragFloat("Base height scale", &draft.PbrLayers.BaseHeightScale, .01f, 0.0f, 65536.0f);
	ImGui::DragFloat("Base height bias", &draft.PbrLayers.BaseHeightBias, .01f);
	if (draft.PbrLayers.Layers.size() < 3 && ImGui::Button("Add layer")) {
		draft.PbrLayers.Layers.emplace_back();
	}
	for (size_t index = 0; index < draft.PbrLayers.Layers.size(); ++index) {
		ImGui::PushID(static_cast<int>(index));
		const auto title = "Layer " + std::to_string(index + 1);
		if (ImGui::TreeNode(title.c_str())) {
			auto& layers = draft.PbrLayers.Layers;
			if (index > 0 && ImGui::Button("Move up")) {
				std::swap(layers[index], layers[index - 1]);
			}
			if (index + 1 < layers.size() && ImGui::Button("Move down")) {
				std::swap(layers[index], layers[index + 1]);
			}
			const bool remove = ImGui::Button("Remove");
			if (!remove) {
				drawLayer(layers[index]);
			}
			ImGui::TreePop();
			if (remove) {
				layers.erase(layers.begin() + index);
				ImGui::PopID();
				break;
			}
		}
		ImGui::PopID();
	}
}
} // namespace IKIGAI::EDITOR
#endif
