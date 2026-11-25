#include "physicsDebugWindow.h"

#ifdef USE_EDITOR
#include "sceneModule/scene.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>

#if USE_PHYSICS_2D
#include "physics2dModule/physics2d.h"
#endif
#if USE_PHYSICS_3D
#include "physicsModule/physicWorld.h"
#endif

namespace IKIGAI::EDITOR {
namespace {
#if USE_PHYSICS_3D
	void drawPhysics3D(SCENE_SYSTEM::PhysicsDebugState<PHYSICS::PhysicWorld> state) {
		using namespace PHYSICS;
		auto& [world, paused, singleStep] = state;
		if (world.bodies().empty() && world.softBodies().empty() && world.fluids().empty() && world.forces.fields().empty()) {
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(480, 400), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("3D Physics")) {
			ImGui::End();
			return;
		}
		ImGui::Checkbox("Paused", &paused);
		ImGui::SameLine();
		if (ImGui::Button("Step")) {
			singleStep = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset")) {
			world.clear();
		}
		ImGui::Text("Bodies: %zu  Soft/cloth: %zu  Fluids: %zu  Fields: %zu", world.bodies().size(), world.softBodies().size(), world.fluids().size(),
			world.forces.fields().size());
		// An orthographic preview makes the simulation inspectable with any renderer.
		static float scale = 80;
		ImGui::SliderFloat("Preview scale", &scale, 10, 250);
		auto size = ImGui::GetContentRegionAvail();
		size.x = std::max(size.x, 100.0f);
		size.y = std::max(size.y, 200.0f);
		auto origin = ImGui::GetCursorScreenPos();
		auto* draw = ImGui::GetWindowDrawList();
		ImGui::InvisibleButton("physics3d_canvas", size);
		draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
		auto project = [&](Vec3 p) {
			return ImVec2(origin.x + size.x * 0.3f + (p.x + p.z * 0.35f) * scale, origin.y + size.y * 0.8f - (p.y + p.z * 0.2f) * scale);
		};
		world.debugDraw({ [&](auto a, auto b, std::uint32_t) { draw->AddLine(project(a), project(b), IM_COL32(210, 150, 240, 255)); },
			[&](auto p, float r, std::uint32_t) { draw->AddCircle(project(p), std::max(1.0f, r * scale), IM_COL32(80, 170, 240, 255), 12); }, {} });
		draw->PopClipRect();
		ImGui::End();
	}
#endif

#if USE_PHYSICS_2D
	void drawPhysics2D(SCENE_SYSTEM::PhysicsDebugState<PHYSICS2D::World> state, bool clothPreview) {
		using namespace PHYSICS2D;
		auto& [world, paused, singleStep] = state;
		if (world.bodies().empty() && world.softBodies().empty() && world.fluids().empty()) {
			return;
		}
		if (clothPreview) {
			const auto* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos({ viewport->Pos.x + 12, viewport->Pos.y + 119 }, ImGuiCond_Appearing);
			ImGui::SetNextWindowSize({ std::min(780.f, viewport->Size.x - 24.f), std::max(325.f, viewport->Size.y - 134.f) }, ImGuiCond_Appearing);
		}
		else {
			ImGui::SetNextWindowSize(ImVec2(850, 620), ImGuiCond_FirstUseEver);
		}
		if (!ImGui::Begin(clothPreview ? "Cloth simulation" : "2D Physics")) {
			ImGui::End();
			return;
		}
		ImGui::Checkbox("Pause", &paused);
		ImGui::SameLine();
		if (ImGui::Button("Step")) {
			paused = true;
			singleStep = true;
		}
		ImGui::SameLine();
		ImGui::Checkbox("Sleeping", &world.settings.sleeping);
		ImGui::Text("Bodies: %zu  Joints: %zu  Soft: %zu  Fluids: %zu", world.bodies().size(), world.joints().size(), world.softBodies().size(),
			world.fluids().size());
		static float physicsZoom = 55;
		static float clothZoom = 82;
		static ImVec2 physicsPan(0, 3);
		static ImVec2 clothPan(0, 2.25f);
		auto& zoom = clothPreview ? clothZoom : physicsZoom;
		auto& pan = clothPreview ? clothPan : physicsPan;
		static bool showGrid = true;
		ImGui::SliderFloat("Pixels/unit", &zoom, 10, 160);
		ImGui::Checkbox("Spatial grid", &showGrid);
		ImGui::TextUnformatted("Drag with right mouse to pan. Red lines are contact normals.");
		ImVec2 size = ImGui::GetContentRegionAvail();
		size.x = std::max(size.x, 100.0f);
		size.y = std::max(size.y, 200.0f);
		ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("physics_canvas", size, ImGuiButtonFlags_MouseButtonRight);
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
			auto delta = ImGui::GetIO().MouseDelta;
			pan.x -= delta.x / zoom;
			pan.y += delta.y / zoom;
		}
		auto* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
		draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(22, 27, 36, 255));
		auto screen = [&](Vec2 p) { return ImVec2(origin.x + size.x / 2 + (p.x - pan.x) * zoom, origin.y + size.y / 2 - (p.y - pan.y) * zoom); };
		if (showGrid) {
			float cell = world.settings.cellSize;
			float left = pan.x - size.x / (2 * zoom), right = pan.x + size.x / (2 * zoom), bottom = pan.y - size.y / (2 * zoom),
				  top = pan.y + size.y / (2 * zoom);
			for (float x = std::floor(left / cell) * cell; x <= right; x += cell) {
				draw->AddLine(screen({ x, bottom }), screen({ x, top }), IM_COL32(42, 49, 62, 255));
			}
			for (float y = std::floor(bottom / cell) * cell; y <= top; y += cell) {
				draw->AddLine(screen({ left, y }), screen({ right, y }), IM_COL32(42, 49, 62, 255));
			}
		}
		auto colour = [](std::uint32_t c) { return IM_COL32(c >> 24, (c >> 16) & 255, (c >> 8) & 255, c & 255); };
		world.debugDraw({ [&](Vec2 a, Vec2 b, std::uint32_t c) { draw->AddLine(screen(a), screen(b), colour(c), 1.5f); },
			[&](Vec2 p, float radius, std::uint32_t c) { draw->AddCircle(screen(p), radius * zoom, colour(c), 20, 1.5f); } });
		draw->PopClipRect();
		ImGui::End();
	}
#endif

}

void DrawPhysicsDebug(SCENE_SYSTEM::Scene& scene, [[maybe_unused]] bool clothPreview) {
	auto* simulation = scene.simulation();
	if (!simulation) {
		return;
	}
#if USE_PHYSICS_3D
	if (auto state = simulation->physics3dDebug()) {
		drawPhysics3D(*state);
	}
#endif
#if USE_PHYSICS_2D
	if (auto state = simulation->physics2dDebug()) {
		drawPhysics2D(*state, clothPreview);
	}
#endif
}
}
#endif
