#include "surfaceBrushWindow.h"
#ifdef USE_EDITOR
#include "coreModule/ecs/components/foliageComponent.h"
#include "coreModule/ecs/components/surfaceComponent.h"
#include "prefabEditor.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "renderModule/gameRenderer.h"
#include "surfaceModule/surfacePicking.h"
#include "surfaceModule/surfaceRuntime.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>

namespace IKIGAI::EDITOR {
namespace {
	ECS::SurfaceComponent& target(SCENE_SYSTEM::Scene& scene, int id) {
		auto object = scene.findObjectByID(ECS::Entity(ECS::Entity::ID(id)));
		if (!object || !object->getComponent<ECS::SurfaceComponent>()) {
			throw std::runtime_error("Surface no longer exists");
		}
		return *object->getComponent<ECS::SurfaceComponent>();
	}
	void applyEdits(SCENE_SYSTEM::Scene& scene, int id, const std::vector<SURFACE::EditLayer>& layers) {
		auto& component = target(scene, id);
		auto settings = component.settings();
		settings.Edits = layers;
		component.apply(settings);
	}
	void recordEdits(int id, std::vector<SURFACE::EditLayer> before, std::vector<SURFACE::EditLayer> after, bool applied) {
		EditorRender::GlobalState.mCommandHistory.addCommand(std::make_unique<PREFABS::Command>(
			"Paint surface", [id, after](auto& scene) { applyEdits(scene, id, after); }, [id, before](auto& scene) { applyEdits(scene, id, before); },
			applied));
	}
	MATH::Vector3f transformedPoint(const MATH::Matrix4f& matrix, const MATH::Vector3f& point, float w) {
		const auto value = matrix * MATH::Vector4f { point.x, point.y, point.z, w };
		return { value.x, value.y, value.z };
	}
	MATH::Vector3f unproject(const MATH::Matrix4f& inverse, float x, float y, float z) {
		const auto value = inverse * MATH::Vector4f { x, y, z, 1 };
		return { value.x / value.w, value.y / value.w, value.z / value.w };
	}
}
struct SurfaceBrushWindow::State {
	int TargetId = 0;
	unsigned EditLayerId = 2;
	SURFACE::BrushStroke Brush = {};
	bool PickFlattenHeight = true;
	bool LivePreview = true;
	bool Dragging = false;
	bool PreviewApplied = false;
	std::weak_ptr<void> SceneLifetime = {};
	std::vector<SURFACE::EditLayer> Before = {};
	std::vector<SURFACE::EditLayer> Working = {};
	MATH::Vector3f LastStamp = {};
	double LastStampTime = 0;
	double LastPreviewTime = 0;
	float NavigationSpeed = 12;
	std::string Error = {};

