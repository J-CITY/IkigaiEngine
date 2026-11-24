#include "sceneTransformTool.h"

#ifdef USE_EDITOR
#include "coreModule/ecs/components/cameraComponent.h"
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
			if (clip.w <= 0 || clip.z < -clip.w || clip.z > clip.w) {
				return false;
			}
			screen = { origin.x + (clip.x / clip.w + 1) * size.x * .5f, origin.y + (1 - clip.y / clip.w) * size.y * .5f };
			return true;
		}
	};

	bool screenSpace(const ECS::Object& object) {
		const bool gui = object.getComponent<ECS::InteractionComponent>() || object.getComponent<ECS::SdfTextComponent>()
			|| object.getComponent<ECS::VectorShapeComponent>() || object.getComponent<ECS::VectorPathComponent>()
			|| object.getComponent<ECS::VectorCanvasComponent>() || object.getComponent<ECS::SpriteComponent>()
			|| object.getComponent<ECS::RootGuiComponent>();
		return gui && !GUI::canvasGeometry(object).worldSpace;
	}

	ScenePick pickGui(SCENE_SYSTEM::Scene& scene, const View& view, MATH::Vector2f extent, ImDrawList& markers, bool clicked) {
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
			if (clip.contains({ origin.x, origin.y })) {
				const ImVec2 screen { view.origin.x + origin.x * view.size.x / extent.x, view.origin.y + origin.y * view.size.y / extent.y };
				markers.AddCircleFilled(screen, 3, IM_COL32(158, 214, 255, 220));
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
			const bool overOrigin = std::abs(pixel.x - origin.x) <= 6 && std::abs(pixel.y - origin.y) <= 6;
			const bool inside = local->x >= 0 && local->y >= 0 && local->x <= size.x && local->y <= size.y;
			if ((overOrigin || inside) && (!best.object || order >= top)) {
				top = order;
				best = { object, view.ray(mouse).origin.z - origin.z };
			}
		}
		return best;
	}

#if USE_PHYSICS_3D
	std::shared_ptr<ECS::Object> pickPhysicsObject(SCENE_SYSTEM::Scene& scene, const SceneRay& ray) {
		if (ray.length <= 0 || !scene.simulation()) {
			return {};
		}
		const auto hit
			= PHYSICS::CHARACTER::raycast(scene.physics3d(), ray.origin, ray.direction * ray.length, { .layer = ~0u, .staticsOnly = false });
		if (!hit) {
			return {};
		}
		for (const auto& object : scene.getObjects()) {
			if (!object->isAlive() || !object->getIsActive()) {
				continue;
			}
			if (auto rigid = object->getComponent<ECS::Physics3DComponent>(); rigid && rigid->simulationId() == hit.body) {
				return object;
			}
			if (auto ragdoll = object->getComponent<ECS::RagdollComponent>(); ragdoll && ragdoll->ownsBody(hit.body)) {
				return object;
			}
		}
		return {};
	}
#endif

	ScenePick pick(SCENE_SYSTEM::Scene& scene, const ECS::Object& camera, const View& view, ImDrawList& markers, bool clicked) {
		const auto mouse = clicked ? ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left] : ImGui::GetMousePos();
		const auto ray = view.ray(mouse);
		ScenePick best { {}, ray.length };
#if USE_PHYSICS_3D
		PHYSICS::CHARACTER::Hit bodyHit {};
		if (clicked && ray.length > 0 && scene.simulation()) {
			bodyHit = PHYSICS::CHARACTER::raycast(scene.physics3d(), ray.origin, ray.direction * ray.length, { .layer = ~0u, .staticsOnly = false });
		}
