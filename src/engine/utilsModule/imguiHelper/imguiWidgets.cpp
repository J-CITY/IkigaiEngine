#include "imguiWidgets.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "misc/cpp/imgui_stdlib.h"
#include "utilsModule/assertion.h"

bool IKIGAI::IMGUI::InputText(const std::string& name, const std::string& tag, std::string& text) {
	ImGui::Text(name.c_str());
	ImGui::SameLine();
	return ImGui::InputText(tag.c_str(), &text);
}

bool IKIGAI::IMGUI::drawFloatN(std::string& name, int size, float* vec, float step, float mn, float mx) {
	switch (size) {
	case 2:
		return ImGui::DragFloat2(name.c_str(), vec, step);
	case 3:
		return ImGui::DragFloat3(name.c_str(), vec, step);
	case 4:
		return ImGui::DragFloat4(name.c_str(), vec, step);
	default: ASSERT("Error");
	}
	return false;
}

bool IKIGAI::IMGUI::drawColorN(std::string& name, int size, float* vec) {
	switch (size) {
	case 3:
		return ImGui::ColorEdit3(name.c_str(), vec);
	case 4:
		return ImGui::ColorEdit4(name.c_str(), vec);
	default: ASSERT("Error");
	}
	return false;
}

bool IKIGAI::IMGUI::SliderFloatWithSteps(const char* label, float* v, float v_min, float v_max, float v_step, const char* display_format) {
	if (!display_format)
		display_format = "%.3f";

	char text_buf[64] = {};
	ImFormatString(text_buf, IM_ARRAYSIZE(text_buf), display_format, *v);

	// Map from [v_min,v_max] to [0,N]
	const int countValues = int((v_max - v_min) / v_step);
	int v_i = int((*v - v_min) / v_step);
	const bool value_changed = ImGui::SliderInt(label, &v_i, 0, countValues, text_buf);

	// Remap from [0,N] to [v_min,v_max]
	*v = v_min + float(v_i) * v_step;
	return value_changed;
}

IKIGAI::MATH::Vector3f IKIGAI::IMGUI::ProjectPointOntoRay(const MATH::Vector3f& rayOrigin,
	const MATH::Vector3f& rayDirection, const MATH::Vector3f& point)
{
	MATH::Vector3f originToPoint = point - rayOrigin;

	// assume the direction is not normalized
	float dist = (rayDirection.dot(originToPoint)) / MATH::Vector3f::Length(rayDirection);

	MATH::Vector3f result = rayDirection * dist;
	result = rayOrigin + result;

	return result;
}

bool IKIGAI::IMGUI::RaySphereIntersection(const MATH::Vector3f& rayOrigin, const MATH::Vector3f& rayDirection,
	const MATH::Vector3f& sphereCenter, float radius, std::vector<MATH::Vector3f>& hits)
{
	// make sure the direction is a unit vector
	MATH::Vector3f direction = MATH::Vector3f::Normalize(rayDirection);

	MATH::Vector3f originToCenter = sphereCenter - rayOrigin;

	// check whether the center of the sphere is behind the ray origin
	if (MATH::Vector3f::Dot(originToCenter, direction) < 0.0f) {
		// the sphere center is behind the ray -> intersection is only possible if the ray is within the sphere

		float distance = MATH::Vector3f::Length(originToCenter);
		if (distance > radius) {
			// ray origin is outside the sphere
			return false;
		} else if (distance > (radius - 0.000001f) && distance < (radius + 0.000001f)) {
			// ray origin is on the sphere
			hits.push_back(rayOrigin);
			return true;
		} else {
			// get the projection point from the sphere center onto the ray
			MATH::Vector3f projected = ProjectPointOntoRay(rayOrigin, direction, sphereCenter);

			// get the intersection point
			float lengthProjCenter = MATH::Vector3f::Length(projected - sphereCenter);
			float dist = sqrtf((radius * radius) + (lengthProjCenter * lengthProjCenter));

			float lengthOriginIntersection = dist - MATH::Vector3f::Length(projected - rayOrigin);

			MATH::Vector3f hit = rayOrigin + (direction * lengthOriginIntersection);
			hits.push_back(hit);

			return true;
		}

	} else {
		// the sphere center is in front of the ray

		MATH::Vector3f projected = ProjectPointOntoRay(rayOrigin, direction, sphereCenter);

		float lengthProjCenter = MATH::Vector3f::Length(sphereCenter - projected);
		if (lengthProjCenter > radius) {
			// the projection point is outside the sphere -> no intersection
			return false;
		} else if (lengthProjCenter > (radius - 0.000001f) && lengthProjCenter < (radius + 0.000001f)) {
			// the projection point is on the sphere
			hits.push_back(projected);
			return true;
		}

		float lengthProjIntersection1 = sqrtf((radius * radius) + (lengthProjCenter * lengthProjCenter));

		// check whether the ray origin is within the sphere
		if (MATH::Vector3f::Length(originToCenter) < radius) {
			// there is only one intersection
			float lengthOriginIntersection = MATH::Vector3f::Length(projected - rayOrigin) + lengthProjIntersection1;

			MATH::Vector3f hit = rayOrigin + (direction * lengthOriginIntersection);
			hits.push_back(hit);

			return true;
		} else {
			// there are two intersections
			// get the first intersection
			float lengthProjOrigin = MATH::Vector3f::Length(projected - rayOrigin);
			float lengthOriginIntersection = lengthProjOrigin - lengthProjIntersection1;
			MATH::Vector3f hit = rayOrigin + (direction * lengthOriginIntersection);
			hits.push_back(hit);

			// get the second intersection point
			lengthOriginIntersection = lengthProjOrigin + lengthProjIntersection1;
			hit = rayOrigin + (direction * lengthOriginIntersection);
			hits.push_back(hit);

			return true;
		}

	}
}

