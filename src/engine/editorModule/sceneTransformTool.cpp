#include "sceneTransformTool.h"

#ifdef USE_EDITOR
#include "coreModule/ecs/components/ambientLight.h"
#include "coreModule/ecs/components/ambientSphereLight.h"
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/components/directionalLight.h"
#include "coreModule/ecs/components/particleSystemComponent.h"
#include "coreModule/ecs/components/pointLight.h"
#include "coreModule/ecs/components/spotLight.h"
#include "coreModule/gui/components/spriteComponent.h"
#include "coreModule/gui/components/vectorComponents.h"
#include "editorRender.h"
#include "guiModule/guiGeometry.h"
#include "prefabEditor.h"
#include "sceneModule/scene.h"
#include "scenePicking.h"
#include "windowModule/window/window.h"
#if USE_PHYSICS_2D
#include "coreModule/ecs/components/cloth2dComponent.h"
#include "coreModule/ecs/components/physics2dComponent.h"
#endif
#if USE_PHYSICS_3D
#include "coreModule/ecs/components/destructibleMeshComponent.h"
#include "coreModule/ecs/components/physics3dComponent.h"
#include "coreModule/ecs/components/physicsGrabComponent.h"
#include "coreModule/ecs/components/ragdollComponent.h"
#include "physicsModule/characterMotor.h"
#endif
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#if defined(USE_SDL) && defined(_WIN32)
#include <SDL3/SDL.h>
#endif

namespace IKIGAI::EDITOR {
namespace {
	using Vec3 = MATH::Vector3f;
	using Matrix = MATH::Matrix4f;
	using Quaternion = MATH::QuaternionF;

	SceneLocalTransform read(const ECS::Object& object) {
		const auto transform = object.tryGetTransform();
		return { transform->getLocalPosition(), transform->getLocalRotation(), transform->getLocalScale() };
	}

	bool planarPhysics(const ECS::Object& object) {
#if USE_PHYSICS_2D
		return object.getComponent<ECS::Physics2DComponent>() || object.getComponent<ECS::SoftBody2DComponent>()
			|| object.getComponent<ECS::Cloth2DComponent>() || object.getComponent<ECS::Fluid2DComponent>();
#else
		(void)object;
		return false;
#endif
	}

#if USE_PHYSICS_3D
	bool hasRagdoll(ECS::Object& object) {
		return object.getComponent<ECS::RagdollComponent>()
			|| std::ranges::any_of(object.getChildren(), [](const auto& child) { return child && hasRagdoll(*child); });
	}
#endif

	bool canScale(ECS::Object& object) {
#if USE_PHYSICS_3D
		if (object.getComponent<ECS::Physics3DComponent>() || object.getComponent<ECS::SoftBody3DComponent>()
			|| object.getComponent<ECS::Cloth3DComponent>() || object.getComponent<ECS::Fluid3DComponent>()
			|| object.getComponent<ECS::DestructibleMeshComponent>() || hasRagdoll(object)) {
			return false;
		}
#endif
		return !planarPhysics(object);
	}

	void applyTransform(ECS::Object& object, const SceneLocalTransform& value) {
		auto transform = object.tryGetTransform();
		const bool moved = value.position != transform->getLocalPosition() || value.rotation != transform->getLocalRotation();
		transform->setLocalPosition(value.position);
		transform->setLocalRotation(value.rotation);
		if (value.scale != transform->getLocalScale() && canScale(object)) {
			transform->setLocalScale(value.scale);
		}
#if USE_PHYSICS_3D
		if (moved) {
			if (auto rigid = object.getComponent<ECS::Physics3DComponent>(); rigid && rigid->runtime) {
				if (auto world = rigid->runtime->world.lock()) {
					world->teleport(rigid->runtime->id, value.position, value.rotation);
				}
			}
			if (auto ragdoll = object.getComponent<ECS::RagdollComponent>()) {
				ragdoll->relocate(transform->getWorldPosition(), transform->getWorldRotation());
			}
		}
#else
		(void)moved;
#endif
	}

