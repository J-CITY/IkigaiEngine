#include "guizmoWindow.h"
#ifdef USE_EDITOR
#include "editorRender.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "coreModule/ecs/object.h"
#include "mathModule/math.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/pathGetter.h"
#include "utilsModule/imguiHelper/imguiWidgets.h"
#include "windowModule/window/window.h"

using namespace IKIGAI::EDITOR;

struct MovableChildData {
	ImVec2 m_pressPosition = ImVec2(0, 0);
	ImVec2 m_position = ImVec2(0, 0);
	bool   m_isDragging = false;
	bool   m_initialPositionSet = false;
};

std::map<std::string, MovableChildData> m_movableChildData;
float camDistance = 8.f;

void PushIconFontSmall() {
	//ImGui::PushFont(GUILayer::Get()->GetIconFontSmall());
}



void IconSmall(const char* icon) {
	//PushIconFontSmall();
	ImGui::Text(icon);
	//ImGui::PopFont();
}

void BeginMovableChild(const char* childID, ImVec2 size, const ImVec2& defaultPosition, const ImRect& confineRect, bool isHorizontal, ImVec2 iconCursorOffset) {
	const ImVec2 confineSize = ImVec2(confineRect.Max.x - confineRect.Min.x, confineRect.Max.y - confineRect.Min.y);
	const float  iconOffset = 12.0f;
	if (isHorizontal)
		size.x += iconOffset;
	else
		size.y += iconOffset;

	// Set the position only if first launch.
	const std::string childIDStr = std::string(childID);
	ImVec2            targetPosition = ImVec2(confineRect.Min.x + m_movableChildData[childIDStr].m_position.x, confineRect.Min.y + m_movableChildData[childIDStr].m_position.y);

	if (targetPosition.x > confineRect.Max.x - size.x)
		targetPosition.x = confineRect.Max.x - size.x;
	if (targetPosition.y > confineRect.Max.y - size.y)
		targetPosition.y = confineRect.Max.y - size.y;

	ImGui::SetNextWindowPos(targetPosition);

	ImGui::BeginChild(childID, size, true, ImGuiWindowFlags_NoMove);
	ImGui::SetCursorPos(iconCursorOffset);

	IconSmall(isHorizontal ? ICON_FA_ELLIPSIS_V : ICON_FA_ELLIPSIS_H);

	if (ImGui::IsItemClicked()) {
		if (!m_movableChildData[childIDStr].m_isDragging) {
			m_movableChildData[childIDStr].m_isDragging = true;
			m_movableChildData[childIDStr].m_pressPosition = m_movableChildData[childIDStr].m_position;
		}
	}
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		m_movableChildData[childIDStr].m_isDragging = false;
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && m_movableChildData[childIDStr].m_isDragging) {

		// Calc new window position.
		const ImVec2 pressPos = m_movableChildData[childIDStr].m_pressPosition;
		const ImVec2 drag = ImGui::GetMouseDragDelta();
		ImVec2       desiredPosition = ImVec2(pressPos.x + drag.x, pressPos.y + drag.y);

		// Confine window position to confine rect.
		const float positionLimitPadding = 2.0f;
		if (desiredPosition.x < positionLimitPadding)
			desiredPosition.x = positionLimitPadding;
		else if (desiredPosition.x > confineSize.x - positionLimitPadding - size.x)
			desiredPosition.x = confineSize.x - positionLimitPadding - size.x;
		if (desiredPosition.y < positionLimitPadding)
			desiredPosition.y = positionLimitPadding;
		else if (desiredPosition.y > confineSize.y - positionLimitPadding - size.y)
			desiredPosition.y = confineSize.y - positionLimitPadding - size.y;

		m_movableChildData[childIDStr].m_position = desiredPosition;
	} else {
		if (!m_movableChildData[childIDStr].m_initialPositionSet) {
			m_movableChildData[childIDStr].m_initialPositionSet = true;
			m_movableChildData[childIDStr].m_position = defaultPosition;
		}
	}
}