	void cancel() {
		if (Dragging && PreviewApplied && !SceneLifetime.expired() && PREFABS::Manager().hasCurrentScene()
			&& SceneLifetime.lock() == PREFABS::Manager().getCurrentScene().lifetime && !PREFABS::Manager().isEditingLocked()) {
			applyEdits(PREFABS::Manager().getCurrentScene(), TargetId, Before);
		}
		Dragging = false;
		PreviewApplied = false;
		Before.clear();
		Working.clear();
	}
	void stamp(ECS::SurfaceComponent& component, MATH::Vector3f position, bool inverse) {
		if (!Dragging) {
			Before = component.settings().Edits;
			Working = Before;
			SceneLifetime = PREFABS::Manager().getCurrentScene().lifetime;
			Dragging = true;
			PreviewApplied = false;
			if (Brush.Operation == SURFACE::BrushOperation::FlattenHeight && PickFlattenHeight) {
				Brush.Value = position.y;
			}
		}
		auto layer = std::find_if(Working.begin(), Working.end(), [&](const auto& value) { return value.Id == EditLayerId; });
		if (layer == Working.end()) {
			Working.push_back({ EditLayerId, "Painted edits " + std::to_string(EditLayerId) });
			layer = std::prev(Working.end());
		}
		auto stroke = Brush;
		stroke.Center = { position.x, position.z };
		if (inverse) {
			if (stroke.Operation == SURFACE::BrushOperation::AddHeight) {
				stroke.Value = -stroke.Value;
			}
			if (stroke.Operation == SURFACE::BrushOperation::PaintLayer) {
				stroke.LayerId = component.settings().stableLayerId(0);
			}
			if (stroke.Operation == SURFACE::BrushOperation::FoliageDensity) {
				stroke.Value = 1 - stroke.Value;
			}
		}
		layer->Strokes.push_back(stroke);
		LastStamp = position;
		LastStampTime = ImGui::GetTime();
	}
	void preview() {
		applyEdits(PREFABS::Manager().getCurrentScene(), TargetId, Working);
		PreviewApplied = true;
		LastPreviewTime = ImGui::GetTime();
	}
	void commit() {
		if (!Dragging) {
			return;
		}
		if (PreviewApplied) {
			preview();
		}
		recordEdits(TargetId, Before, Working, PreviewApplied);
		Dragging = false;
		Before.clear();
		Working.clear();
		PreviewApplied = false;
	}
};
SurfaceBrushWindow::SurfaceBrushWindow()
	: mState(std::make_unique<State>()) {
	mIsOpen = false;
}
SurfaceBrushWindow::~SurfaceBrushWindow() = default;
bool SurfaceBrushWindow::isPainting() const {
	return mState->Dragging;
}
void SurfaceBrushWindow::draw() {
	auto& state = *mState;
	if (!mIsOpen) {
		if (state.Dragging) {
			PREFABS::Try([&] { state.cancel(); });
		}
		return;
	}
	const auto viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos({ viewport->WorkPos.x + 12, viewport->WorkPos.y + 35 }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({ std::min(1100.0f, viewport->WorkSize.x - 24), std::min(760.0f, viewport->WorkSize.y - 55) }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Surface Brushes", &mIsOpen)) {
		if (state.Dragging) {
			PREFABS::Try([&] { state.cancel(); });
		}
		ImGui::End();
		return;
	}
	try {
		auto& manager = PREFABS::Manager();
		if (!manager.hasCurrentScene()) {
			ImGui::TextUnformatted("Open a scene with a surface.");
			ImGui::End();
			return;
		}
		auto& scene = manager.getCurrentScene();
		if (state.Dragging && state.SceneLifetime.lock() != scene.lifetime) {
			state.Dragging = false;
			state.Before.clear();
			state.Working.clear();
		}
		std::vector<std::shared_ptr<ECS::Object>> objects;
		for (auto object : scene.getObjects()) {
			if (object->getComponent<ECS::SurfaceComponent>()) {
				objects.push_back(object);
			}
		}
		if (objects.empty()) {
			ImGui::TextUnformatted("Add a SurfaceComponent to the scene.");
			ImGui::End();
			return;
		}
		auto selected = std::find_if(objects.begin(), objects.end(), [&](const auto& object) { return object->getIDInt() == state.TargetId; });
		if (selected == objects.end()) {
			selected = objects.begin();
			if (EditorRender::GlobalState.mSelectObject && EditorRender::GlobalState.mSelectObject->getComponent<ECS::SurfaceComponent>()) {
				selected = std::find(objects.begin(), objects.end(), EditorRender::GlobalState.mSelectObject);
				if (selected == objects.end()) {
					selected = objects.begin();
				}
			}
			state.TargetId = (*selected)->getIDInt();
			state.Brush.LayerId = (*selected)->getComponent<ECS::SurfaceComponent>()->settings().stableLayerId(0);
		}
		auto object = *selected;
		auto component = object->getComponent<ECS::SurfaceComponent>();
		ImGui::BeginChild("SurfaceTools", { 285, 0 }, true);
		ImGui::BeginDisabled(state.Dragging);
		ImGui::TextUnformatted("Surface");
		ImGui::SetNextItemWidth(-1);
		if (ImGui::BeginCombo("##Surface", object->getName().c_str())) {
			for (auto candidate : objects) {
				ImGui::PushID(candidate->getIDInt());
				if (ImGui::Selectable(candidate->getName().c_str(), candidate == object)) {
					state.TargetId = candidate->getIDInt();
					state.Brush.LayerId = candidate->getComponent<ECS::SurfaceComponent>()->settings().stableLayerId(0);
					EditorRender::GlobalState.mSelectObject = candidate;
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		int operation = int(state.Brush.Operation);
		ImGui::TextUnformatted("Brush");
		ImGui::SetNextItemWidth(-1);
		if (ImGui::Combo("##Brush", &operation, "Raise / lower\0Flatten\0Paint coating\0Foliage density\0")) {
			state.Brush.Operation = SURFACE::BrushOperation(operation);
			state.Brush.Value = operation == 0 ? .25f : 0;
		}
		ImGui::SetNextItemWidth(115);
		ImGui::SliderFloat("Radius", &state.Brush.Radius, .2f, 30, "%.2f");
		ImGui::SetNextItemWidth(115);
		ImGui::SliderFloat("Hardness", &state.Brush.Hardness, 0, 1);
		ImGui::SetNextItemWidth(115);
		ImGui::SliderFloat("Strength", &state.Brush.Strength, .01f, 1);
		if (state.Brush.Operation == SURFACE::BrushOperation::AddHeight) {
			ImGui::SetNextItemWidth(115);
			ImGui::DragFloat("Height per stamp", &state.Brush.Value, .02f, .01f, 10);
		}
		if (state.Brush.Operation == SURFACE::BrushOperation::FlattenHeight) {
			ImGui::Checkbox("Pick initial height", &state.PickFlattenHeight);
			ImGui::SetNextItemWidth(115);
			ImGui::DragFloat("Flatten height", &state.Brush.Value, .1f);
		}
		if (state.Brush.Operation == SURFACE::BrushOperation::FoliageDensity) {
			ImGui::SetNextItemWidth(115);
			ImGui::SliderFloat("Density", &state.Brush.Value, 0, 1);
		}
		const auto& settings = component->settings();
		if (state.Brush.Operation == SURFACE::BrushOperation::PaintLayer) {
			const char* label = "Choose coating";
			for (unsigned index = 0; index < settings.Layers.size(); ++index) {
				if (settings.stableLayerId(index) == state.Brush.LayerId) {
					label = settings.Layers[index].Name.c_str();
				}
			}
			ImGui::TextUnformatted("Coating");
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##Coating", label)) {
				for (unsigned index = 0; index < settings.Layers.size(); ++index) {
					if (ImGui::Selectable(settings.Layers[index].Name.c_str(), settings.stableLayerId(index) == state.Brush.LayerId)) {
						state.Brush.LayerId = settings.stableLayerId(index);
					}
				}
				ImGui::EndCombo();
			}
		}
		int layerId = int(state.EditLayerId);
		ImGui::SetNextItemWidth(115);
		if (ImGui::InputInt("Edit layer ID", &layerId)) {
			state.EditLayerId = unsigned(std::clamp(layerId, 1, 1000000));
		}
		ImGui::Checkbox("Live preview", &state.LivePreview);
		ImGui::SetNextItemWidth(115);
		ImGui::DragFloat("Camera speed", &state.NavigationSpeed, .5f, .1f, 200);
		ImGui::EndDisabled();
		ImGui::BeginDisabled(state.Dragging || manager.isEditingLocked() || PREFABS::Locked(*object));
		const bool undo = ImGui::Button("Undo");
		ImGui::SameLine();
		const bool redo = ImGui::Button("Redo");
		ImGui::SameLine();
		if (ImGui::Button("Save")) {
			PREFABS::Try([&] { manager.saveToFile(); });
		}
		if (undo || redo) {
			PREFABS::Try([&] {
				if (undo) {
					EditorRender::GlobalState.mCommandHistory.undo();
				}
				else {
					EditorRender::GlobalState.mCommandHistory.redo();
				}
			});
			ImGui::EndDisabled();
			ImGui::EndChild();
			ImGui::End();
			return;
		}
		ImGui::SeparatorText("Saved edit layers");
		for (const auto& layer : component->settings().Edits) {
			ImGui::PushID(int(layer.Id));
			bool enabled = layer.Enabled;
			if (ImGui::Checkbox(layer.Name.c_str(), &enabled)) {
				auto before = component->settings().Edits;
				auto after = before;
				for (auto& edit : after) {
					if (edit.Id == layer.Id) {
						edit.Enabled = enabled;
					}
				}
				PREFABS::Try([&] { recordEdits(state.TargetId, before, after, false); });
				ImGui::PopID();
				break;
			}
			ImGui::Text("%u strokes / opacity %.2f", unsigned(layer.Strokes.size()), layer.Opacity);
			ImGui::PopID();
		}
		ImGui::EndDisabled();
		if (component->updatePending()) {
			ImGui::TextWrapped("Updating in background; previous terrain stays visible.");
		}
		bool buildFailed = !component->updateError().empty();
		if (buildFailed) {
			ImGui::TextWrapped("%s", component->updateError().c_str());
		}
		for (auto& foliage : scene.getWorld().getComponentManager()->getComponentsArray<ECS::FoliageComponent>()) {
			if (foliage.surface().get() == component.get() && !foliage.updateError().empty()) {
				ImGui::TextWrapped("Foliage: %s", foliage.updateError().c_str());
				buildFailed = true;
			}
		}
		if (buildFailed && !state.Dragging && !manager.isEditingLocked() && !PREFABS::Locked(*object) && ImGui::Button("Retry update")) {
			PREFABS::Try([&] { component->apply(component->settings()); });
		}
		if (!state.Error.empty()) {
			ImGui::TextWrapped("%s", state.Error.c_str());
		}
		if (!PREFABS::error.empty()) {
			ImGui::TextWrapped("%s", PREFABS::error.c_str());
		}
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("SurfaceViewport", { 0, 0 }, true);
		ImGui::TextWrapped("Left drag: paint. Shift: reverse. Esc: cancel. Right drag + WASD/QE: camera.");
		auto& renderer = RESOURCES::ServiceManager::Get<RENDER::GameRendererInterface>();
		const auto& pipeline = renderer.getCurrentPipeline();
		const auto display = pipeline.mTextures.find("Display");
		auto camera = scene.findMainCamera();
		if (display != pipeline.mTextures.end() && display->second && camera) {
			const auto texture = display->second;
			const auto available = ImGui::GetContentRegionAvail();
			const float scale = std::max(.001f, std::min(available.x / texture->getWidth(), available.y / texture->getHeight()));
			const ImVec2 size { float(texture->getWidth()) * scale, float(texture->getHeight()) * scale };
			const auto origin = ImGui::GetCursorScreenPos();
			ImGui::Image(reinterpret_cast<ImTextureID>(texture->getImguiId()), size, { 0, 1 }, { 1, 0 });
			const bool hovered = ImGui::IsItemHovered();
			auto& io = ImGui::GetIO();
			if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !state.Dragging && !PREFABS::Locked(camera->obj.get())) {
				auto transform = camera->obj->getTransform();
				auto rotation = transform->getLocalRotationDeg();
				rotation.x = std::clamp(rotation.x + io.MouseDelta.y * .2f, -89.0f, 89.0f);
				rotation.y -= io.MouseDelta.x * .2f;
				transform->setLocalRotationDeg(rotation);
				MATH::Vector3f move;
				if (ImGui::IsKeyDown(ImGuiKey_W)) {
					move += transform->getLocalForward();
				}
				if (ImGui::IsKeyDown(ImGuiKey_S)) {
					move -= transform->getLocalForward();
				}
				if (ImGui::IsKeyDown(ImGuiKey_D)) {
					move += transform->getLocalRight();
				}
				if (ImGui::IsKeyDown(ImGuiKey_A)) {
					move -= transform->getLocalRight();
				}
				if (ImGui::IsKeyDown(ImGuiKey_E)) {
					move += transform->getLocalUp();
				}
				if (ImGui::IsKeyDown(ImGuiKey_Q)) {
					move -= transform->getLocalUp();
				}
				transform->translateLocal(move * (state.NavigationSpeed * std::min(io.DeltaTime, .1f)));
			}
			const auto& live = component->terrain().settings();
			const auto& world = component->obj->getTransform()->getWorldMatrix();
			const auto inverseWorld = MATH::Matrix4f::Inverse(world);
			const auto viewProjection = camera->getCamera().getProjectionMatrix() * camera->getCamera().getViewMatrix();
			if (hovered && !manager.isEditingLocked() && !PREFABS::Locked(component->obj.get())) {
				const float x = 2 * (io.MousePos.x - origin.x) / size.x - 1;
				const float y = 1 - 2 * (io.MousePos.y - origin.y) / size.y;
				const auto inverse = MATH::Matrix4f::Inverse(viewProjection);
				const auto nearPoint = unproject(inverse, x, y, -1);
				const auto farPoint = unproject(inverse, x, y, 1);
				const auto direction = MATH::Vector3f::Normalize(farPoint - nearPoint);
				const auto hit = SURFACE::raycastSurface(
					live, transformedPoint(inverseWorld, nearPoint, 1), transformedPoint(inverseWorld, direction, 0), camera->getCamera().getFar());
				if (hit) {
					auto ring = [&](float radius, ImU32 color) {
						std::vector<ImVec2> points;
						for (unsigned index = 0; index <= 64; ++index) {
							const float angle = index * MATH::PI * 2 / 64;
							const float px = hit->Position.x + radius * std::cos(angle);
							const float pz = hit->Position.z + radius * std::sin(angle);
							if (!SURFACE::contains(live, px, pz)) {
								continue;
							}
							const auto p = transformedPoint(world, SURFACE::project(live, px, pz).Position, 1);
							const auto clip = viewProjection * MATH::Vector4f { p.x, p.y, p.z, 1 };
							if (clip.w > .001f) {
								points.push_back(
									{ origin.x + (clip.x / clip.w * .5f + .5f) * size.x, origin.y + (.5f - clip.y / clip.w * .5f) * size.y });
							}
						}
						if (points.size() > 1) {
							ImGui::GetWindowDrawList()->AddPolyline(points.data(), int(points.size()), color, 0, 2);
						}
					};
					ring(state.Brush.Radius, IM_COL32(255, 210, 65, 255));
					ring(state.Brush.Radius * state.Brush.Hardness, IM_COL32(255, 210, 65, 130));
					const bool allowed = state.Brush.Operation != SURFACE::BrushOperation::PaintLayer || settings.Blend.Enabled;
					if (allowed && ImGui::IsMouseDown(ImGuiMouseButton_Left) && (state.Dragging || ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
						if (!state.Dragging) {
							state.stamp(*component, hit->Position, io.KeyShift);
						}
						else {
							const auto delta = hit->Position - state.LastStamp;
							const float distance = MATH::Vector3f::Length(delta);
							const float spacing = std::max(.05f, state.Brush.Radius * .2f);
							if (distance >= spacing) {
								const auto start = state.LastStamp;
								const unsigned count = std::min(64u, unsigned(std::ceil(distance / spacing)));
								for (unsigned stamp = 1; stamp <= count; ++stamp) {
									state.stamp(*component, start + delta * (float(stamp) / count), io.KeyShift);
								}
							}
							else if (ImGui::GetTime() - state.LastStampTime >= .1) {
								state.stamp(*component, hit->Position, io.KeyShift);
							}
						}
						if (state.LivePreview && ImGui::GetTime() - state.LastPreviewTime >= .15) {
							state.preview();
						}
					}
					if (!allowed) {
						ImGui::SetTooltip("Enable smooth coating transitions on this surface before painting.");
					}
				}
			}
		}
		else {
			ImGui::TextUnformatted("The active pipeline needs a Display texture and a scene camera.");
		}
		if (state.Dragging && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			state.cancel();
		}
		else if (state.Dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			state.commit();
		}
		ImGui::EndChild();
	} catch (const std::exception& error) {
		state.Error = error.what();
		if (state.Dragging) {
			try {
				state.cancel();
			} catch (...) { }
		}
		// Child windows are normally ended above; terminate any active child scopes on errors.
		while (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_ChildWindow) {
			ImGui::EndChild();
		}
		ImGui::TextWrapped("%s", state.Error.c_str());
	}
	ImGui::End();
}
}
#endif