	class TransformCommand final : public ICommand {
	public:
		TransformCommand(const std::shared_ptr<ECS::Object>& object, std::weak_ptr<void> scene, SceneLocalTransform before)
			: mObject(object)
			, mScene(std::move(scene))
			, mBefore(before)
			, mAfter(read(*object)) { }
		void execute() override {
			applyToObject(mAfter);
		}
		void undo() override {
			applyToObject(mBefore);
		}
		std::string getName() const override {
			return "Scene transform";
		}

	private:
		void applyToObject(const SceneLocalTransform& value) {
			if (auto object = mObject.lock(); object && object->isAlive() && !mScene.expired() && !PREFABS::Locked(*object)) {
				applyTransform(*object, value);
			}
		}
		std::weak_ptr<ECS::Object> mObject {};
		std::weak_ptr<void> mScene {};
		SceneLocalTransform mBefore {};
		SceneLocalTransform mAfter {};
	};

	struct View {
		ImVec2 origin {};
		ImVec2 size {};
		Matrix viewProjection {};
		Matrix inverse {};

		SceneRay ray(ImVec2 point) const {
			const float x = (point.x - origin.x) * 2 / size.x - 1;
			const float y = 1 - (point.y - origin.y) * 2 / size.y;
			return unprojectSceneRay(inverse, { x, y });
		}

		bool project(Vec3 point, ImVec2& screen) const {
			const auto clip = viewProjection * MATH::Vector4f { point.x, point.y, point.z, 1 };
			if (clip.w <= 0 || std::abs(clip.x) > clip.w || std::abs(clip.y) > clip.w || std::abs(clip.z) > clip.w) {
				return false;
			}
			screen = { origin.x + (clip.x / clip.w + 1) * size.x * .5f, origin.y + (1 - clip.y / clip.w) * size.y * .5f };
			return true;
		}
	};

	enum class Marker { Pivot, Light, Emitter, Camera };

	bool screenEmitter(const ECS::Object& object) {
		return object.getComponent<ECS::ParticleSystem2DComponent>() || object.getComponent<ECS::SpriteParticleComponent>();
	}

	Marker markerFor(const ECS::Object& object) {
		if (object.getComponent<ECS::DirectionalLight>() || object.getComponent<ECS::PointLight>() || object.getComponent<ECS::SpotLight>()
			|| object.getComponent<ECS::AmbientLight>() || object.getComponent<ECS::AmbientSphereLight>()) {
			return Marker::Light;
		}
		if (screenEmitter(object) || object.getComponent<ECS::ParticleSystem3DComponent>()) {
			return Marker::Emitter;
		}
		return object.getComponent<ECS::CameraComponent>() ? Marker::Camera : Marker::Pivot;
	}

	void drawMarker(ImDrawList& draw, ImVec2 position, Marker marker, bool selected) {
		const auto color = selected ? IM_COL32(255, 195, 65, 255) : IM_COL32(158, 214, 255, 240);
		const auto at = [position](float x, float y) { return ImVec2 { position.x + x, position.y + y }; };
		if (marker == Marker::Pivot) {
			draw.AddCircleFilled(position, 3.f, color);
			return;
		}
		draw.AddCircleFilled(position, 9.f, IM_COL32(16, 25, 37, 235), 16);
		switch (marker) {
		case Marker::Light:
			draw.AddCircle(position, 3.f, color, 12, 1.4f);
			for (unsigned index = 0; index < 8; ++index) {
				const float angle = index * MATH::PI / 4.f;
				const float x = std::cos(angle);
				const float y = std::sin(angle);
				draw.AddLine(at(x * 4.8f, y * 4.8f), at(x * 7.f, y * 7.f), color, 1.25f);
			}
			break;
		case Marker::Emitter:
			draw.AddCircleFilled(at(-3.5f, 3.f), 2.3f, color);
			draw.AddCircleFilled(at(0.5f, -.5f), 1.9f, color);
			draw.AddCircleFilled(at(4.f, -4.f), 1.45f, color);
			break;
		case Marker::Camera:
			draw.AddRect(at(-6, -3.5f), at(1, 3.5f), color, 1.f);
			draw.AddTriangle(at(1, 0), at(6, -3.5f), at(6, 3.5f), color);
			break;
		case Marker::Pivot:
			break;
		}
	}