IKIGAI::IMGUI::CombineVecEdit::CombineVecEdit(std::string_view name, size_t vecSize, MODE mode, float mn, float mx,
                                              float step):
	mVecSize(vecSize), mName(name), mMode(mode), mMin(mn), mMax(mx), mStep(step)
{
	switch (mVecSize) {
	case 2: mButtonLblVec = "XY"; mButtonLblColor = "RG"; mMode = MODE::POS; break;
	case 3: mButtonLblVec = "XYZ"; mButtonLblColor = "RGB"; mWidth = 150.0f; break;
	case 4: mButtonLblVec = "XYZW"; mButtonLblColor = "RGBA"; mWidth = 150.0f; break;
	default: ASSERT("Support only 2, 3, 4 vec size");
	}
	if (mode == MODE::POS) {
		mIsColorMode = false;
	}
	if (mode == MODE::COLOR) {
		mIsColorMode = true;
	}
}

bool IKIGAI::IMGUI::CombineVecEdit::draw(float* vec) {
	ImGui::Columns(2);
	ImGui::SetColumnWidth(1, 200.0f);

	ImGui::Text(mName.c_str());
	ImGui::NextColumn();

	ImGui::SetNextItemWidth(mWidth);

	bool res = false;
	auto _nameId = "##" + mName;
	if (mIsColorMode) {
		if (IKIGAI::IMGUI::drawColorN(_nameId, mVecSize, vec)) {
			res = true;
		}
	} else {
		if (IKIGAI::IMGUI::drawFloatN(_nameId, mVecSize, vec, mStep, mMin, mMax)) {
			res = true;
		}
	}
	if (mMode == MODE::COMBINE) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(50);
		if (ImGui::SmallButton(mIsColorMode ? mButtonLblColor.c_str() : mButtonLblVec.c_str())) {
			mIsColorMode = !mIsColorMode;
		}
	}

	ImGui::NextColumn();
	ImGui::Columns(1);
	return res;
}

int IKIGAI::IMGUI::DrawCombineVecEdit(const std::string& name, int size, float* vec, float mn, float mx, float step, bool allowColorMode) {
	ImGui::Columns(2);
	ImGui::SetColumnWidth(1, 200.0f);
	ImGui::Text(name.c_str());
	ImGui::NextColumn();

	float width = (size == 2) ? 200.0f : 150.0f;
	ImGui::SetNextItemWidth(width);

	int res = 0;
	auto _nameId = "##" + name;
	ImGuiID id = ImGui::GetID(_nameId.c_str());
	ImGuiStorage* storage = ImGui::GetStateStorage();
	bool isColorMode = storage->GetBool(id, false);

	if (isColorMode && allowColorMode && (size == 3 || size == 4)) {
		if (IKIGAI::IMGUI::drawColorN(_nameId, size, vec)) {
			res |= 1;
		}
	} else {
		if (IKIGAI::IMGUI::drawFloatN(_nameId, size, vec, step, mn, mx)) {
			res |= 1;
		}
	}

	if (ImGui::IsItemActivated()) res |= 2;
	if (ImGui::IsItemDeactivatedAfterEdit()) res |= 4;

	ImGui::SameLine();
	ImGui::SetNextItemWidth(50);
	std::string btnLbl;
	if (isColorMode) {
		btnLbl = (size == 3) ? "RGB" : ((size == 4) ? "RGBA" : "RG");
	} else {
		btnLbl = (size == 2) ? "XY" : ((size == 3) ? "XYZ" : "XYZW");
	}

	if (ImGui::SmallButton((btnLbl + _nameId).c_str())) {
		if (allowColorMode && (size == 3 || size == 4)) {
			storage->SetBool(id, !isColorMode);
		}
	}

	ImGui::NextColumn();
	ImGui::Columns(1);
	return res;
}