void GuizmoWindow::draw() {

	ImGui::Begin("Scene Watcher", nullptr, mGizmoWindowFlags);

	//auto& renderer = RESOURCES::ServiceManager::Get<RENDER::GameRendererInterface>();
	//auto _renderer = reinterpret_cast<RENDER::GameRendererGl*>(&renderer);

	//botton panel

	ImGui::Columns(2, nullptr, false);
	ImGui::SetColumnWidth(0, 40.0f);
	ImGui::Button(ICON_FA_ARROWS_ALT, ImVec2(36, 36));
	ImGui::Button(ICON_FA_SYNC_ALT, ImVec2(36, 36));
	ImGui::Button(ICON_FA_COMPRESS_ALT, ImVec2(36, 36));
	ImGui::NextColumn();

	float w = 0.0f;
	float h = 0.0f;
	//TODO: set texture
	auto val = 0;// _renderer->pingPongTex[!_renderer->pingPong];
	if (val) {
		auto winSize = RESOURCES::ServiceManager::Get<WINDOW::Window>().getSize();
		ImVec2 imWinSize = ImGui::GetWindowSize();
		if (imWinSize.x < imWinSize.y) {
			w = imWinSize.x;
			h = (winSize.y * imWinSize.x) / winSize.x;
		} else {
			w = (imWinSize.y * winSize.x) / winSize.y;
			h = imWinSize.y;
		}

		if (h > 100.0f) {
			h -= 30.0f;
		}
		//ImGui::Image(reinterpret_cast<ImTextureID>((uintptr_t)val->id), ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
		if (ImGui::IsItemHovered()) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				ImVec2 screen_pos = ImGui::GetMousePos();

				ImGuiWindow* window = ImGui::GetCurrentWindowRead();
				screen_pos.x = screen_pos.x - window->Pos.x;
				screen_pos.y = screen_pos.y - window->Pos.y;


				auto startXY = ImGui::GetCursorPos();
				//startXY.x -= w;
				startXY.y -= h;

				auto x = (screen_pos.x - startXY.x);
				auto y = (screen_pos.y - startXY.y);

				auto screenRes = RESOURCES::ServiceManager::Get<WINDOW::Window>().getSize();
				auto camera = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene().findMainCamera();
				auto scale = w / screenRes.x;

				x *= scale;
				y *= scale;

				MATH::Vector4f lRayStart_NDC(
					(x / screenRes.x - 0.5f) * 2.0f, // [0,1024] -> [-1,1]
					(y / screenRes.y - 0.5f) * 2.0f, // [0, 768] -> [-1,1]
					-1.0, // The near plane maps to Z=-1 in Normalized Device Coordinates
					1.0f
				);
				MATH::Vector4f lRayEnd_NDC(
					(x / screenRes.x - 0.5f) * 2.0f,
					(y / screenRes.y - 0.5f) * 2.0f,
					0.0,
					1.0f
				);

				auto InverseProjectionMatrix = MATH::Matrix4f::Inverse(camera->getCamera().getProjectionMatrix());

				auto InverseViewMatrix = MATH::Matrix4f::Inverse(camera->getCamera().getViewMatrix());


				MATH::Vector4f lRayStart_camera = InverseProjectionMatrix * lRayStart_NDC;
				lRayStart_camera = lRayStart_camera / lRayStart_camera.w;

				MATH::Vector4f lRayStart_world = InverseViewMatrix * lRayStart_camera;
				lRayStart_world = lRayStart_world / lRayStart_world.w;

				MATH::Vector4f lRayEnd_camera = InverseProjectionMatrix * lRayEnd_NDC;
				lRayEnd_camera = lRayEnd_camera / lRayEnd_camera.w;
				MATH::Vector4f lRayEnd_world = InverseViewMatrix * lRayEnd_camera;
				lRayEnd_world = lRayEnd_world / lRayEnd_world.w;

				auto v = lRayEnd_world - lRayStart_world;
				MATH::Vector3f lRayDir_world(v.x, v.y, v.z);
				lRayDir_world = (lRayDir_world);
				//lRayDir_world = MATHGL::Vector3::Normalize(lRayDir_world);

				auto origin = camera->obj->mTransform->getWorldPosition();
				//MATHGL::Vector3 out_end = origin + lRayDir_world * 1000.0f;

				std::vector<UTILS::Ref<ECS::Object>> objs;

				for (const auto& modelRenderer : RESOURCES::ServiceManager::Get<ECS2::World>().getComponentManager()->getComponentsArray<ECS::ModelRenderer>()) {
					//for (const auto& modelRenderer : ECS::ComponentManager::GetInstance().getComponentArrayRef<ECS::ModelRenderer>()) {
					if (modelRenderer.obj.get().getIsActive()) {
						auto bs = modelRenderer.getModel()->getBoundingSphere();
						std::vector<MATH::Vector3f> hits;
						auto _pos = bs.position + modelRenderer.obj.get().mTransform->getWorldPosition();
						auto scale = modelRenderer.obj.get().mTransform->getWorldScale();
						auto _r = bs.radius * std::max(scale.x, std::max(scale.y, scale.z));
						if (IMGUI::RaySphereIntersection(origin, lRayDir_world, _pos, _r, hits)) {
							objs.push_back(modelRenderer.obj);
						}
					}
				}

				std::sort(objs.begin(), objs.end(), [origin](UTILS::Ref<ECS::Object> a, UTILS::Ref<ECS::Object> b) {
					auto _a = std::abs(MATH::Vector3f::Distance(a->getTransform()->getWorldPosition(), origin));
				auto _b = std::abs(MATH::Vector3f::Distance(b->getTransform()->getWorldPosition(), origin));

				return _a > _b;
					});
				std::cout << objs.size() << std::endl;
			}
		}
	}

	drawGuizmo(w, h);

	ImGui::Columns(1);
	ImGui::End();
}