	bool screenSpace(const ECS::Object& object) {
		const bool gui = screenEmitter(object) || object.getComponent<ECS::InteractionComponent>() || object.getComponent<ECS::SdfTextComponent>()
			|| object.getComponent<ECS::VectorShapeComponent>() || object.getComponent<ECS::VectorPathComponent>()
			|| object.getComponent<ECS::VectorCanvasComponent>() || object.getComponent<ECS::SpriteComponent>()
			|| object.getComponent<ECS::RootGuiComponent>();
		return gui && !GUI::canvasGeometry(object).worldSpace;
	}

	ScenePick pickGui(SCENE_SYSTEM::Scene& scene, const View& view, MATH::Vector2f extent, ImDrawList* markers, bool clicked, bool editWidgets) {
		const auto mouse = clicked ? ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left] : ImGui::GetMousePos();
		const MATH::Vector2f pixel { (mouse.x - view.origin.x) * extent.x / view.size.x, (mouse.y - view.origin.y) * extent.y / view.size.y };
		ScenePick best {};
		std::pair<int, std::int64_t> top {};
		for (const auto& object : scene.getObjects()) {
			if (!object->isAlive() || !object->getIsActive() || !object->getIsVisible() || !screenSpace(*object)) {
				continue;
			}
			const auto canvas = GUI::canvasGeometry(*object);
			const auto matrix = canvas.correction * object->tryGetTransform()->getWorldMatrix();
			const auto origin = matrix * Vec3 {};
			const auto clip = GUI::clipBounds(*object, extent);
			const auto marker = markerFor(*object);
			const bool showMarker = markers && (editWidgets || marker == Marker::Emitter);
			const ImVec2 screen { view.origin.x + origin.x * view.size.x / extent.x, view.origin.y + origin.y * view.size.y / extent.y };
			if (showMarker && clip.contains({ origin.x, origin.y })) {
				drawMarker(*markers, screen, marker, object == EditorRender::GlobalState.mSelectObject);
			}
			if (!clicked || !clip.contains(pixel)) {
				continue;
			}
			const auto local = GUI::localPoint(matrix, pixel);
			if (!local) {
				continue;
			}
			MATH::Vector2f size {};
			if (auto interaction = object->getComponent<ECS::InteractionComponent>()) {
				size = interaction->hitSize();
			}
			else if (auto shape = object->getComponent<ECS::VectorShapeComponent>()) {
				size = shape->Size;
			}
			const auto order = GUI::drawOrder(*object, canvas);
			const float radius = marker == Marker::Emitter ? 10.f : 5.f;
			const bool overOrigin = showMarker && std::abs(mouse.x - screen.x) <= radius && std::abs(mouse.y - screen.y) <= radius;
			const bool inside = local->x >= 0 && local->y >= 0 && local->x <= size.x && local->y <= size.y;
			if ((overOrigin || inside) && (!best.object || order >= top)) {
				top = order;
				best = { object, view.ray(mouse).origin.z - origin.z };
			}
		}
		return best;
	}