inline float BezierValue(float dt01, float P[4]) {
	enum { STEPS = 256 };
	ImVec2 Q[4] = {{0, 0}, {P[0], P[1]}, {P[2], P[3]}, {1, 1}};
	ImVec2 results[STEPS + 1];
	ImGui::bezier_table<STEPS>(Q, results);
	return results[(int)((dt01 < 0 ? 0 : dt01 > 1 ? 1 : dt01) * static_cast<float>(STEPS))].y;
}

int ImGui::Bezier(const char* label, float P[])
{
	// visuals
	enum {
		SMOOTHNESS = 64
	};  // curve smoothness: the higher number of segments, the smoother curve
	enum { CURVE_WIDTH = 4 };  // main curved line width
	enum { LINE_WIDTH = 1 };   // handlers: small lines width
	enum { GRAB_RADIUS = 8 };  // handlers: circle radius
	enum { GRAB_BORDER = 2 };  // handlers: circle border width
	enum {
		AREA_CONSTRAINED = true
	};  // should grabbers be constrained to grid area?
	enum {
		AREA_WIDTH = 128
	};  // area width in pixels. 0 for adaptive size (will use max avail width)

	// curve presets
	static struct {
		const char* name;
		float points[4];
	} presets[] = {
			{"Linear", {0.000f, 0.000f, 1.000f, 1.000f}},

			{"In Sine", {0.470f, 0.000f, 0.745f, 0.715f}},
			{"In Quad", {0.550f, 0.085f, 0.680f, 0.530f}},
			{"In Cubic", {0.550f, 0.055f, 0.675f, 0.190f}},
			{"In Quart", {0.895f, 0.030f, 0.685f, 0.220f}},
			{"In Quint", {0.755f, 0.050f, 0.855f, 0.060f}},
			{"In Expo", {0.950f, 0.050f, 0.795f, 0.035f}},
			{"In Circ", {0.600f, 0.040f, 0.980f, 0.335f}},
			{"In Back", {0.600f, -0.28f, 0.735f, 0.045f}},

			{"Out Sine", {0.390f, 0.575f, 0.565f, 1.000f}},
			{"Out Quad", {0.250f, 0.460f, 0.450f, 0.940f}},
			{"Out Cubic", {0.215f, 0.610f, 0.355f, 1.000f}},
			{"Out Quart", {0.165f, 0.840f, 0.440f, 1.000f}},
			{"Out Quint", {0.230f, 1.000f, 0.320f, 1.000f}},
			{"Out Expo", {0.190f, 1.000f, 0.220f, 1.000f}},
			{"Out Circ", {0.075f, 0.820f, 0.165f, 1.000f}},
			{"Out Back", {0.175f, 0.885f, 0.320f, 1.275f}},

			{"InOut Sine", {0.445f, 0.050f, 0.550f, 0.950f}},
			{"InOut Quad", {0.455f, 0.030f, 0.515f, 0.955f}},
			{"InOut Cubic", {0.645f, 0.045f, 0.355f, 1.000f}},
			{"InOut Quart", {0.770f, 0.000f, 0.175f, 1.000f}},
			{"InOut Quint", {0.860f, 0.000f, 0.070f, 1.000f}},
			{"InOut Expo", {1.000f, 0.000f, 0.000f, 1.000f}},
			{"InOut Circ", {0.785f, 0.135f, 0.150f, 0.860f}},
			{"InOut Back", {0.680f, -0.55f, 0.265f, 1.550f}},

			// easeInElastic: not a bezier
			// easeOutElastic: not a bezier
			// easeInOutElastic: not a bezier
			// easeInBounce: not a bezier
			// easeOutBounce: not a bezier
			// easeInOutBounce: not a bezier
		};

	// preset selector

	bool reload = 0;
	ImGui::PushID(label);
	if (ImGui::ArrowButton(
		"##lt",
		ImGuiDir_Left)) {  // ImGui::ArrowButton(ImGui::GetCurrentWindow()->GetID("##lt"),
		// ImGuiDir_Left, ImVec2(0, 0), 0)
		if (--P[4] >= 0)
			reload = 1;
		else
			++P[4];
	}
	ImGui::SameLine();

	if (ImGui::Button("Presets")) {
		ImGui::OpenPopup("!Presets");
	}
	if (ImGui::BeginPopup("!Presets")) {
		for (int i = 0; i < IM_ARRAYSIZE(presets); ++i) {
			if (i == 1 || i == 9 || i == 17)
				ImGui::Separator();
			if (ImGui::MenuItem(presets[i].name, NULL, P[4] == i)) {
				P[4] = i;
				reload = 1;
			}
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();

	if (ImGui::ArrowButton(
		"##rt",
		ImGuiDir_Right)) {  // ImGui::ArrowButton(ImGui::GetCurrentWindow()->GetID("##rt"),
		// ImGuiDir_Right, ImVec2(0, 0), 0)
		if (++P[4] < IM_ARRAYSIZE(presets))
			reload = 1;
		else
			--P[4];
	}
	ImGui::SameLine();
	ImGui::PopID();

	if (reload) {
		memcpy(P, presets[(int)P[4]].points, sizeof(float) * 4);
	}

	// bezier widget

	const ImGuiStyle& Style = GetStyle();
	// const ImGuiIO& IO = GetIO();
	ImDrawList* DrawList = GetWindowDrawList();
	ImGuiWindow* Window = GetCurrentWindow();
	if (Window->SkipItems)
		return false;

	// header and spacing
	int changed = 0;
	changed = SliderFloat4(label, P, 0, 1, "%.3f", 1.0f);

	int hovered = IsItemActive() || IsItemHovered();  // IsItemDragged() ?
	Dummy(ImVec2(0, 3));

	// prepare canvas
	const float avail = GetContentRegionAvail().x;
	const float dim = AREA_WIDTH > 0 ? AREA_WIDTH : avail;
	ImVec2 Canvas(dim, dim);

	ImRect bb(Window->DC.CursorPos, Window->DC.CursorPos + Canvas);
	ItemSize(bb);
	if (!ItemAdd(bb, NULL))
		return changed;

	const ImGuiID id = Window->GetID(label);
	hovered |=
		0 != ItemHoverable(ImRect(bb.Min, bb.Min + ImVec2(avail, dim)), id, ImGuiItemFlags_None);

	RenderFrame(bb.Min, bb.Max, GetColorU32(ImGuiCol_FrameBg, 1), true,
	            Style.FrameRounding);

	// background grid
	for (int i = 0; i <= Canvas.x; i += (Canvas.x / 4)) {
		DrawList->AddLine(ImVec2(bb.Min.x + i, bb.Min.y),
		                  ImVec2(bb.Min.x + i, bb.Max.y),
		                  GetColorU32(ImGuiCol_TextDisabled));
	}
	for (int i = 0; i <= Canvas.y; i += (Canvas.y / 4)) {
		DrawList->AddLine(ImVec2(bb.Min.x, bb.Min.y + i),
		                  ImVec2(bb.Max.x, bb.Min.y + i),
		                  GetColorU32(ImGuiCol_TextDisabled));
	}

	// eval curve
	ImVec2 Q[4] = {{0, 0}, {P[0], P[1]}, {P[2], P[3]}, {1, 1}};
	ImVec2 results[SMOOTHNESS + 1];
	bezier_table<SMOOTHNESS>(Q, results);

	// control points: 2 lines and 2 circles
	{
		// handle grabbers
		ImVec2 mouse = GetIO().MousePos, pos[2];
		float distance[2];

		for (int i = 0; i < 2; ++i) {
			pos[i] =
				ImVec2(P[i * 2 + 0], 1 - P[i * 2 + 1]) * (bb.Max - bb.Min) + bb.Min;
			distance[i] = (pos[i].x - mouse.x) * (pos[i].x - mouse.x) +
				(pos[i].y - mouse.y) * (pos[i].y - mouse.y);
		}

		for (int i = 0; i < 2; ++i) {
		}

		int selected = distance[0] < distance[1] ? 0 : 1;
		if (distance[selected] < (4 * GRAB_RADIUS * 4 * GRAB_RADIUS)) {
			SetTooltip("(%4.3f, %4.3f)", P[selected * 2 + 0], P[selected * 2 + 1]);

			if (hovered && (IsMouseClicked(0) || IsMouseDragging(0))) {
				float canvasScale = 1.0;// / ImGui::GetFontSize();
				float& px = P[selected * 2 + 0];
				px += GetIO().MouseDelta.x / (Canvas.x * canvasScale);
				float& py = P[selected * 2 + 1];
				py -= GetIO().MouseDelta.y / (Canvas.y * canvasScale);

				if (AREA_CONSTRAINED) {
					px = (px < 0 ? 0 : (px > 1 ? 1 : px));
					py = (py < 0 ? 0 : (py > 1 ? 1 : py));
				}

				changed = true;
			}
		}
	}

	// if (hovered || changed) DrawList->PushClipRectFullScreen();

	// draw curve
	{
		ImColor color(GetStyle().Colors[ImGuiCol_PlotLines]);
		for (int i = 0; i < SMOOTHNESS; ++i) {
			ImVec2 p = {results[i + 0].x, 1 - results[i + 0].y};
			ImVec2 q = {results[i + 1].x, 1 - results[i + 1].y};
			ImVec2 r(p.x * (bb.Max.x - bb.Min.x) + bb.Min.x,
			         p.y * (bb.Max.y - bb.Min.y) + bb.Min.y);
			ImVec2 s(q.x * (bb.Max.x - bb.Min.x) + bb.Min.x,
			         q.y * (bb.Max.y - bb.Min.y) + bb.Min.y);
			DrawList->AddLine(r, s, color, CURVE_WIDTH);
		}
	}

	// draw preview (cycles every 1s)
	ImVec4 white(GetStyle().Colors[ImGuiCol_Text]);
	//static clock_t epoch = clock();
	//for (int i = 0; i < 3; ++i) {
	//	double now = ((clock() - epoch) / (double)CLOCKS_PER_SEC);
	//	float delta = ((int)(now * 1000) % 1000) / 1000.f;
	//	delta += i / 3.f;
	//	if (delta > 1)
	//		delta -= 1;
	//	int idx = (int)(delta * SMOOTHNESS);
	//	float evalx = results[idx].x;  //
	//	float evaly = results[idx].y;  // ImGui::BezierValue( delta, P );
	//	ImVec2 p0 = ImVec2(evalx, 1 - 0) * (bb.Max - bb.Min) + bb.Min;
	//	ImVec2 p1 = ImVec2(0, 1 - evaly) * (bb.Max - bb.Min) + bb.Min;
	//	ImVec2 p2 = ImVec2(evalx, 1 - evaly) * (bb.Max - bb.Min) + bb.Min;
	//	DrawList->AddCircleFilled(p0, GRAB_RADIUS / 2, ImColor(white));
	//	DrawList->AddCircleFilled(p1, GRAB_RADIUS / 2, ImColor(white));
	//	DrawList->AddCircleFilled(p2, GRAB_RADIUS / 2, ImColor(white));
	//}

	// draw lines and grabbers
	float luma = IsItemActive() || IsItemHovered() ? 0.5f : 1.0f;
	ImVec4 pink(1.00f, 0.00f, 0.75f, luma), cyan(0.00f, 0.75f, 1.00f, luma);
	ImVec2 p1 = ImVec2(P[0], 1 - P[1]) * (bb.Max - bb.Min) + bb.Min;
	ImVec2 p2 = ImVec2(P[2], 1 - P[3]) * (bb.Max - bb.Min) + bb.Min;
	DrawList->AddLine(ImVec2(bb.Min.x, bb.Max.y), p1, ImColor(white), LINE_WIDTH);
	DrawList->AddLine(ImVec2(bb.Max.x, bb.Min.y), p2, ImColor(white), LINE_WIDTH);
	DrawList->AddCircleFilled(p1, GRAB_RADIUS, ImColor(white));
	DrawList->AddCircleFilled(p1, GRAB_RADIUS - GRAB_BORDER, ImColor(pink));
	DrawList->AddCircleFilled(p2, GRAB_RADIUS, ImColor(white));
	DrawList->AddCircleFilled(p2, GRAB_RADIUS - GRAB_BORDER, ImColor(cyan));

	// if (hovered || changed) DrawList->PopClipRect();

	return changed;
}