void GuizmoWindow::drawGuizmo(int w, int h) {
	const auto selectObject = EditorRender::GlobalState.mSelectObject;
	if (!selectObject) {
		return;
	}

	ImGuizmo::SetOrthographic(!mIsPerspective);
	ImGuizmo::BeginFrame();

	ImGuizmo::SetID(0);

	bool editTransformDecomposition = false;

	static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);
	static bool useSnap = false;
	static float snap[3] = {1.f, 1.f, 1.f};
	static float bounds[] = {-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
	static float boundsSnap[] = {0.1f, 0.1f, 0.1f};
	static bool boundSizing = false;
	static bool boundSizingSnap = false;

	auto& sceneManager = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>();
	auto cameraComponent = sceneManager.getCurrentScene().findMainCamera();
	auto cameraProjection = MATH::Matrix4f::Transpose(cameraComponent->getCamera().getProjectionMatrix());
	auto cameraView = MATH::Matrix4f::Transpose(cameraComponent->getCamera().getViewMatrix());
	auto matrix = MATH::Matrix4f::Transpose(selectObject->getTransform()->getWorldMatrix());


	if (editTransformDecomposition) {
		if (ImGui::IsKeyPressed(ImGuiKey_T))
			mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
		if (ImGui::IsKeyPressed(ImGuiKey_E))
			mCurrentGizmoOperation = ImGuizmo::ROTATE;
		if (ImGui::IsKeyPressed(ImGuiKey_R)) // r Key
			mCurrentGizmoOperation = ImGuizmo::SCALE;
		if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
			mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
		ImGui::SameLine();
		if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE))
			mCurrentGizmoOperation = ImGuizmo::ROTATE;
		ImGui::SameLine();
		if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE))
			mCurrentGizmoOperation = ImGuizmo::SCALE;
		if (ImGui::RadioButton("Universal", mCurrentGizmoOperation == ImGuizmo::UNIVERSAL))
			mCurrentGizmoOperation = ImGuizmo::UNIVERSAL;
		float matrixTranslation[3], matrixRotation[3], matrixScale[3];
		ImGuizmo::DecomposeMatrixToComponents(matrix.getData(), matrixTranslation, matrixRotation, matrixScale);
		ImGui::InputFloat3("Tr", matrixTranslation);
		ImGui::InputFloat3("Rt", matrixRotation);
		ImGui::InputFloat3("Sc", matrixScale);
		//ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, matrix.getData());

		if (mCurrentGizmoOperation != ImGuizmo::SCALE) {
			if (ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
				mCurrentGizmoMode = ImGuizmo::LOCAL;
			ImGui::SameLine();
			if (ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD))
				mCurrentGizmoMode = ImGuizmo::WORLD;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_S))
			useSnap = !useSnap;
		ImGui::Checkbox("##UseSnap", &useSnap);
		ImGui::SameLine();

		switch (mCurrentGizmoOperation) {
		case ImGuizmo::TRANSLATE:
			ImGui::InputFloat3("Snap", &snap[0]);
			break;
		case ImGuizmo::ROTATE:
			ImGui::InputFloat("Angle Snap", &snap[0]);
			break;
		case ImGuizmo::SCALE:
			ImGui::InputFloat("Scale Snap", &snap[0]);
			break;
		}
		ImGui::Checkbox("Bound Sizing", &boundSizing);
		if (boundSizing) {
			ImGui::PushID(3);
			ImGui::Checkbox("##BoundSizing", &boundSizingSnap);
			ImGui::SameLine();
			ImGui::InputFloat3("Snap", boundsSnap);
			ImGui::PopID();
		}
	}

	ImGuiIO& io = ImGui::GetIO();
	float viewManipulateRight = io.DisplaySize.x;
	float viewManipulateTop = 0;

	//if (useWindow)
	//{
	//	ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_Appearing);
	//	ImGui::SetNextWindowPos(ImVec2(400, 20), ImGuiCond_Appearing);
	//	ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(0.35f, 0.3f, 0.3f));
	//	ImGui::Begin("Gizmo", 0, gizmoWindowFlags);
	//	ImGuizmo::SetDrawlist();
	//	float windowWidth = (float)ImGui::GetWindowWidth();
	//	float windowHeight = (float)ImGui::GetWindowHeight();
	//	ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);
	//	viewManipulateRight = ImGui::GetWindowPos().x + windowWidth;
	//	viewManipulateTop = ImGui::GetWindowPos().y;
	//	ImGuiWindow* window = ImGui::GetCurrentWindow();
	//	gizmoWindowFlags = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(window->InnerRect.Min, window->InnerRect.Max) ? ImGuiWindowFlags_NoMove : 0;
	//}
	//else
	//{
	//	ImGuizmo::SetDrawlist();
	//	float windowWidth = (float)ImGui::GetWindowWidth();
	//	float windowHeight = (float)ImGui::GetWindowHeight();
	//	ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);
	//	viewManipulateRight = ImGui::GetWindowPos().x + windowWidth;
	//	viewManipulateTop = ImGui::GetWindowPos().y;
	//	
	//}
	std::string childID = "##levelpanel_playops";
	const ImVec2 windowPos = ImGui::GetWindowPos();
	const ImVec2 windowSize = ImGui::GetWindowSize();
	const ImRect confineRect = ImRect(windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y));

	const float   rounding = 4.0f;
	const float   itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
	const float   itemSpacingY = ImGui::GetStyle().ItemSpacing.y;
	const float   childRounding = 6.0f;
	const ImVec2  buttonSize = ImVec2(28, 28);
	const ImVec2  currentWindowPos = ImGui::GetWindowPos();
	const ImVec2  contentOffset = ImVec2(0.5f, -2.0f);
	static ImVec2 childSize = ImVec2(buttonSize.x * 6 + itemSpacingX * 7, buttonSize.y + itemSpacingY * 3);
	ImGui::SetNextWindowBgAlpha(0.5f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, childRounding);
	const ImVec2 defaultPos = ImVec2((confineRect.Max.x - confineRect.Min.x) - childSize.x - 12 - 10, 12);

	BeginMovableChild(childID.c_str(), childSize, defaultPos, confineRect, true, ImVec2(5, 10));
	ImGui::SameLine();
	PushIconFontSmall();
	ImGui::SetCursorPos(ImVec2(17, 6));

	const bool isInPlayMode = false;
	const bool isPaused = false;

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
	ImGui::Button(ICON_FA_PLAY, buttonSize);
	ImGui::PopStyleColor();

	ImGui::SameLine();
	ImGui::SetCursorPosY(6.0f);
	ImGui::Button(ICON_FA_PAUSE, buttonSize);

	ImGui::SameLine();
	ImGui::SetCursorPosY(6.0f);
	ImGui::Button(ICON_FA_FAST_FORWARD, buttonSize);

	ImGui::SameLine();
	ImGui::SetCursorPosY(6.0f);
	ImGui::Button(ICON_FA_CAMERA, buttonSize);

	ImGui::SameLine();
	ImGui::SetCursorPosY(6.0f);
	ImGui::Button(ICON_FA_EYE, buttonSize);

	ImGui::SameLine();
	ImGui::SetCursorPosY(6.0f);
	ImGui::Button(ICON_FA_VECTOR_SQUARE, buttonSize);

	ImGui::EndChild();
	ImGui::PopStyleVar();


	////////////////////

	ImVec2 imageRectMin = ImGui::GetWindowPos();
	ImVec2 imageRectMax = ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight());


	//ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::Enable(true);
	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(imageRectMin.x, imageRectMin.y, imageRectMax.x - imageRectMin.x, imageRectMax.y - imageRectMin.y);
	ImGui::PushClipRect(imageRectMin, imageRectMax, false);


	ImGuizmo::SetDrawlist();
	float windowWidth = (float)w;// ImGui::GetWindowWidth();
	float windowHeight = (float)h;// ImGui::GetWindowHeight();
	ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);
	viewManipulateRight = ImGui::GetWindowPos().x + windowWidth;
	viewManipulateTop = ImGui::GetWindowPos().y;


	ImGuizmo::MODE gizmoMode = ImGuizmo::MODE::LOCAL;
	ImGuizmo::Manipulate(cameraView.getData(), cameraProjection.getData(), mCurrentGizmoOperation, gizmoMode, matrix.getData());

	auto rot = cameraComponent->obj->getTransform()->getLocalRotationDeg();
	float _rot[] = {rot.x, rot.y, rot.z};
	auto viewData = ImGuizmo::ViewManipulate(cameraView.getData(), _rot, camDistance, ImVec2(viewManipulateRight - 128, viewManipulateTop), ImVec2(128, 128), 0x10101010);
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	mGizmoWindowFlags = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(window->InnerRect.Min, window->InnerRect.Max) ? ImGuiWindowFlags_NoMove : 0;

	if (viewData[0] == 1) {
		//auto vm = MATHGL::Matrix4::Transpose(cameraView);
		//vm(0, 3) = vm(1, 3) = vm(2,3) =
		//	vm(3,0) = vm(3,1) = vm(3,2) = 0.;
		//vm(3,3) = 1;
		//vm = MATHGL::Matrix4::Inverse(vm);
		//MATHGL::Quaternion q(vm);
		//auto rot = MATHGL::Quaternion::ToEulerAngles(q);


		//cameraComponent->obj->getTransform()->setLocalPosition(MATHGL::Vector3(viewData[1], viewData[2], viewData[3]));
		cameraComponent->obj->getTransform()->setLocalRotationDeg({viewData[4], viewData[5], viewData[6]});
		//cameraComponent->getCamera().setView(MATHGL::Matrix4::Transpose(cameraView));
	}
	if (ImGuizmo::IsUsing()) {
		float matrixTranslation[3], matrixRotation[3], matrixScale[3];
		ImGuizmo::DecomposeMatrixToComponents(matrix.getData(), matrixTranslation, matrixRotation, matrixScale);
		selectObject->getTransform()->setLocalPosition(MATH::Vector3f(matrixTranslation[0], matrixTranslation[1], matrixTranslation[2]));
		selectObject->getTransform()->setLocalScale(MATH::Vector3f(matrixScale[0], matrixScale[1], matrixScale[2]));
		selectObject->getTransform()->setLocalRotationDeg(MATH::Vector3f(matrixRotation[0], matrixRotation[1], matrixRotation[2]));
	}


}
#endif