	std::optional<ScenePick> pick(SCENE_SYSTEM::Scene& scene, const ECS::Object& camera, const View& view, ImDrawList* markers, bool clicked,
		bool preferPhysics, bool previewOwnsClick) {
		const auto mouse = clicked ? ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left] : ImGui::GetMousePos();
		const auto ray = view.ray(mouse);
		ScenePick best { {}, ray.length };
		ScenePick marked { {}, ray.length };
		float markerOffset = std::numeric_limits<float>::infinity();
		bool previewStrike = false;
#if USE_PHYSICS_3D
		PHYSICS::CHARACTER::Hit bodyHit {};
		if (clicked && ray.length > 0 && scene.simulation()) {
			bodyHit = PHYSICS::CHARACTER::raycast(scene.physics3d(), ray.origin, ray.direction * ray.length, { .layer = ~0u, .staticsOnly = false });
		}
#else
		(void)preferPhysics;
		(void)previewOwnsClick;
#endif
		const auto& selected = EditorRender::GlobalState.mSelectObject;
		for (const auto& object : scene.getObjects()) {
			if (!object->isAlive() || !object->getIsActive() || !object->getIsVisible() || object.get() == &camera || screenSpace(*object)) {
				continue;
			}
#if USE_PHYSICS_3D
			if (previewOwnsClick && bodyHit) {
				const auto destructible = object->getComponent<ECS::DestructibleMeshComponent>();
				previewStrike |= destructible
					&& std::ranges::find(destructible->pieces(), bodyHit.body, &DESTRUCTION::DestructiblePiece::body) != destructible->pieces().end();
			}
			const auto rigid = object->getComponent<ECS::Physics3DComponent>();
			const auto ragdoll = object->getComponent<ECS::RagdollComponent>();
			const bool simulatedRagdoll = ragdoll && ragdoll->state() == ECS::RagdollState::Simulated;
			const bool physicalPick
				= (preferPhysics && rigid && rigid->simulationId() == bodyHit.body) || (simulatedRagdoll && ragdoll->ownsBody(bodyHit.body));
			if (bodyHit && physicalPick && bodyHit.distance <= best.distance + .001f) {
				best = { object, bodyHit.distance };
			}
			if (simulatedRagdoll) {
				continue;
			}
#endif
			// A zero-length ray classifies marker-only objects without testing triangles.
			const auto intersection = intersectSceneObject(*object, clicked ? ray : SceneRay {});
			if (intersection.hasGeometry) {
				if (clicked && intersection.distance < best.distance - .001f) {
					best = { object, intersection.distance };
				}
				continue;
			}
			if (!markers) {
				continue;
			}
#if USE_PHYSICS_3D
			if (preferPhysics && rigid && rigid->simulationId()) {
				continue;
			}
#endif
			const auto position = object->tryGetTransform()->getWorldPosition();
			ImVec2 screen {};
			if (!view.project(position, screen)) {
				continue;
			}
			const auto marker = markerFor(*object);
			drawMarker(*markers, screen, marker, object == selected);
			const float x = screen.x - mouse.x;
			const float y = screen.y - mouse.y;
			const float offset = x * x + y * y;
			const float radius = marker == Marker::Pivot ? 5.f : 10.f;
			if (offset > radius * radius) {
				continue;
			}
			const float distance = (position - ray.origin).dot(ray.direction);
			if (distance >= 0 && (offset < markerOffset || (offset == markerOffset && distance < marked.distance))) {
				marked = { object, distance };
				markerOffset = offset;
			}
		}
		if (marked.object) {
			markers->AddText({ mouse.x + 12, mouse.y - 7 }, IM_COL32(224, 241, 255, 255), marked.object->getName().c_str());
		}
		if (previewStrike) {
			return std::nullopt;
		}
		return marked.object ? marked : best;
	}

	bool canEdit(const ECS::Object& object) {
		auto parent = object.getParent();
		return !PREFABS::Locked(object) && (!parent || std::abs(Matrix::Determinant(parent->tryGetTransform()->getWorldMatrix())) > 1e-12f);
	}

	Vec3 localPosition(const ECS::Object& object, Vec3 worldPosition) {
		if (screenSpace(object)) {
			worldPosition = Matrix::Inverse(GUI::canvasGeometry(object).correction) * worldPosition;
		}
		if (auto parent = object.getParent()) {
			return Matrix::Inverse(parent->tryGetTransform()->getWorldMatrix()) * worldPosition;
		}
		return worldPosition;
	}

	Matrix gizmoMatrix(ECS::Object& object, ImGuizmo::OPERATION operation) {
		const auto transform = object.tryGetTransform();
		auto matrix = Quaternion::ToMatrix4(transform->getWorldRotation());
		const auto position
			= screenSpace(object) ? GUI::canvasGeometry(object).correction * transform->getWorldPosition() : transform->getWorldPosition();
		const auto scale = transform->getLocalScale();
		for (unsigned axis = 0; axis < 3; ++axis) {
			matrix(axis, 3) = position[axis];
			if (operation == ImGuizmo::SCALE) {
				for (unsigned row = 0; row < 3; ++row) {
					matrix(row, axis) *= std::max(.001f, std::abs(scale[axis]));
				}
			}
		}
		return Matrix::Transpose(matrix);
	}

	void applyGizmo(ECS::Object& object, Matrix& matrix, ImGuizmo::OPERATION operation) {
		auto value = read(object);
		float position[3] {};
		float rotation[3] {};
		float scale[3] {};
		ImGuizmo::DecomposeMatrixToComponents(matrix.getData(), position, rotation, scale);
		switch (operation) {
		case ImGuizmo::TRANSLATE:
			value.position = localPosition(object, { position[0], position[1], position[2] });
			break;
		case ImGuizmo::ROTATE: {
			const auto world = Quaternion(Matrix::Transpose(matrix));
			auto parent = object.getParent();
			value.rotation = Quaternion::Normalize(parent ? Quaternion::Conjugate(parent->tryGetTransform()->getWorldRotation()) * world : world);
			break;
		}
		case ImGuizmo::SCALE:
			for (unsigned axis = 0; axis < 3; ++axis) {
				value.scale[axis] = std::copysign(std::max(.001f, scale[axis]), value.scale[axis]);
			}
			break;
		default:
			return;
		}
		applyTransform(object, value);
	}
}

