#include "destructionEditor.h"
#if defined(USE_EDITOR) && USE_PHYSICS_3D
#include "coreModule/ecs/components/destructibleMeshComponent.h"
#include "coreModule/ecs/object.h"
#include "imgui.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/imguiHelper/ImGuizmo.h"
#include <algorithm>
#include <misc/cpp/imgui_stdlib.h>

namespace IKIGAI::EDITOR {
namespace {
	using Vec3 = MATH::Vector3f;
	struct Draft {
		ECS::ComponentHandle<ECS::DestructibleMeshComponent> owner {};
		std::weak_ptr<ECS::ComponentManager> components {};
		Vec3 point {};
		Vec3 normal { 1, 0, 0 };
		std::uint64_t piece = 0;
		bool rotate = false;
		bool preview = true;
		float zoom = 1;
		std::string error {};
	};
	Draft& draft(ECS::DestructibleMeshComponent& component) {
		static std::vector<Draft> drafts {};
		auto& world = component.obj->getWorld();
		const auto owner = world.getComponentManager()->getComponentHandle<ECS::DestructibleMeshComponent>(component.obj->getID());
		std::erase_if(drafts, [](const auto& value) {
			const auto manager = value.components.lock();
			return !manager || !manager->resolveComponent(value.owner);
		});
		for (auto& value : drafts) {
			if (value.owner == owner) {
				return value;
			}
		}
		Draft value {};
		value.owner = owner;
		value.components = world.getComponentManagerWeak();
		value.point = component.obj->tryGetTransform()->getWorldPosition();
		drafts.push_back(std::move(value));
		return drafts.back();
	}
	void preview(ECS::DestructibleMeshComponent& component, Draft& state, SCENE_SYSTEM::Scene& scene) {
		const auto origin = ImGui::GetCursorScreenPos();
		const ImVec2 size(std::max(200.f, ImGui::GetContentRegionAvail().x), 240);
		ImGui::InvisibleButton("Destruction preview", size);
		const bool clicked = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
		auto* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(origin, { origin.x + size.x, origin.y + size.y }, true);
		draw->AddRectFilled(origin, { origin.x + size.x, origin.y + size.y }, IM_COL32(18, 22, 28, 255));
		const auto center = component.obj->tryGetTransform()->getWorldPosition();
		float radius = 1;
		for (const auto& piece : component.pieces()) {
			if (auto* body = scene.physics3d().body(piece.body)) {
				for (const auto& vertex : piece.mesh.vertices) {
					radius = std::max(radius, (body->getPointInWorldSpace(vertex.position) - center).magnitude());
				}
			}
		}
		radius *= state.zoom;
		const auto view = MATH::Matrix4f::CreateView(center + Vec3(3, 2, 4) * radius, center, { 0, 1, 0 });
		const auto projection
			= MATH::Matrix4f::CreateOrthographic(-radius * size.x / size.y, radius * size.x / size.y, -radius, radius, .01f, radius * 16);
		const auto worldToClip = projection * view;
		const auto project = [&](Vec3 point) {
			const auto clip = worldToClip * MATH::Vector4f(point.x, point.y, point.z, 1);
			return ImVec2(origin.x + (clip.x / clip.w + 1) * size.x * .5f, origin.y + (1 - clip.y / clip.w) * size.y * .5f);
		};
		const bool validNormal = PHYSICS_COMMON::finite(state.normal) && state.normal.squareMagnitude() > 1e-12f;
		const auto normal = validNormal ? Vec3::Normalize(state.normal) : Vec3(1, 0, 0);
		const auto tangent = Vec3::Normalize(normal.cross(std::abs(normal.y) < .9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0)));
		const auto bitangent = normal.cross(tangent);
		const auto mouse = ImGui::GetMousePos();
		const auto inverse = MATH::Matrix4f::Inverse(worldToClip);
		const auto unproject = [&](float z) {
			auto value = inverse * MATH::Vector4f((mouse.x - origin.x) * 2 / size.x - 1, 1 - (mouse.y - origin.y) * 2 / size.y, z, 1);
			return Vec3(value.x, value.y, value.z) / value.w;
		};
		const auto rayStart = unproject(-1);
		const auto rayDirection = Vec3::Normalize(unproject(1) - rayStart);
		float nearest = std::numeric_limits<float>::max();
		std::uint64_t selected = 0;
		for (const auto& piece : component.pieces()) {
			auto* body = scene.physics3d().body(piece.body);
			if (!body) {
				continue;
			}
			const auto color = piece.id == state.piece ? IM_COL32(255, 185, 65, 255) : IM_COL32(125, 165, 195, 255);
			// Draw triangle-local section segments: a concave section can have several loops and holes.
			for (size_t triangle = 0; triangle < piece.mesh.indices.size(); triangle += 3) {
				std::array<Vec3, 3> vertices {};
				for (unsigned corner = 0; corner < 3; ++corner) {
					vertices[corner] = body->getPointInWorldSpace(piece.mesh.vertices[piece.mesh.indices[triangle + corner]].position);
				}
				std::array<Vec3, 3> intersections {};
				unsigned count = 0;
				for (unsigned corner = 0; corner < 3; ++corner) {
					const auto a = vertices[corner];
					const auto b = vertices[(corner + 1) % 3];
					draw->AddLine(project(a), project(b), color);
					const auto da = normal.dot(a - state.point);
					const auto db = normal.dot(b - state.point);
					if (validNormal && ((da <= 0 && db > 0) || (db <= 0 && da > 0))) {
						intersections[count++] = a + (b - a) * (da / (da - db));
					}
				}
				if (count == 2 && (!state.piece || state.piece == piece.id)) {
					draw->AddLine(project(intersections[0]), project(intersections[1]), IM_COL32(255, 90, 70, 255), 2);
				}
				if (clicked) {
					const auto edgeA = vertices[1] - vertices[0];
					const auto edgeB = vertices[2] - vertices[0];
					const auto cross = rayDirection.cross(edgeB);
					const auto determinant = edgeA.dot(cross);
					if (std::abs(determinant) <= 1e-8f) {
						continue;
					}
					const auto offset = rayStart - vertices[0];
					const auto u = offset.dot(cross) / determinant;
					const auto q = offset.cross(edgeA);
					const auto v = rayDirection.dot(q) / determinant;
					const auto distance = edgeB.dot(q) / determinant;
					if (u >= 0 && v >= 0 && u + v <= 1 && distance >= 0 && distance < nearest) {
						nearest = distance;
						selected = piece.id;
					}
				}
			}
			draw->AddCircleFilled(project(body->getPosition()), 3, color);
		}
		if (validNormal) {
			draw->AddLine(project(state.point), project(state.point + normal * radius * .5f), IM_COL32(255, 90, 70, 255), 2);
			auto matrix = MATH::Matrix4f::Identity;
			for (int axis = 0; axis < 3; ++axis) {
				matrix(axis, 0) = tangent[axis];
				matrix(axis, 1) = bitangent[axis];
				matrix(axis, 2) = normal[axis];
				matrix(axis, 3) = state.point[axis];
			}
			auto gizmoMatrix = MATH::Matrix4f::Transpose(matrix);
			auto gizmoView = MATH::Matrix4f::Transpose(view);
			auto gizmoProjection = MATH::Matrix4f::Transpose(projection);
			ImGuizmo::SetID(component.obj->getIDInt());
			ImGuizmo::SetDrawlist(draw);
			ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
			ImGuizmo::SetOrthographic(true);
			if (ImGuizmo::Manipulate(gizmoView.getData(), gizmoProjection.getData(), state.rotate ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE,
					ImGuizmo::LOCAL, gizmoMatrix.getData())) {
				matrix = MATH::Matrix4f::Transpose(gizmoMatrix);
				state.point = { matrix(0, 3), matrix(1, 3), matrix(2, 3) };
				state.normal = Vec3::Normalize({ matrix(0, 2), matrix(1, 2), matrix(2, 2) });
			}
			if (clicked && !ImGuizmo::IsOver() && selected) {
				state.piece = selected;
			}
		}
		draw->PopClipRect();
	}
}
void DrawDestructionEditor(ECS::DestructibleMeshComponent& component) {
	auto& state = draft(component);
	auto& scene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
	ImGui::InputText("Definition", &component.settings.Definition);
	ImGui::Checkbox("Impact damage", &component.settings.ImpactDamage);
	ImGui::Text("Pieces: %zu; pending: %zu; %s", component.pieces().size(), component.pendingRequests(),
		DESTRUCTION::sliceStatusName(component.lastStatus()));
	if (!component.lastError().empty()) {
		ImGui::TextWrapped("%s", component.lastError().c_str());
	}
	ImGui::DragFloat3("Plane point (world)", &state.point.x, .02f);
	ImGui::DragFloat3("Plane normal (world)", &state.normal.x, .02f);
	if (ImGui::Button("Point at root")) {
		state.point = component.obj->tryGetTransform()->getWorldPosition();
	}
	if (ImGui::BeginCombo("Piece", state.piece ? std::to_string(state.piece).c_str() : "Nearest intersected")) {
		if (ImGui::Selectable("Nearest intersected", !state.piece)) {
			state.piece = 0;
		}
		for (const auto& piece : component.pieces()) {
			if (ImGui::Selectable(std::to_string(piece.id).c_str(), state.piece == piece.id)) {
				state.piece = piece.id;
				if (auto* body = scene.physics3d().body(piece.body)) {
					state.point = body->getPosition();
				}
			}
		}
		ImGui::EndCombo();
	}
	ImGui::Checkbox("Collider / cut preview", &state.preview);
	if (state.preview && !component.pieces().empty()) {
		ImGui::Checkbox("Rotate plane gizmo", &state.rotate);
		ImGui::SliderFloat("Preview zoom", &state.zoom, .5f, 3);
		preview(component, state, scene);
		ImGui::TextDisabled("Click a collider to select its piece; dots mark centers of mass.");
	}
	try {
		if (ImGui::Button("Slice plane")) {
			component.slicePiece(state.piece, state.point, state.normal);
			state.error.clear();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset / retry")) {
			component.reset();
			state.piece = 0;
			state.error.clear();
		}
	} catch (const std::exception& error) {
		state.error = error.what();
	}
	if (!state.error.empty()) {
		ImGui::TextWrapped("%s", state.error.c_str());
	}
	if (ImGui::TreeNode("Request results")) {
		for (const auto& result : component.completedRequests()) {
			ImGui::Text("%llu: %s, %.2f ms (slice %.2f, cook %.2f)", static_cast<unsigned long long>(result.id),
				DESTRUCTION::sliceStatusName(result.status), result.latencyMilliseconds, result.slicingMilliseconds, result.cookingMilliseconds);
			if (!result.error.empty()) {
				ImGui::TextWrapped("%s", result.error.c_str());
			}
		}
		ImGui::TreePop();
	}
	if (scene.destructionRuntime && ImGui::TreeNode("Scene destruction budgets")) {
		const auto& stats = scene.destructionRuntime->statistics;
		ImGui::Text("Jobs %u; CPU %.2f MiB; GPU %.2f MiB", stats.activeJobs, (stats.workingBytes + stats.topologyBytes) / 1048576.0,
			stats.gpuBytes / 1048576.0);
		ImGui::Text("GPU pending upload: %.2f MiB reserved", stats.gpuReservedBytes / 1048576.0);
		ImGui::Text("Frame: %.3f ms; upload %.3f ms / %zu bytes; commit %.3f ms", stats.mainThreadMilliseconds, stats.uploadMilliseconds,
			stats.uploadedBytes, stats.commitMilliseconds);
		ImGui::Text("Submitted %llu; committed %llu; rejected %llu", static_cast<unsigned long long>(stats.submitted),
			static_cast<unsigned long long>(stats.committed), static_cast<unsigned long long>(stats.rejected));
		ImGui::TreePop();
	}
}
}
#endif