#endif
		const auto& selected = EditorRender::GlobalState.mSelectObject;
		for (const auto& object : scene.getObjects()) {
			if (!object->isAlive() || !object->getIsActive() || !object->getIsVisible() || object.get() == &camera || screenSpace(*object)) {
				continue;
			}
#if USE_PHYSICS_3D
			if (auto ragdoll = object->getComponent<ECS::RagdollComponent>(); ragdoll && ragdoll->state() == ECS::RagdollState::Simulated) {
				if (bodyHit && bodyHit.distance < best.distance && ragdoll->ownsBody(bodyHit.body)) {
					best = { object, bodyHit.distance };
				}
				continue;
			}
#endif
			// Geometry tests are only needed for a press. A zero-length ray classifies marker-only objects.
			const auto intersection = intersectSceneObject(*object, clicked ? ray : SceneRay {});
			if (intersection.hasGeometry) {
				if (clicked && intersection.distance < best.distance) {
					best = { object, intersection.distance };
				}
				continue;
			}
			const auto position = object->tryGetTransform()->getWorldPosition();
			ImVec2 screen {};
			if (!view.project(position, screen)) {
				continue;
			}
			const ImU32 color = object == selected ? IM_COL32(255, 195, 65, 255) : IM_COL32(158, 214, 255, 220);
			markers.AddCircleFilled(screen, 4, color);
			const float x = screen.x - mouse.x;
			const float y = screen.y - mouse.y;
			const float distance = (position - ray.origin).dot(ray.direction);
			if (clicked && x * x + y * y <= 64 && distance >= 0 && distance < best.distance) {
				best = { object, distance };
			}
			if (x * x + y * y <= 64) {
				markers.AddText({ screen.x + 8, screen.y - 7 }, color, object->getName().c_str());
			}
		}
		return best;
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

void SceneTransformTool::draw(SCENE_SYSTEM::Scene& scene) {
	const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	if (!down) {
		mWaitForRelease = false;
	}
	auto camera = scene.findMainCamera();
#if USE_PHYSICS_3D
	const bool physicsPicking = camera && camera->obj->getComponent<ECS::PhysicsGrabComponent>();
#else
	constexpr bool physicsPicking = false;
#endif
	const bool transformsAvailable = enabled || physicsPicking;
	auto& selected = EditorRender::GlobalState.mSelectObject;
	if (active()) {
		const auto object = mObject.lock();
		if (mScene.lock() != scene.lifetime) {
			mGesture = Gesture::None;
			mObject.reset();
			mWaitForRelease = down;
			ImGuizmo::Enable(false);
		}
		else if (!transformsAvailable || (!enabled && mGesture == Gesture::Surface) || !camera || object != selected || !object || !object->isAlive()
			|| !canEdit(*object) || object->getParent() != mParent.lock() || ImGui::GetIO().AppFocusLost
			|| ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			finish(true);
			mWaitForRelease = down;
			ImGuizmo::Enable(false);
		}
	}
	if (!transformsAvailable || !camera) {
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
	if (active() && !down) {
		finish(false);
	}
	const bool pickPressed = clicked && !active() && !overGizmo;
	if (!enabled) {
#if USE_PHYSICS_3D
		if (pickPressed && physicsPicking) {
			selected = pickPhysicsObject(scene, view.ray(ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left]));
		}
#endif
		return;
	}
	auto& markers = *ImGui::GetBackgroundDrawList(viewport);
	auto guiHit = pickGui(scene, guiView, pixelExtent, markers, pickPressed);
	auto hit = pick(scene, camera->obj.get(), view, markers, pickPressed && !guiHit.object);
	if (guiHit.object) {
		hit = std::move(guiHit);
	}
	if (pickPressed) {
		selected = hit.object;
		if (selected && canEdit(*selected)) {
			const bool gui = screenSpace(*selected);
			const auto ray = (gui ? guiView : view).ray(ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left]);
			begin(selected, Gesture::Surface);
			mAnchor = ray.origin + ray.direction * hit.distance;
			mPlaneNormal = gui ? Vec3 { 0, 0, -1 } : camera->obj->tryGetTransform()->getWorldForward();
			mWorldStart = selected->tryGetTransform()->getWorldPosition();
			if (gui) {
				mWorldStart = GUI::canvasGeometry(*selected).correction * mWorldStart;
			}
		}
	}
	if (mGesture == Gesture::Surface && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		const auto ray = (screenSpace(*selected) ? guiView : view).ray(ImGui::GetMousePos());
		const float denominator = ray.direction.dot(mPlaneNormal);
		if (std::abs(denominator) > 1e-6f) {
			const auto target = ray.origin + ray.direction * ((mAnchor - ray.origin).dot(mPlaneNormal) / denominator);
			auto value = read(*selected);
			value.position = localPosition(*selected, mWorldStart + target - mAnchor);
			applyTransform(*selected, value);
		}
	}
}
}
#endif