bool SceneTransformTool::capturesMouse() const {
	return enabled || mGesture == Gesture::Gizmo;
}

bool SceneTransformTool::active() const {
	return mGesture != Gesture::None;
}

void SceneTransformTool::finish(bool cancel) {
	if (auto object = mObject.lock();
		object && object->isAlive() && !mScene.expired() && object->getParent() == mParent.lock() && canEdit(*object) && read(*object) != mBefore) {
		if (cancel) {
			applyTransform(*object, mBefore);
		}
		else {
			EditorRender::GlobalState.mCommandHistory.addCommand(std::make_unique<TransformCommand>(object, mScene, mBefore));
		}
	}
	mObject.reset();
	mParent.reset();
	mScene.reset();
	mGesture = Gesture::None;
}

void SceneTransformTool::draw(SCENE_SYSTEM::Scene& scene, bool previewOwnsClick) {
	auto& io = ImGui::GetIO();
	bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	bool focusLost = io.AppFocusLost;
	auto dragCursor = ImGui::GetMousePos();
#if defined(USE_SDL) && defined(_WIN32)
	if (mGesture == Gesture::Surface) {
		// A secondary ImGui viewport can change ImGui's focus and mouse state mid-drag.
		float screenX = 0;
		float screenY = 0;
		down = (SDL_GetGlobalMouseState(&screenX, &screenY) & SDL_BUTTON_LMASK) != 0;
		focusLost = SDL_GetKeyboardFocus() == nullptr;
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			dragCursor = { screenX, screenY };
		}
		else {
			const auto& window = RESOURCES::ServiceManager::Get<WINDOW::Window>();
			int x = 0;
			int y = 0;
			SDL_GetWindowPosition(window.getSDLWindow(), &x, &y);
			dragCursor = { screenX - x, screenY - y };
		}
	}
#endif
	if (!down) {
		mWaitForRelease = false;
	}
	auto camera = scene.findMainCamera();
#if USE_PHYSICS_3D
	const bool physicsPicking = camera && camera->obj->getComponent<ECS::PhysicsGrabComponent>();
#else
	constexpr bool physicsPicking = false;
#endif
	auto& selected = EditorRender::GlobalState.mSelectObject;
	if (active()) {
		const auto object = mObject.lock();
		if (mScene.lock() != scene.lifetime) {
			mGesture = Gesture::None;
			mObject.reset();
			mWaitForRelease = down;
			ImGuizmo::Enable(false);
		}
		else if ((!enabled && mGesture == Gesture::Surface) || !camera || !object || !object->isAlive() || !canEdit(*object)
			|| object->getParent() != mParent.lock() || focusLost || ImGui::IsKeyPressed(ImGuiKey_Escape, false)
			|| (mGesture == Gesture::Gizmo && object != selected)) {
			finish(true);
			mWaitForRelease = down;
			ImGuizmo::Enable(false);
		}
	}
	if (!camera) {
		return;
	}
	auto* viewport = ImGui::GetMainViewport();
	if (viewport->Size.x <= 0 || viewport->Size.y <= 0) {
		return;
	}
	const auto& cameraView = camera->getCamera().getViewMatrix();
	const auto& projection = camera->getCamera().getProjectionMatrix();
	const auto viewProjection = projection * cameraView;
	if (std::abs(Matrix::Determinant(viewProjection)) < 1e-12f) {
		return;
	}
	const View view { viewport->Pos, viewport->Size, viewProjection, Matrix::Inverse(viewProjection) };
	const auto pixels = RESOURCES::ServiceManager::Get<WINDOW::Window>().getPixelSize();
	if (!pixels.x || !pixels.y) {
		return;
	}
	const MATH::Vector2f pixelExtent { float(pixels.x), float(pixels.y) };
	auto guiCamera = Matrix::Identity;
	guiCamera(2, 3) = -1000.f;
	const auto guiProjection = Matrix::CreateOrthographic(0, pixelExtent.x, pixelExtent.y, 0, .1f, 2000);
	const auto guiViewProjection = guiProjection * guiCamera;
	const View guiView { view.origin, view.size, guiViewProjection, Matrix::Inverse(guiViewProjection) };
	const bool onScene = !ImGui::GetIO().WantCaptureMouse
		&& ImGui::IsMouseHoveringRect(view.origin, { view.origin.x + view.size.x, view.origin.y + view.size.y }, false);
	const bool clicked = onScene && !mWaitForRelease && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

	const bool allowScale = !selected || canScale(*selected);
	if (!allowScale && mOperation == ImGuizmo::SCALE && !active()) {
		mOperation = ImGuizmo::TRANSLATE;
	}
	if (enabled || selected) {
		ImGui::SetNextWindowPos({ viewport->WorkPos.x + 12, viewport->WorkPos.y + 8 }, ImGuiCond_FirstUseEver);
		bool showPanel = true;
		ImGui::Begin("Scene Editing", &showPanel, ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::BeginDisabled(active());
		if (ImGui::RadioButton("Move", mOperation == ImGuizmo::TRANSLATE)) {
			mOperation = ImGuizmo::TRANSLATE;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Rotate", mOperation == ImGuizmo::ROTATE)) {
			mOperation = ImGuizmo::ROTATE;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!allowScale);
		if (ImGui::RadioButton("Scale", mOperation == ImGuizmo::SCALE)) {
			mOperation = ImGuizmo::SCALE;
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !allowScale) {
			ImGui::SetTooltip("Physics controls collider size. Select a visual mesh to scale it.");
		}
		if (mOperation != ImGuizmo::SCALE) {
			if (ImGui::RadioButton("World", mSpace == ImGuizmo::WORLD)) {
				mSpace = ImGuizmo::WORLD;
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Local", mSpace == ImGuizmo::LOCAL)) {
				mSpace = ImGuizmo::LOCAL;
			}
		}
		ImGui::EndDisabled();
		if (selected) {
			ImGui::TextUnformatted(selected->getName().c_str());
		}
		if (physicsPicking && !enabled) {
			ImGui::TextDisabled("Drag a body or use its gizmo.");
		}
		ImGui::End();
		if (!showPanel) {
			finish(true);
			enabled = false;
			selected.reset();
			ImGuizmo::Enable(false);
			return;
		}
	}

	const auto begin = [&](const std::shared_ptr<ECS::Object>& object, Gesture type) {
		mGesture = type;
		mObject = object;
		mParent = object->getParent();
		mScene = scene.lifetime;
		mBefore = read(*object);
	};

	bool overGizmo = false;
	if (selected && selected->isAlive() && selected.get() != &camera->obj.get() && scene.getObjectIndex(*selected) && canEdit(*selected)
		&& mGesture != Gesture::Surface && !mWaitForRelease && (allowScale || mOperation != ImGuizmo::SCALE)) {
		ImGuizmo::BeginFrame();
		ImGuizmo::Enable(true);
		ImGuizmo::SetID(selected->getIDInt());
		const bool gui = screenSpace(*selected);
		ImGuizmo::SetOrthographic(gui || camera->getProjectionMode() == RENDER::Camera::ProjectionMode::ORTHOGRAPHIC);
		ImGuizmo::SetRect(view.origin.x, view.origin.y, view.size.x, view.size.y);
		auto transposedView = Matrix::Transpose(gui ? guiCamera : cameraView);
		auto transposedProjection = Matrix::Transpose(gui ? guiProjection : projection);
		auto matrix = gizmoMatrix(*selected, mOperation);
		const auto space = mOperation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : mSpace;
		const auto operation = mOperation == ImGuizmo::ROTATE && planarPhysics(*selected) ? ImGuizmo::ROTATE_Z : mOperation;
		const bool changed = ImGuizmo::Manipulate(transposedView.getData(), transposedProjection.getData(), operation, space, matrix.getData());
		const bool usingGizmo = ImGuizmo::IsUsing();
		overGizmo = ImGuizmo::IsOver();
		if ((usingGizmo || changed) && !active()) {
			begin(selected, Gesture::Gizmo);
		}
		if (changed && mGesture == Gesture::Gizmo) {
			applyGizmo(*selected, matrix, mOperation);
		}
	}
	if (mGesture == Gesture::Gizmo && !down) {
		finish(false);
	}
	const bool pickPressed = clicked && !active() && !overGizmo;
	auto* markers = showMarkers ? ImGui::GetBackgroundDrawList(viewport) : nullptr;
	auto guiHit = pickGui(scene, guiView, pixelExtent, markers, pickPressed, enabled);
	auto hit = pick(scene, camera->obj.get(), view, markers, pickPressed && !guiHit.object, physicsPicking && !enabled, previewOwnsClick && !enabled);
	if (guiHit.object) {
		// In preview, interactive widgets keep their click. Screen emitters use their editor markers.
		if (!enabled && !screenEmitter(*guiHit.object)) {
			return;
		}
		hit = std::move(guiHit);
	}
	if (pickPressed && hit) {
		selected = hit->object;
		if (enabled && selected && canEdit(*selected)) {
			const bool gui = screenSpace(*selected);
			mSurfacePress = io.MouseClickedPos[ImGuiMouseButton_Left];
			mSurfaceMoved = false;
			const auto ray = (gui ? guiView : view).ray(mSurfacePress);
			begin(selected, Gesture::Surface);
			mAnchor = ray.origin + ray.direction * hit->distance;
			mPlaneNormal = gui ? Vec3 { 0, 0, -1 } : camera->obj->tryGetTransform()->getWorldForward();
			mWorldStart = selected->tryGetTransform()->getWorldPosition();
			if (gui) {
				mWorldStart = GUI::canvasGeometry(*selected).correction * mWorldStart;
			}
		}
	}

	if (mGesture == Gesture::Surface) {
		auto object = mObject.lock();
		const bool validCursor = ImGui::IsMousePosValid(&dragCursor);
		if (validCursor) {
			const float x = dragCursor.x - mSurfacePress.x;
			const float y = dragCursor.y - mSurfacePress.y;
			mSurfaceMoved |= x * x + y * y > io.MouseDragThreshold * io.MouseDragThreshold;
		}
		if (object && mSurfaceMoved && validCursor) {
			const auto ray = (screenSpace(*object) ? guiView : view).ray(dragCursor);
			const float denominator = ray.direction.dot(mPlaneNormal);
			if (std::abs(denominator) > 1e-6f) {
				const auto target = ray.origin + ray.direction * ((mAnchor - ray.origin).dot(mPlaneNormal) / denominator);
				auto value = read(*object);
				value.position = localPosition(*object, mWorldStart + target - mAnchor);
				applyTransform(*object, value);
			}
		}
		if (down) {
			ImGui::SetNextFrameWantCaptureMouse(true);
		}
		else {
			finish(false);
		}
	}
}
}
#endif
