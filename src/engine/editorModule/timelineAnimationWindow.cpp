#include "timelineAnimationWindow.h"

#include "utilsModule/animation.h"
#include "utilsModule/tinyspline/tinysplinecxx.h"


#ifdef USE_EDITOR

#define IMGUI_DEFINE_MATH_OPERATORS

#include "utilsModule/imguiHelper/imgui_neo_sequencer.h"
#include "editorRender.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "renderModule/backends/gl/materialGl.h"
#include "resourceModule/materialManager.h"

#include "sceneModule/sceneManager.h"
#include "misc/cpp/imgui_stdlib.h"
#include "resourceModule/textureManager.h"
#include "utilsModule/imguiHelper/imguiWidgets.h"
#include "utilsModule/time/time.h"

#include "utilsModule/time/time.h"


using namespace ImGui;
static const float NODE_SLOT_RADIUS = 4.0f;

enum class CurveEditorFlags {
	NO_TANGENTS = 1 << 0,
	SHOW_GRID = 1 << 1,
	RESET = 1 << 2,
	NO_MOVE_X = 1 << 3,
	NO_MOVE_Y = 1 << 4,
	WITH_OFFSETS = 1 << 5,
	NO_DELETE = 1 << 6,
	NO_ADD = 1 << 7,
	NO_POINT_ORDER = 1 << 8,
};
static constexpr float HANDLE_RADIUS = 4;


int CurveEditorExt(const char* label, std::vector<IKIGAI::ANIMATION::Curve>& values, 
	const ImVec2& editor_size, ImU32 flags, float maxTangentRadius, int* new_count, int* selected_point, int* hovered_point, 
	const ImVec2& x_span = ImVec2(0, 0), const ImVec2& y_span = ImVec2(0, 0)) {

	enum class StorageValues : ImGuiID {
		FROM_X = 100,
		FROM_Y,
		WIDTH,
		HEIGHT,
		IS_PANNING,
		POINT_START_X,
		POINT_START_Y,
	};

	const float HEIGHT = 100;
	const float SCALE_FACTOR = 1.1f;
	const float EXTRA_OFFSET = 0.5f;
	static ImVec2 start_pan;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	ImVec2 size = editor_size;
	size.x = size.x < 0 ? CalcItemWidth() + (style.FramePadding.x * 2) : size.x;
	size.y = size.y < 0 ? HEIGHT : size.y;
	if (hovered_point) *hovered_point = -1;

	ImGuiWindow* parent_window = GetCurrentWindow();
	ImGuiID id = parent_window->GetID(label);

	const auto points_count = values.size();

	if (new_count) {
		*new_count = points_count;
	}

	if (!BeginChild(id, size, ImGuiChildFlags_FrameStyle, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		EndChild();
		return -1;
	}

	int hovered_idx = -1;

	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems) {
		EndChild();
		return -1;
	}

	ImVec2 points_min(FLT_MAX, FLT_MAX);
	ImVec2 points_max(-FLT_MAX, -FLT_MAX);
	for (int point_idx = 0; point_idx < points_count; ++point_idx) {
		ImVec2 point;
		//if (flags & (int)CurveEditorFlags::NO_TANGENTS) {
			point = ImVec2(values[point_idx].mLeft.x, values[point_idx].mLeft.y);
		//} else {
		//	point = ((ImVec2*)values)[1 + point_idx * 3];
		//}
		points_max = ImMax(points_max, point);
		points_min = ImMin(points_min, point);
	}
	points_max.y = ImMax(points_max.y, points_min.y + 0.0001f);

	if (x_span.x < x_span.y) {//need wrap to span by x
		points_min.x = std::max(points_min.x, x_span.x - EXTRA_OFFSET);
		points_max.x = std::min(points_max.x, x_span.y + EXTRA_OFFSET);
	}
	if (y_span.x < y_span.y) {//need wrap to span by y
		points_min.y = std::max(points_min.y, y_span.x - EXTRA_OFFSET);
		points_max.y = std::min(points_max.y, y_span.y + EXTRA_OFFSET);
	}

	if (flags & (int)CurveEditorFlags::RESET) window->StateStorage.Clear();

	float from_x = window->StateStorage.GetFloat((ImGuiID)StorageValues::FROM_X, points_min.x);
	float from_y = window->StateStorage.GetFloat((ImGuiID)StorageValues::FROM_Y, points_min.y);
	float width = window->StateStorage.GetFloat((ImGuiID)StorageValues::WIDTH, points_max.x - points_min.x);
	float height = window->StateStorage.GetFloat((ImGuiID)StorageValues::HEIGHT, points_max.y - points_min.y);

	if (x_span.x < x_span.y) {//need wrap to span by x
		from_x = std::max(from_x, x_span.x - EXTRA_OFFSET);
		if (from_x + width > x_span.y + EXTRA_OFFSET) {
			width = x_span.y + EXTRA_OFFSET - from_x;
		}
	}
	if (y_span.x < y_span.y) {//need wrap to span by y
		from_y = std::max(from_y, y_span.x - EXTRA_OFFSET);
		if (from_y + height > y_span.y + EXTRA_OFFSET) {
			height = y_span.y + EXTRA_OFFSET - from_y;
		}
	}

	window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_X, from_x);
	window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_Y, from_y);
	window->StateStorage.SetFloat((ImGuiID)StorageValues::WIDTH, width);
	window->StateStorage.SetFloat((ImGuiID)StorageValues::HEIGHT, height);

	const ImRect inner_bb = window->InnerClipRect;
	if (inner_bb.GetWidth() == 0 || inner_bb.GetHeight() == 0) {
		EndChild();
		return -1;
	}
	const ImRect frame_bb(inner_bb.Min - style.FramePadding, inner_bb.Max + style.FramePadding);

	auto transform = [&](const ImVec2& pos) -> ImVec2 {
		float x = (pos.x - from_x) / width;
		float y = (pos.y - from_y) / height;

		return ImVec2(
			inner_bb.Min.x * (1 - x) + inner_bb.Max.x * x,
			inner_bb.Min.y * y + inner_bb.Max.y * (1 - y)
		);
	};

	auto invTransform = [&](const ImVec2& pos) -> ImVec2 {
		float x = (pos.x - inner_bb.Min.x) / (inner_bb.Max.x - inner_bb.Min.x);
		float y = (inner_bb.Max.y - pos.y) / (inner_bb.Max.y - inner_bb.Min.y);

		return ImVec2(
			from_x + width * x,
			from_y + height * y
		);
	};

	if (flags & (int)CurveEditorFlags::SHOW_GRID) {//draw grid
		int exp;
		frexp(width / 5, &exp);
		float step_x = (float)ldexp(1.0, exp);
		int cell_cols = int(width / step_x);

		float x = step_x * int(from_x / step_x);
		for (int i = -1; i < cell_cols + 2; ++i) {
			ImVec2 a = transform({x + i * step_x, from_y});
			ImVec2 b = transform({x + i * step_x, from_y + height});
			window->DrawList->AddLine(a, b, 0x55000000);
			char buf[64];
			if (exp > 0) {
				ImFormatString(buf, sizeof(buf), " %d", int(x + i * step_x));
			} else {
				ImFormatString(buf, sizeof(buf), " %.2f", x + i * step_x);
			}
			window->DrawList->AddText(b, 0x55000000, buf);
		}

		frexp(height / 5, &exp);
		float step_y = (float)ldexp(1.0, exp);
		int cell_rows = int(height / step_y);

		float y = step_y * int(from_y / step_y);
		for (int i = -1; i < cell_rows + 2; ++i) {
			ImVec2 a = transform({from_x, y + i * step_y});
			ImVec2 b = transform({from_x + width, y + i * step_y});
			window->DrawList->AddLine(a, b, 0x55000000);
			char buf[64];
			if (exp > 0) {
				ImFormatString(buf, sizeof(buf), " %d", int(y + i * step_y));
			} else {
				ImFormatString(buf, sizeof(buf), " %.2f", y + i * step_y);
			}
			window->DrawList->AddText(a, 0x55000000, buf);
		}
	}

	const ImGuiID dragger_id = GetID("##_node_dragger");
	ImGui::ItemAdd(inner_bb, dragger_id);

	if (GetIO().MouseWheel != 0 && IsItemHovered()) {
		float scale = powf(SCALE_FACTOR, -GetIO().MouseWheel);
		width *= scale;
		height *= scale;
		if (x_span.x < x_span.y) {//need wrap to span by x
			if (from_x + width > x_span.y + EXTRA_OFFSET) {
				width = x_span.y + EXTRA_OFFSET - from_x;
			}
		}
		if (y_span.x < y_span.y) {//need wrap to span by y
			if (from_y + height > y_span.y + EXTRA_OFFSET) {
				height = y_span.y + EXTRA_OFFSET - from_y;
			}
		}
		window->StateStorage.SetFloat((ImGuiID)StorageValues::WIDTH, width);
		window->StateStorage.SetFloat((ImGuiID)StorageValues::HEIGHT, height);
	}
	if (IsMouseReleased(2)) {
		window->StateStorage.SetBool((ImGuiID)StorageValues::IS_PANNING, false);
	}
	if (window->StateStorage.GetBool((ImGuiID)StorageValues::IS_PANNING, false)) {
		ImVec2 drag_offset = GetMouseDragDelta(2);
		auto from_x_save = from_x;
		auto from_y_save = from_y;
		from_x = start_pan.x;
		from_y = start_pan.y;
		from_x -= drag_offset.x * width / (inner_bb.Max.x - inner_bb.Min.x);
		from_y += drag_offset.y * height / (inner_bb.Max.y - inner_bb.Min.y);
		if (x_span.x < x_span.y) {//need wrap to span by x
			if (from_x + width < x_span.y + EXTRA_OFFSET) {
				from_x = std::max(from_x, x_span.x - EXTRA_OFFSET);
			}
			else {
				from_x = from_x_save;
			}
		}
		if (y_span.x < y_span.y) {//need wrap to span by y
			if (from_y + height < y_span.y + EXTRA_OFFSET) {
				from_y = std::max(from_y, y_span.x - EXTRA_OFFSET);
			}
			else {
				from_y = from_y_save;
			}
		}
		window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_X, from_x);
		window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_Y, from_y);
	} else if (IsMouseDragging(2) && IsItemHovered()) {
		window->StateStorage.SetBool((ImGuiID)StorageValues::IS_PANNING, true);
		start_pan.x = from_x;
		start_pan.y = from_y;
	}

	int changed_idx = -1;
	for (int point_idx = points_count - 2; point_idx >= 0; --point_idx) {
		IKIGAI::ANIMATION::Curve& point = values[point_idx];
		IKIGAI::ANIMATION::Curve& pointNext = values[point_idx + 1];

		ImVec2 p_prev = ImVec2(point.mLeft.x, point.mLeft.y);
		ImVec2 tangent_last = ImVec2(point.mLeftTangent.x, point.mLeftTangent.y);
		ImVec2 tangent = ImVec2(point.mRightTangent.x, point.mRightTangent.y);
		ImVec2 p = ImVec2(pointNext.mLeft.x, pointNext.mLeft.y);

		auto handlePoint = [&](ImVec2& p, int idx, float drawOffset = 0.0f, ImU32 color = GetColorU32(ImGuiCol_PlotLines), ImU32 colorHover = GetColorU32(ImGuiCol_PlotLinesHovered)) -> bool {
			static const float SIZE = 3;

			ImVec2 cursor_pos = GetCursorScreenPos();
			ImVec2 pos = transform(p);

			pos.y += drawOffset;

			SetCursorScreenPos(pos - ImVec2(SIZE, SIZE));
			PushID(idx + 1);
			InvisibleButton("", ImVec2(2 * HANDLE_RADIUS, 2 * HANDLE_RADIUS));

			bool is_selected = selected_point && *selected_point == point_idx + idx;
			float thickness = is_selected ? 2.0f : 1.0f;
			ImU32 col = IsItemActive() || IsItemHovered() ? colorHover : color;

			window->DrawList->AddLine(pos + ImVec2(-SIZE, 0), pos + ImVec2(0, SIZE), col, thickness);
			window->DrawList->AddLine(pos + ImVec2(SIZE, 0), pos + ImVec2(0, SIZE), col, thickness);
			window->DrawList->AddLine(pos + ImVec2(SIZE, 0), pos + ImVec2(0, -SIZE), col, thickness);
			window->DrawList->AddLine(pos + ImVec2(-SIZE, 0), pos + ImVec2(0, -SIZE), col, thickness);

			if (IsItemHovered()) hovered_idx = point_idx + idx;

			bool changed = false;
			if (IsItemActive() && IsMouseClicked(0)) {
				if (selected_point) *selected_point = point_idx + idx;
				window->StateStorage.SetFloat((ImGuiID)StorageValues::POINT_START_X, pos.x);
				window->StateStorage.SetFloat((ImGuiID)StorageValues::POINT_START_Y, pos.y);
			}

			if (IsItemHovered() || (IsItemActive() && IsMouseDragging(0))) {
				char tmp[64];
				ImFormatString(tmp, sizeof(tmp), "%0.2f, %0.2f", p.x, p.y);
				window->DrawList->AddText({pos.x, pos.y - GetTextLineHeight()}, 0xff000000, tmp);
			}

			if (IsItemActive() && IsMouseDragging(0)) {
				pos.x = window->StateStorage.GetFloat((ImGuiID)StorageValues::POINT_START_X, pos.x);
				pos.y = window->StateStorage.GetFloat((ImGuiID)StorageValues::POINT_START_Y, pos.y);
				pos += GetMouseDragDelta();
				pos.y -= drawOffset;
				ImVec2 v = invTransform(pos);

				p = v;
				if (x_span.x < x_span.y) {//need wrap to span by x
					if (p.x < x_span.x) {
						p.x = x_span.x;
					}
					if (p.x > x_span.y) {
						p.x = x_span.y;
					}
				}
				if (y_span.x < y_span.y) {//need wrap to span by y
					if (p.y < y_span.x) {
						p.y = y_span.x;
					}
					if (p.y > y_span.y) {
						p.y = y_span.y;
					}
				}

				changed = true;
				for (int i = 0; i < values.size() - 1; i++) {
					values[i].mRight = values[i + 1].mLeft;
				}
			}
			PopID();

			SetCursorScreenPos(cursor_pos);
			return changed;
		};

		auto handleTangent = [&](ImVec2& t, const ImVec2& p, int idx) -> bool {
			static const float SIZE = 2;
			static const float LENGTH = 80;

			auto normalized = [](const ImVec2& v) -> ImVec2 {
				float len = 1.0f / sqrtf(v.x * v.x + v.y * v.y);
				return ImVec2(v.x * len, v.y * len);
			};

			ImVec2 cursor_pos = GetCursorScreenPos();
			ImVec2 pos = transform(p);
			ImVec2 tang = pos + ImVec2(t.x, -t.y) * LENGTH;

			SetCursorScreenPos(tang - ImVec2(SIZE, SIZE));
			PushID(-(idx + 1));
			InvisibleButton("", ImVec2(2 * HANDLE_RADIUS, 2 * HANDLE_RADIUS));

			window->DrawList->AddLine(pos, tang, GetColorU32(ImGuiCol_PlotLines));

			ImU32 col = IsItemHovered() ? GetColorU32(ImGuiCol_PlotLinesHovered) : GetColorU32(ImGuiCol_PlotLines);

			window->DrawList->AddLine(tang + ImVec2(-SIZE, SIZE), tang + ImVec2(SIZE, SIZE), col);
			window->DrawList->AddLine(tang + ImVec2(SIZE, SIZE), tang + ImVec2(SIZE, -SIZE), col);
			window->DrawList->AddLine(tang + ImVec2(SIZE, -SIZE), tang + ImVec2(-SIZE, -SIZE), col);
			window->DrawList->AddLine(tang + ImVec2(-SIZE, -SIZE), tang + ImVec2(-SIZE, SIZE), col);

			bool changed = false;
			if (IsItemActive() && IsMouseDragging(0)) {
				tang = GetIO().MousePos - pos;
				tang = tang / LENGTH;
				//tang = normalized(tang);
				tang.y *= -1;

				t = tang;
				changed = true;
			}
			PopID();

			SetCursorScreenPos(cursor_pos);
			return changed;
		};

		PushID(point_idx + 1);
		if ((flags & (int)CurveEditorFlags::NO_TANGENTS) == 0) {
			window->DrawList->AddBezierCubic(
				transform(p_prev),
				transform(p_prev + tangent_last),
				transform(p + tangent),
				transform(p),
				GetColorU32(ImGuiCol_PlotLines),
				1.0f,
				20);

			if ((flags & (int)CurveEditorFlags::WITH_OFFSETS) == (int)CurveEditorFlags::WITH_OFFSETS) {
				if (point.mLeftYOffset.y > 0 || pointNext.mLeftYOffset.y > 0)
				window->DrawList->AddBezierCubic(
					transform(p_prev + ImVec2(0.0f, point.mLeftYOffset.y)),
					transform(p_prev + ImVec2(0.0f, point.mLeftYOffset.y) + tangent_last),
					transform(p + ImVec2(0.0f, pointNext.mLeftYOffset.y) + tangent),
					transform(p + ImVec2(0.0f, pointNext.mLeftYOffset.y)),
					GetColorU32(ImGuiCol_PlotHistogram),
					1.0f,
					20);
				if (point.mLeftYOffset.x < 0 || pointNext.mLeftYOffset.x < 0)
				window->DrawList->AddBezierCubic(
					transform(p_prev + ImVec2(0.0f, point.mLeftYOffset.x)),
					transform(p_prev + ImVec2(0.0f, point.mLeftYOffset.x) + tangent_last),
					transform(p + ImVec2(0.0f, pointNext.mLeftYOffset.x) + tangent),
					transform(p + ImVec2(0.0f, pointNext.mLeftYOffset.x)),
					GetColorU32(ImGuiCol_PlotHistogram),
					1.0f,
					20);
				//TODO:: cntrl for move both points
				const float drawOffset = 20.0f;
				ImVec2 pOffsetPlus = p;
				pOffsetPlus.y += pointNext.mLeftYOffset.y;
				if (handlePoint(pOffsetPlus, 1000, -drawOffset, GetColorU32(ImGuiCol_PlotHistogramHovered), GetColorU32(ImGuiCol_PlotHistogramHovered))) {
					pointNext.mLeftYOffset.y = std::max(0.0f, pOffsetPlus.y - p.y);
				}

				ImVec2 pOffsetMinus = p;
				pOffsetMinus.y += pointNext.mLeftYOffset.x;
				if (handlePoint(pOffsetMinus, -1000, drawOffset, GetColorU32(ImGuiCol_PlotHistogramHovered), GetColorU32(ImGuiCol_PlotHistogramHovered))) {
					pointNext.mLeftYOffset.x = std::min(0.0f, pOffsetMinus.y - p.y);
				}
			}

			if (handleTangent(tangent_last, p_prev, 0)) {
				const auto v = ImClamp(tangent_last, ImVec2(-maxTangentRadius, -maxTangentRadius), ImVec2(maxTangentRadius, maxTangentRadius));
				point.mLeftTangent = IKIGAI::MATH::Vector2f(v.x, v.y);
				changed_idx = point_idx;
			}
			if (handleTangent(tangent, p, 1)) {
				const auto v = ImClamp(tangent, ImVec2(-maxTangentRadius, -maxTangentRadius), ImVec2(maxTangentRadius, maxTangentRadius));
				point.mRightTangent = IKIGAI::MATH::Vector2f(v.x, v.y);
				changed_idx = point_idx + 1;
			}
			if (handlePoint(p, 1)) {
				if (p.x <= p_prev.x) {
					p.x = p_prev.x + 0.001f;
				}
				if (point_idx < points_count - 2 && p.x >= pointNext.mRight.x) {// check order
					p.x = pointNext.mRight.x - 0.001f;
				}
				if ((flags & (int)CurveEditorFlags::NO_MOVE_X) == (int)CurveEditorFlags::NO_MOVE_X) {
					pointNext.mLeft.y = p.y;
				}
				else if ((flags & (int)CurveEditorFlags::NO_MOVE_Y) == (int)CurveEditorFlags::NO_MOVE_Y) {
					pointNext.mLeft.x = p.x;
				}
				else {
					pointNext.mLeft = IKIGAI::MATH::Vector2f(p.x, p.y);
				}
				changed_idx = point_idx + 1;
			}

		} else {
			window->DrawList->AddLine(transform(p_prev), transform(p), GetColorU32(ImGuiCol_PlotLines), 1.0f);

			if ((flags & (int)CurveEditorFlags::WITH_OFFSETS) == (int)CurveEditorFlags::WITH_OFFSETS) {
				if (point.mLeftYOffset.y > 0 || pointNext.mLeftYOffset.y > 0)
				window->DrawList->AddLine(transform(p_prev + ImVec2(0.0f, point.mLeftYOffset.y)), transform(p + ImVec2(0.0f, pointNext.mLeftYOffset.y)), GetColorU32(ImGuiCol_PlotHistogram), 1.0f);
				if (point.mLeftYOffset.x < 0 || pointNext.mLeftYOffset.x < 0)
				window->DrawList->AddLine(transform(p_prev + ImVec2(0.0f, point.mLeftYOffset.x)), transform(p + ImVec2(0.0f, pointNext.mLeftYOffset.x)), GetColorU32(ImGuiCol_PlotHistogram), 1.0f);
				//TODO:: cntrl for move both points
				const float drawOffset = 20.0f;
				ImVec2 pOffsetPlus = p;
				pOffsetPlus.y += pointNext.mLeftYOffset.y;
				if (handlePoint(pOffsetPlus, 1000, -drawOffset, GetColorU32(ImGuiCol_PlotHistogramHovered), GetColorU32(ImGuiCol_PlotHistogramHovered))) {
					pointNext.mLeftYOffset.y = std::max(0.0f, pOffsetPlus.y - p.y);
				}

				ImVec2 pOffsetMinus = p;
				pOffsetMinus.y += pointNext.mLeftYOffset.x;
				if (handlePoint(pOffsetMinus, -1000, drawOffset, GetColorU32(ImGuiCol_PlotHistogramHovered), GetColorU32(ImGuiCol_PlotHistogramHovered))) {
					pointNext.mLeftYOffset.x = std::min(0.0f, pOffsetMinus.y - p.y);
				}
			}

			if (handlePoint(p, 1)) {
				if (p.x <= p_prev.x) p.x = p_prev.x + 0.001f;
				if (point_idx < points_count - 2 && p.x >= point.mLeft.x) {
					p.x = point.mLeft.x - 0.001f;
				}

				if ((flags & (int)CurveEditorFlags::NO_MOVE_X) == (int)CurveEditorFlags::NO_MOVE_X) {
					pointNext.mLeft.y = p.y;
				} else if ((flags & (int)CurveEditorFlags::NO_MOVE_Y) == (int)CurveEditorFlags::NO_MOVE_Y) {
					pointNext.mLeft.x = p.x;
				} else {
					pointNext.mLeft = IKIGAI::MATH::Vector2f(p.x, p.y);
				}
				changed_idx = point_idx + 1;
			}
		}
		if (point_idx == 0) {
			if ((flags & (int)CurveEditorFlags::WITH_OFFSETS) == (int)CurveEditorFlags::WITH_OFFSETS) {
				//TODO:: cntrl for move both points
				const float drawOffset = 20.0f;
				ImVec2 pOffsetPlus = p_prev;
				pOffsetPlus.y += point.mLeftYOffset.y;
				if (handlePoint(pOffsetPlus, 999, -drawOffset, GetColorU32(ImGuiCol_PlotHistogramHovered), GetColorU32(ImGuiCol_PlotHistogramHovered))) {
					point.mLeftYOffset.y = std::max(0.0f, pOffsetPlus.y - p_prev.y);
				}

				ImVec2 pOffsetMinus = p_prev;
				pOffsetMinus.y += point.mLeftYOffset.x;
				if (handlePoint(pOffsetMinus, -999, drawOffset, GetColorU32(ImGuiCol_PlotHistogramHovered), GetColorU32(ImGuiCol_PlotHistogramHovered))) {
					point.mLeftYOffset.x = std::min(0.0f, pOffsetMinus.y - p_prev.y);
				}
			}
			if (handlePoint(p_prev, 0)) {
				if (p.x <= p_prev.x) {
					p_prev.x = p.x - 0.001f;
				}
				
				if ((flags & (int)CurveEditorFlags::NO_MOVE_X) == (int)CurveEditorFlags::NO_MOVE_X) {
					point.mLeft.y = p_prev.y;
				} else if ((flags & (int)CurveEditorFlags::NO_MOVE_Y) == (int)CurveEditorFlags::NO_MOVE_Y) {
					point.mLeft.x = p_prev.x;
				} else {
					point.mLeft = IKIGAI::MATH::Vector2f(p_prev.x, p_prev.y);
				}
				changed_idx = point_idx;
			}
		}
		PopID();
	}

	SetCursorScreenPos(inner_bb.Min);

	InvisibleButton("bg", inner_bb.Max - inner_bb.Min);

	if (IsItemActive() && IsMouseDoubleClicked(0) && new_count) {
		ImVec2 mp = GetMousePos();
		ImVec2 new_p = invTransform(mp);

		if ((flags & (int)CurveEditorFlags::NO_TANGENTS) == 0) {
			values.push_back({
				IKIGAI::MATH::Vector2f(new_p.x, new_p.y),
				IKIGAI::MATH::Vector2f(new_p.x, new_p.y),
				IKIGAI::MATH::Vector2f(-0.2f, 0),
				IKIGAI::MATH::Vector2f(0.2f, 0),
			});

			++* new_count;

			std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) {
				float fa = a.mLeft.x;
				float fb = b.mLeft.x;
				return fa < fb;
			});
			for (int i = 0; i < values.size()-1; i++) {
				values[i].mRight = values[i + 1].mLeft;
			}

		} else {
			values.push_back({
				IKIGAI::MATH::Vector2f(new_p.x, new_p.y),
				IKIGAI::MATH::Vector2f(),
				IKIGAI::MATH::Vector2f(),
				IKIGAI::MATH::Vector2f(),
			});
			++* new_count;
			std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) {
				float fa = a.mLeft.x;
				float fb = b.mLeft.x;
				return fa < fb;
			});
			for (int i = 0; i < values.size() - 1; i++) {
				values[i].mRight = values[i + 1].mLeft;
			}
		}
	}

	if (hovered_idx >= 0 && IsMouseDoubleClicked(0) && new_count && points_count > 2) {
		--* new_count;
		values.erase(values.begin() + hovered_idx);
	}

	if (hovered_point) *hovered_point = hovered_idx;

	EndChild();
	RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, inner_bb.Min.y), label);
	return changed_idx;
}




int SplineEditorExt(const char* label, tinyspline::BSpline& spline, std::vector<tinyspline::real>& data, float detailStep,
	const ImVec2& editor_size, ImU32 flags, int* new_count, int* selected_point, int* hovered_point) {

	enum class StorageValues : ImGuiID {
		FROM_X = 100,
		FROM_Y,
		WIDTH,
		HEIGHT,
		IS_PANNING,
		POINT_START_X,
		POINT_START_Y,
	};

	const float HEIGHT = 100;
	const float SCALE_FACTOR = 1.1f;
	const float EXTRA_OFFSET = 0.5f;
	static ImVec2 start_pan;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	ImVec2 size = editor_size;
	size.x = size.x < 0 ? CalcItemWidth() + (style.FramePadding.x * 2) : size.x;
	size.y = size.y < 0 ? HEIGHT : size.y;
	if (hovered_point) {
		*hovered_point = -1;
	}
	ImGuiWindow* parent_window = GetCurrentWindow();
	ImGuiID id = parent_window->GetID(label);

	const auto points_count = data.size() / 2;

	if (new_count) {
		*new_count = points_count;
	}

	if (!BeginChild(id, size, ImGuiChildFlags_FrameStyle, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		EndChild();
		return -1;
	}

	int hovered_idx = -1;

	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems) {
		EndChild();
		return -1;
	}

	ImVec2 points_min(FLT_MAX, FLT_MAX);
	ImVec2 points_max(-FLT_MAX, -FLT_MAX);
	for (int point_idx = 0; point_idx < points_count; ++point_idx) {
		if (spline.numControlPoints() <= point_idx) {
			continue;
		}
		auto _p = spline.controlPointVec2At(point_idx);
		ImVec2 point = ImVec2(_p.x(), _p.y());

		points_max = ImMax(points_max, point);
		points_min = ImMin(points_min, point);
	}
	points_max.y = ImMax(points_max.y, points_min.y + 0.0001f);


	if (flags & (int)CurveEditorFlags::RESET) window->StateStorage.Clear();

	float from_x = window->StateStorage.GetFloat((ImGuiID)StorageValues::FROM_X, points_min.x);
	float from_y = window->StateStorage.GetFloat((ImGuiID)StorageValues::FROM_Y, points_min.y);
	float width = window->StateStorage.GetFloat((ImGuiID)StorageValues::WIDTH, points_max.x - points_min.x + 10.0f);
	float height = window->StateStorage.GetFloat((ImGuiID)StorageValues::HEIGHT, points_max.y - points_min.y + 10.0f);

	window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_X, from_x);
	window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_Y, from_y);
	window->StateStorage.SetFloat((ImGuiID)StorageValues::WIDTH, width);
	window->StateStorage.SetFloat((ImGuiID)StorageValues::HEIGHT, height);

	const ImRect inner_bb = window->InnerClipRect;
	if (inner_bb.GetWidth() == 0 || inner_bb.GetHeight() == 0) {
		EndChild();
		return -1;
	}
	const ImRect frame_bb(inner_bb.Min - style.FramePadding, inner_bb.Max + style.FramePadding);

	auto transform = [&](const ImVec2& pos) -> ImVec2 {
		float x = (pos.x - from_x) / width;
		float y = (pos.y - from_y) / height;

		return ImVec2(
			inner_bb.Min.x * (1 - x) + inner_bb.Max.x * x,
			inner_bb.Min.y * y + inner_bb.Max.y * (1 - y)
		);
	};

	auto invTransform = [&](const ImVec2& pos) -> ImVec2 {
		float x = (pos.x - inner_bb.Min.x) / (inner_bb.Max.x - inner_bb.Min.x);
		float y = (inner_bb.Max.y - pos.y) / (inner_bb.Max.y - inner_bb.Min.y);

		return ImVec2(
			from_x + width * x,
			from_y + height * y
		);
	};

	if (flags & (int)CurveEditorFlags::SHOW_GRID) {//draw grid
		int exp;
		frexp(width / 5, &exp);
		float step_x = (float)ldexp(1.0, exp);
		int cell_cols = int(width / step_x);

		float x = step_x * int(from_x / step_x);
		for (int i = -1; i < cell_cols + 2; ++i) {
			ImVec2 a = transform({x + i * step_x, from_y});
			ImVec2 b = transform({x + i * step_x, from_y + height});
			window->DrawList->AddLine(a, b, 0x55000000);
			char buf[64];
			if (exp > 0) {
				ImFormatString(buf, sizeof(buf), " %d", int(x + i * step_x));
			} else {
				ImFormatString(buf, sizeof(buf), " %.2f", x + i * step_x);
			}
			window->DrawList->AddText(b, 0x55000000, buf);
		}

		frexp(height / 5, &exp);
		float step_y = (float)ldexp(1.0, exp);
		int cell_rows = int(height / step_y);

		float y = step_y * int(from_y / step_y);
		for (int i = -1; i < cell_rows + 2; ++i) {
			ImVec2 a = transform({from_x, y + i * step_y});
			ImVec2 b = transform({from_x + width, y + i * step_y});
			window->DrawList->AddLine(a, b, 0x55000000);
			char buf[64];
			if (exp > 0) {
				ImFormatString(buf, sizeof(buf), " %d", int(y + i * step_y));
			} else {
				ImFormatString(buf, sizeof(buf), " %.2f", y + i * step_y);
			}
			window->DrawList->AddText(a, 0x55000000, buf);
		}
	}

	const ImGuiID dragger_id = GetID("##_node_dragger");
	ImGui::ItemAdd(inner_bb, dragger_id);

	if (GetIO().MouseWheel != 0 && IsItemHovered()) {
		float scale = powf(SCALE_FACTOR, -GetIO().MouseWheel);
		width *= scale;
		height *= scale;
		window->StateStorage.SetFloat((ImGuiID)StorageValues::WIDTH, width);
		window->StateStorage.SetFloat((ImGuiID)StorageValues::HEIGHT, height);
	}
	if (IsMouseReleased(2)) {
		window->StateStorage.SetBool((ImGuiID)StorageValues::IS_PANNING, false);
	}
	if (window->StateStorage.GetBool((ImGuiID)StorageValues::IS_PANNING, false)) {
		ImVec2 drag_offset = GetMouseDragDelta(2);
		from_x = start_pan.x;
		from_y = start_pan.y;
		from_x -= drag_offset.x * width / (inner_bb.Max.x - inner_bb.Min.x);
		from_y += drag_offset.y * height / (inner_bb.Max.y - inner_bb.Min.y);
		window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_X, from_x);
		window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_Y, from_y);
	} else if (IsMouseDragging(2) && IsItemHovered()) {
		window->StateStorage.SetBool((ImGuiID)StorageValues::IS_PANNING, true);
		start_pan.x = from_x;
		start_pan.y = from_y;
	}

	int changed_idx = -1;
	for (int point_idx = points_count - 1; point_idx >= 0; --point_idx) {
		if (spline.numControlPoints() <= point_idx) {
			continue;
		}
		auto _p = spline.controlPointVec2At(point_idx);
		ImVec2 point = ImVec2(_p.x(), _p.y());

		auto handlePoint = [&](ImVec2& p, int idx, float drawOffset = 0.0f, ImU32 color = GetColorU32(ImGuiCol_PlotLines), ImU32 colorHover = GetColorU32(ImGuiCol_PlotLinesHovered)) -> bool {
			static const float SIZE = 3;

			ImVec2 cursor_pos = GetCursorScreenPos();
			ImVec2 pos = transform(p);

			pos.y += drawOffset;

			SetCursorScreenPos(pos - ImVec2(SIZE, SIZE));
			PushID(idx + 1);
			InvisibleButton("", ImVec2(2 * HANDLE_RADIUS, 2 * HANDLE_RADIUS));

			bool is_selected = selected_point && *selected_point == point_idx + idx;
			float thickness = is_selected ? 2.0f : 1.0f;
			ImU32 col = IsItemActive() || IsItemHovered() ? colorHover : color;

			window->DrawList->AddLine(pos + ImVec2(-SIZE, 0), pos + ImVec2(0, SIZE), col, thickness);
			window->DrawList->AddLine(pos + ImVec2(SIZE, 0), pos + ImVec2(0, SIZE), col, thickness);
			window->DrawList->AddLine(pos + ImVec2(SIZE, 0), pos + ImVec2(0, -SIZE), col, thickness);
			window->DrawList->AddLine(pos + ImVec2(-SIZE, 0), pos + ImVec2(0, -SIZE), col, thickness);

			if (IsItemHovered()) hovered_idx = point_idx + idx;

			bool changed = false;
			if (IsItemActive() && IsMouseClicked(0)) {
				if (selected_point) *selected_point = point_idx + idx;
				window->StateStorage.SetFloat((ImGuiID)StorageValues::POINT_START_X, pos.x);
				window->StateStorage.SetFloat((ImGuiID)StorageValues::POINT_START_Y, pos.y);
			}

			if (IsItemHovered() || (IsItemActive() && IsMouseDragging(0))) {
				char tmp[64];
				ImFormatString(tmp, sizeof(tmp), "%0.2f, %0.2f", p.x, p.y);
				window->DrawList->AddText({pos.x, pos.y - GetTextLineHeight()}, 0xff000000, tmp);
			}

			if (IsItemActive() && IsMouseDragging(0)) {
				pos.x = window->StateStorage.GetFloat((ImGuiID)StorageValues::POINT_START_X, pos.x);
				pos.y = window->StateStorage.GetFloat((ImGuiID)StorageValues::POINT_START_Y, pos.y);
				pos += GetMouseDragDelta();
				pos.y -= drawOffset;
				ImVec2 v = invTransform(pos);

				p = v;
				changed = true;
			}
			PopID();

			SetCursorScreenPos(cursor_pos);
			return changed;
		};

		PushID(point_idx + 1);


		if (handlePoint(point, point_idx)) {
			data[point_idx * 2] = point.x;
			data[point_idx * 2 + 1] = point.y;
			if (data.size() / 2 > 3) {
				spline = tinyspline::BSpline(data.size() / 2);
				spline.setControlPoints(data);
			}
		}

		PopID();
	}

	if (spline.numControlPoints() > 0) {
		for (float i = 0.0f; i < 1.0f; i += detailStep) {
			if (i + detailStep >= 1.0f) {
				break;
			}
			std::vector<tinyspline::real> p0 = spline.eval(i).result();
			std::vector<tinyspline::real> p1 = spline.eval(i + detailStep).result();

			ImVec2 pos0 = transform({static_cast<float>(p0[0]), static_cast<float>(p0[1])});
			ImVec2 pos1 = transform({static_cast<float>(p1[0]), static_cast<float>(p1[1])});

			window->DrawList->AddLine(pos0, pos1, GetColorU32(ImGuiCol_PlotLinesHovered), 1.0f);
		}
	}

	SetCursorScreenPos(inner_bb.Min);

	InvisibleButton("bg", inner_bb.Max - inner_bb.Min);

	if (IsItemActive() && IsMouseDoubleClicked(0) && new_count) {
		ImVec2 mp = GetMousePos();
		ImVec2 new_p = invTransform(mp);
		
		data.push_back(new_p.x);
		data.push_back(new_p.y);

		if (data.size() / 2  > 3) {
			spline = tinyspline::BSpline(data.size() / 2);
			spline.setControlPoints(data);
		}

		++* new_count;
	}

	if (hovered_idx >= 0 && IsMouseDoubleClicked(0) && new_count && points_count > 2) {
		--* new_count;
		data.erase(data.begin() + hovered_idx);
		data.erase(data.begin() + hovered_idx);

		if (data.size() / 2 > 3) {
			spline = tinyspline::BSpline(data.size() / 2);
			spline.setControlPoints(data);
		}
	}

	if (hovered_point) *hovered_point = hovered_idx;

	EndChild();
	RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, inner_bb.Min.y), label);
	return changed_idx;
}




/*
virtual void RemoveInterval(int eId, int iId) override {
	if (eId < 0 || iId < 0) {
		return;
	}

	std::map<std::string, ANIMATION::PropType> prop;
	//for (auto& e : myItems[eId].intervalsValues[iId]) {
	auto& e = myItems[eId].intervalsValues[iId];
	prop[std::to_string(static_cast<int>(myItems[eId].info.id)) + e.first] = e.second;
	//}
	animation->delKeyFrameProp(myItems[eId].intervals[iId].first, prop);

	myItems[eId].intervals.erase(myItems[eId].intervals.begin() + iId);
	myItems[eId].intervalsValues.erase(myItems[eId].intervalsValues.begin() + iId);
}

virtual void AddInterval(int eId, int frame) override {
	if (eId < 0 || frame < 0) {
		return;
	}
	myItems[eId].intervals.push_back(std::make_pair(frame, frame));
	myItems[eId].intervalsValues.push_back(std::make_pair(myItems[eId].info.propName, myItems[eId].info.value));


	std::map<std::string, ANIMATION::PropType> prop;
	auto& e = myItems[eId].intervalsValues.back();
	//for (auto& e : myItems[eId].intervalsValues.back()) {

	prop[std::to_string(static_cast<int>(myItems[eId].info.id)) + e.first] = e.second;
	//}
	animation->delKeyFrameProp(myItems[eId].intervals.back().first, prop);
}

virtual void Add(int type) {
		auto& data = SequencerItemTypeNames[type];

		static int lineId = 0;
		myItems.push_back(MySequenceItem{ data, lineId++, {}, false, {} });

		//if (data.componentName == "Transform") {
			//addWrappersForComponent(*animation, selectObj->getComponent<ECS::TransformComponent>().value().get());
		//}
		std::visit(
			[&data, this](auto& arg) {
				addWrappersForComponent(*animation, data, *selectObj->getComponent<std::remove_reference_t<decltype(*arg)>>().get());
			},
			data.component);
	};

	//TODO: del from anim
	virtual void Del(int index) { myItems.erase(myItems.begin() + index); }

*/
IKIGAI::EDITOR::TimelineAnimationWindow::TimelineAnimationWindow() {
	mAnimation = std::make_unique<IKIGAI::ANIMATION::Animation>(100, 60, false);
}

void IKIGAI::EDITOR::TimelineAnimationWindow::openResource(const std::string& path) {

}

////////
///

struct Prop {
	Prop() = default;
	Prop(std::string objName,
		IKIGAI::ECS::Object::Id objId,
		std::string componentName,
		std::string propertyName,
		IKIGAI::ANIMATION::AnimationProperty animProp,
		IKIGAI::ANIMATION::PropType defaultValue):objName(objName), objId(objId), componentName(componentName), propertyName(propertyName), animProp(animProp), defaultValue(defaultValue){}

	std::string objName;
	IKIGAI::ECS::Object::Id objId{IKIGAI::ECS2::Entity::ID(0)};
	std::string componentName;
	std::string propertyName;
	IKIGAI::ANIMATION::AnimationProperty animProp;
	IKIGAI::ANIMATION::PropType defaultValue;
};

std::map<std::string, Prop> AllObjPropsInAnimation;

std::map<std::string, Prop> ObjProps;

template<typename T>
void getPropsImpl(const std::string& name, const IKIGAI::ECS::Object::Id& id, IKIGAI::UTILS::WeakPtr<IKIGAI::ECS::ComponentBase> comp) {
	if (comp->getName() == IKIGAI::ECS::GetComponentName<T>()) {
		auto cm = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::ECS2::World>().getComponentManager();
		auto props = T::GetMembers();
		std::apply([&comp, &name, &id, cm]<typename... Args> (Args&... tpl) {
			auto drawElem = [&comp, &name, &id, cm](auto& prop) {
				const auto propName = prop.getName();
				const auto wType = std::get<IKIGAI::UTILS::WidgetType>(prop.getMetadata().at(IKIGAI::UTILS::MetaParam::EDIT_WIDGET));
				const auto lineName = name + comp->getName() + propName;
				switch (wType) {
				case IKIGAI::UTILS::WidgetType::DRAG_FLOAT_4: {
					if constexpr (std::is_same_v<decltype(prop.getPropType()), IKIGAI::MATH::Vector4f>) {
						ObjProps[lineName, cm] = Prop{
							name, id, comp->getName(), propName,
							IKIGAI::ANIMATION::AnimationProperty(lineName,
							[prop, id, cm](IKIGAI::ANIMATION::PropType val) {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								prop.set(*component.get(), std::get<IKIGAI::MATH::Vector4f>(val));
							},
							[prop, id, cm]() {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								return prop.get(*component.get());
							}),
							prop.getPropType()};
					}
					break;
				}
				case IKIGAI::UTILS::WidgetType::DRAG_COLOR_4: {
					break;
				}
				case IKIGAI::UTILS::WidgetType::DRAG_FLOAT_3: {
					if constexpr (std::is_same_v<decltype(prop.getPropType()), IKIGAI::MATH::Vector3f>) {
						ObjProps[lineName] = Prop{
							name, id, comp->getName(), propName,
							IKIGAI::ANIMATION::AnimationProperty(lineName,
							[prop, id, cm](IKIGAI::ANIMATION::PropType val) {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								prop.set(*component.get(), std::get<IKIGAI::MATH::Vector3f>(val));
							},
							[prop, id, cm]() {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								return prop.get(*component.get());
							}),
							prop.getPropType()};
					}
					break;
				}
				case IKIGAI::UTILS::WidgetType::DRAG_COLOR_3: {
					//widgetColor3(static_cast<T*>(comp.get()), prop);
					break;
				}
				case IKIGAI::UTILS::WidgetType::DRAG_FLOAT: {
					if constexpr (std::is_same_v<decltype(prop.getPropType()), float>) {
						ObjProps[lineName] = Prop{
							name, id, comp->getName(), propName,
							IKIGAI::ANIMATION::AnimationProperty(lineName,
							[prop, id, cm](IKIGAI::ANIMATION::PropType val) {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								prop.set(*component.get(), std::get<float>(val));
							},
							[prop, id, cm]() {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								return prop.get(*component.get());
							}),
							prop.getPropType()};
					}
					break;
				}
				case IKIGAI::UTILS::WidgetType::DRAG_INT: {
					if constexpr (std::is_same_v<decltype(prop.getPropType()), int>) {
						ObjProps[lineName] = Prop{
							name, id, comp->getName(), propName,
							IKIGAI::ANIMATION::AnimationProperty(lineName,
							[prop, id, cm](IKIGAI::ANIMATION::PropType val) {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								prop.set(*component.get(), std::get<int>(val));
							},
							[prop, id, cm]() {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								return prop.get(*component.get());
							}),
							prop.getPropType()};
					}
					break;
				}
				case IKIGAI::UTILS::WidgetType::BOOL: {
					if constexpr (std::is_same_v<decltype(prop.getPropType()), bool>) {
						ObjProps[lineName] = Prop{
							name, id, comp->getName(), propName,
							IKIGAI::ANIMATION::AnimationProperty(lineName,
							[prop, id, cm](IKIGAI::ANIMATION::PropType val) {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								prop.set(*component.get(), std::get<bool>(val));
							},
							[prop, id, cm]() {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								return prop.get(*component.get());
							}),
							prop.getPropType()};
					}
					break;
				}
				case IKIGAI::UTILS::WidgetType::STRING: {
					//widgetString(static_cast<T*>(comp.get()), prop);
					break;
				}
				case IKIGAI::UTILS::WidgetType::COMBO: {
					//widgetCombo(static_cast<T*>(comp.get()), prop);
					break;
				}
				}
			};
			(drawElem(tpl), ...);
		}, props);
	}
}

template<template<typename...> class Container, typename...ComponentType>
void getProps(const std::string& name, const IKIGAI::ECS::Object::Id& id, IKIGAI::UTILS::WeakPtr<IKIGAI::ECS::ComponentBase> comp, Container<ComponentType...> opt) {
	(getPropsImpl<ComponentType>(name, id, comp), ...);
}

void getProps(const std::string& name, const IKIGAI::ECS::Object::Id& id, IKIGAI::UTILS::WeakPtr<IKIGAI::ECS::ComponentBase> comp) {
	getProps(name, id, comp, IKIGAI::ECS::ComponentsTypeProviderType{});
}
////////////


template<typename T>
void getPropsImpl(const std::string& name, const IKIGAI::ECS::Object::Id& id, const std::string& componentName, const std::string& propName, Prop& prop) {
	if (componentName == IKIGAI::ECS::GetComponentName<T>()) {
		auto props = T::GetMembers();

		auto& scene = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>().getCurrentScene();
		auto obj = scene.findObjectByID(id);

		if (!obj) {
			//problem
			return;
		}

		auto comp = obj->getComponent<T>();
		auto cm = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::ECS2::World>().getComponentManager();

		std::apply([&comp, &name, &id, &propName, &prop, cm]<typename... Args> (Args&... tpl) {
			auto drawElem = [&comp, &name, &id, &propName, &prop, cm](auto& _prop) {

				const auto _propName = _prop.getName();
				if (propName != _propName) {
					return;
				}

				const auto wType = std::get<IKIGAI::UTILS::WidgetType>(_prop.getMetadata().at(IKIGAI::UTILS::MetaParam::EDIT_WIDGET));
				const auto lineName = name + comp->getName() + propName;
				switch (wType) {
				case IKIGAI::UTILS::WidgetType::DRAG_FLOAT_4: {
					if constexpr (std::is_same_v<decltype(_prop.getPropType()), IKIGAI::MATH::Vector4f>) {
						prop = Prop{
							name, id, comp->getName(), propName,
							IKIGAI::ANIMATION::AnimationProperty(lineName,
							[_prop, id, cm](IKIGAI::ANIMATION::PropType val) {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								_prop.set(*component.get(), std::get<IKIGAI::MATH::Vector4f>(val));
							},
							[_prop, id, cm]() {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								return _prop.get(*component.get());
							}, IKIGAI::ANIMATION::InterpolationType::CUSTOM),
							_prop.getPropType()};
					}
					break;
				}
				case IKIGAI::UTILS::WidgetType::DRAG_COLOR_4: {
					break;
				}
				case IKIGAI::UTILS::WidgetType::DRAG_FLOAT_3: {
					if constexpr (std::is_same_v<decltype(_prop.getPropType()), IKIGAI::MATH::Vector3f>) {
						prop = Prop{
							name, id, comp->getName(), propName,
							IKIGAI::ANIMATION::AnimationProperty(lineName,
							[_prop, id, cm](IKIGAI::ANIMATION::PropType val) {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								_prop.set(*component.get(), std::get<IKIGAI::MATH::Vector3f>(val));
							},
							[_prop, id, cm]() {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								return _prop.get(*component.get());
							}, IKIGAI::ANIMATION::InterpolationType::CUSTOM),
							_prop.getPropType()};
					}
					break;
				}
				case IKIGAI::UTILS::WidgetType::DRAG_COLOR_3: {
					//widgetColor3(static_cast<T*>(comp.get()), prop);
					break;
				}
				case IKIGAI::UTILS::WidgetType::DRAG_FLOAT: {
					if constexpr (std::is_same_v<decltype(_prop.getPropType()), float>) {
						prop = Prop{
							name, id, comp->getName(), propName,
							IKIGAI::ANIMATION::AnimationProperty(lineName,
							[_prop, id, cm](IKIGAI::ANIMATION::PropType val) {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								_prop.set(*component.get(), std::get<float>(val));
							},
							[_prop, id, cm]() {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								return _prop.get(*component.get());
							}, IKIGAI::ANIMATION::InterpolationType::CUSTOM),
							_prop.getPropType()};
					}
					break;
				}
				case IKIGAI::UTILS::WidgetType::DRAG_INT: {
					if constexpr (std::is_same_v<decltype(_prop.getPropType()), int>) {
						prop = Prop{
							name, id, comp->getName(), propName,
							IKIGAI::ANIMATION::AnimationProperty(lineName,
							[_prop, id, cm](IKIGAI::ANIMATION::PropType val) {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								_prop.set(*component.get(), std::get<int>(val));
							},
							[_prop, id, cm]() {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								return _prop.get(*component.get());
							}, IKIGAI::ANIMATION::InterpolationType::CUSTOM),
							_prop.getPropType()};
					}
					break;
				}
				case IKIGAI::UTILS::WidgetType::BOOL: {
					if constexpr (std::is_same_v<decltype(_prop.getPropType()), bool>) {
						prop = Prop{
							name, id, comp->getName(), propName,
							IKIGAI::ANIMATION::AnimationProperty(lineName,
							[_prop, id, cm](IKIGAI::ANIMATION::PropType val) {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								_prop.set(*component.get(), std::get<bool>(val));
							},
							[_prop, id, cm]() {
								auto component = cm->getComponent<T>(id);
								//auto component = IKIGAI::ECS::ComponentManager::GetInstance().getComponent<T>(id);
								return _prop.get(*component.get());
							}, IKIGAI::ANIMATION::InterpolationType::CUSTOM),
							_prop.getPropType()};
					}
					break;
				}
				case IKIGAI::UTILS::WidgetType::STRING: {
					//widgetString(static_cast<T*>(comp.get()), prop);
					break;
				}
				case IKIGAI::UTILS::WidgetType::COMBO: {
					//widgetCombo(static_cast<T*>(comp.get()), prop);
					break;
				}
				}
			};
			(drawElem(tpl), ...);
		}, props);
	}
}

template<template<typename...> class Container, typename...ComponentType>
void getProp(const std::string& name, const IKIGAI::ECS::Object::Id& id, const std::string& componentName, const std::string& propName, Prop& prop, Container<ComponentType...> opt) {
	(getPropsImpl<ComponentType>(name, id, componentName, propName, prop), ...);
}

void getProp(const std::string& name, const IKIGAI::ECS::Object::Id& id, const std::string& componentName, const std::string& propName, Prop& prop) {
	getProp(name, id, componentName, propName, prop, IKIGAI::ECS::ComponentsTypeProviderType{});
}








std::shared_ptr<IKIGAI::ECS::Object> selectObject;
void getObjectProps() {
	if (!IKIGAI::EDITOR::EditorRender::GlobalState.mSelectObject) {
		selectObject = nullptr;
		return;
	}
	if (selectObject == IKIGAI::EDITOR::EditorRender::GlobalState.mSelectObject) {
		return;
	}
	selectObject = IKIGAI::EDITOR::EditorRender::GlobalState.mSelectObject;
	ObjProps.clear();


	const auto objId = selectObject->getID();
	const auto objName = selectObject->getName();

	auto cm = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::ECS2::World>().getComponentManager();
	auto components = cm->getComponents<IKIGAI::ECS::ComponentBase>(selectObject->getID());
	//auto components = IKIGAI::ECS::ComponentManager::GetInstance().getComponents(selectObject->getID());
	for (auto& component : components) {
		getProps(objName, objId, component);
	}

	Prop eventProp("", IKIGAI::ECS::Object::Id(IKIGAI::ECS::Object::Id::ID(0)), "EVENT", "",
		IKIGAI::ANIMATION::AnimationProperty(objName + "EVENT",
		[](IKIGAI::ANIMATION::PropType val) {
			std::cout << "SEND EVENT" << std::get<std::string>(val) << std::endl;
		},
		[]() {
			return "";
		}),
		std::string());

	ObjProps[objName + "EVENT"] = eventProp;

}

IKIGAI::ANIMATION::TimelineAnimationDescriptor IKIGAI::EDITOR::TimelineAnimationWindow::toDescriptor() {
	IKIGAI::ANIMATION::TimelineAnimationDescriptor desc;

	desc.isLooped = mAnimation->getLooped();
	desc.framesCount = mAnimation->getFrameCount();
	desc.FPS = mAnimation->getFPS();

	for (auto& track : AllObjPropsInAnimation) {
		desc.tracks[track.first] = IKIGAI::ANIMATION::TimelineAnimationTrackDescriptor{
			track.second.objName,
			static_cast<int>(track.second.objId.getUniqueId()),
			track.second.componentName,
			track.second.propertyName,
		};
	}

	for (auto frames : mAnimation->fastAccess) {
		for (auto frame : frames.second) {
			desc.trackFrames[frames.first].push_back(ANIMATION::TimelineAnimationFrameDescriptor{
				frame,
				mAnimation->frames[frame][frames.first],
				ANIMATION::InterpolationType::CUSTOM, //TODO add this to any frame and remove from track
				mAnimation->curves[frames.first][frame]
			});
		}
	}

	return desc;
}


std::unique_ptr<IKIGAI::ANIMATION::Animation> IKIGAI::EDITOR::TimelineAnimationWindow::fromDescriptor(IKIGAI::ANIMATION::TimelineAnimationDescriptor& desc) {
	mAnimation = std::make_unique<ANIMATION::Animation>(static_cast<unsigned int>(desc.framesCount), static_cast<unsigned int>(desc.FPS), desc.isLooped);

	for (auto& track : desc.tracks) {
		Prop prop;
		getProp(track.second.objectName, IKIGAI::ECS::Object::Id(IKIGAI::ECS::Object::Id::ID(track.second.objectId)), track.second.componentName, track.second.propertyName, prop);

		mAnimation->addProperty(ObjProps[track.first].animProp);
		AllObjPropsInAnimation[track.first] = prop;
	}

	for (auto frames : desc.trackFrames) {
		for (auto frame : frames.second) {
			if (!mAnimation->hasFrameForProperty(frame.frame, frames.first)) {
				std::map<std::string, ANIMATION::PropType> prop;
				prop[frames.first] = frame.value;
				mAnimation->addKeyFrameMerge(frame.frame, prop);

				mAnimation->curves[frames.first][frame.frame] = frame.curve;
			}
		}
	}

	return std::move(mAnimation);
}

//TODO: save to file move to utils function

void IKIGAI::EDITOR::TimelineAnimationWindow::draw() {
	if (!mAnimation) {
		return;
	}

	//if (CurveEditorExt("##curve2", values2_, {ImGui::GetContentRegionAvail().x, 150}, flags, 10.0f, &new_count, nullptr, &hovered_point,
	//	{0, 1}, {-2, 2}
		//	)) {
		//	coutn = new_count;
		//}
	ImGui::Begin("Timeline Animation");
	ImGui::PushItemWidth(130);
	int maxFrame = mAnimation->getFrameCount();
	if (ImGui::DragInt("Frame Count", &maxFrame)) {
		mAnimation->setFrameCount(maxFrame);
	}
	ImGui::SameLine();

	int fps = mAnimation->getFPS();
	if (ImGui::DragInt("FPS", &maxFrame)) {
		mAnimation->setFPS(fps);
	}
	ImGui::SameLine();

	bool isLooped = mAnimation->getLooped();
	if (ImGui::Checkbox("Loop", &isLooped)) {
		mAnimation->setLooped(isLooped);
	}
	ImGui::SameLine();


	static bool isCurve = false;
	if (ImGui::Checkbox("Curve", &isCurve)) {
		
	}
	ImGui::SameLine();

	if (ImGui::Button("Play")) {
		mAnimation->stop();
		mAnimation->play();
	}
	ImGui::SameLine();

	if (ImGui::Button("Save")) {
		ImGui::OpenPopup("Save new timeline animation");
	}
	ImGui::SameLine();

	if (ImGui::Button("New")) {
		mAnimation = std::make_unique<ANIMATION::Animation>(100, 60, false);
	}

	// Always center this window when appearing
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Save new timeline animation", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		static std::string name = "newAnimation";

		ImGui::Text("Enter name");
		ImGui::SameLine();
		ImGui::InputText("#animName", &name);

		ImGui::Separator();

		if (ImGui::Button("OK", ImVec2(120, 0))) {
			if (!name.empty()) {
				auto desc = toDescriptor();
				auto res = UTILS::ToJsonStr(desc);

				if (res.isErr()) {
					//LOG_ERROR << "Can not save: " << sceneFilePath;
				}

				auto jsonStr = res.unwrap();
				//write
				auto path = UTILS::GetRealPath("animations/") + name + ".trackanim";
				std::ofstream f(path);
				if (f.is_open()) {
					f << jsonStr << std::endl;
				}
				f.close();

				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}


	ImGui::PopItemWidth();
	getObjectProps();

	// Add line
	std::string comboLabel;
	int itemCurrentIndex = -1;
	std::string selectedPropName;
	if (ImGui::BeginCombo("Add", comboLabel.c_str(), ImGuiComboFlags_NoPreview)) {
		int n = 0;
		for (auto& obj : ObjProps) {
			const bool isSelected = (itemCurrentIndex == n);
			if (ImGui::Selectable(obj.first.c_str(), isSelected)) {
				itemCurrentIndex = n;
				selectedPropName = obj.first;
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
			n++;
		}
		ImGui::EndCombo();
	}
	if (itemCurrentIndex >= 0) {
		// Add line

		mAnimation->addProperty(ObjProps[selectedPropName].animProp);
		AllObjPropsInAnimation[selectedPropName] = ObjProps[selectedPropName];

		itemCurrentIndex = -1;
		selectedPropName.clear();
	}


	static std::optional<std::string> lineSelected;

	static std::optional<int> frameToEdit;
	static std::optional<std::string> lineToEdit;
	if (!isCurve) {
		static int32_t currentFrame = 0;
		static int32_t startFrame = 0;
		if (ImGui::BeginNeoSequencer("#Sequencer", &currentFrame, &startFrame, &maxFrame, {0, 0},
			ImGuiNeoSequencerFlags_EnableSelection |
			ImGuiNeoSequencerFlags_Selection_EnableDragging |
			ImGuiNeoSequencerFlags_Selection_EnableDeletion)) {

			// Yet group not support
			//if (ImGui::BeginNeoGroup("Transform", &transformOpen)) {
			//ImGui::EndNeoGroup();
			//}

			std::optional<std::string> lineToDelete;
			std::optional<int> frameToDelete;
			
			lineSelected = std::nullopt;
			auto& lines = mAnimation->props;
			for (auto& line : lines) {
				bool needDeleteLine = false;
				bool isSelected = false;
				if (ImGui::BeginNeoTimelineEx(line.first.c_str(), nullptr, ImGuiNeoTimelineFlags_None, &needDeleteLine, &isSelected)) {
					for (auto&& v : mAnimation->frames) {
						static std::map<std::string, int> frames;
						frames[line.first+std::to_string(v.first)] = v.first;
						//TODO: problem with moving frames
						if (v.second.contains(line.first)) { // Delete frame event
							int frameAction = ImGui::NeoKeyframe(&frames[line.first + std::to_string(v.first)]);

							if (frames[line.first + std::to_string(v.first)] != v.first) {
								mAnimation->moveFrameInTrack(line.first, v.first, frames[line.first + std::to_string(v.first)]);
							}

							if (frameAction == 1) {
								std::cout << "Delete frame: " << frames[line.first + std::to_string(v.first)] << " in line: " << line.first << std::endl;
								frameToDelete = frames[line.first + std::to_string(v.first)];
							}
							if (frameAction == 2) {
								// edit
								frameToEdit = frames[line.first + std::to_string(v.first)];
								lineToEdit = line.first;
							}
						}
					}

					if (frameToDelete) { // Delete frame from line
						mAnimation->delKeyFrameProp(frameToDelete.value(), line.first);
						frameToDelete = std::nullopt;
					}

					const auto addFrameData = GetAddFrame(); // Add frame to line
					if (addFrameData.y >= 0 && addFrameData.x >= 0) {
						std::cout << "Add frame" << addFrameData.x << " " << addFrameData.y << std::endl;

						if (!mAnimation->hasFrameForProperty(addFrameData.y, line.first)) {
							std::map<std::string, ANIMATION::PropType> prop;
							prop[line.first] = AllObjPropsInAnimation.at(line.first).defaultValue;
							mAnimation->addKeyFrameMerge(static_cast<int>(addFrameData.y), prop);
						}
					}

					if (needDeleteLine) { // Delete line event
						std::cout << "Delete line: " << line.first << std::endl;
						lineToDelete = line.first;
					}

					if (isSelected) {
						lineSelected = line.first;
					}
					
					ImGui::EndNeoTimeLine();
				}
			}

			if (lineToDelete) { // Delete line
				mAnimation->delProperty(lineToDelete.value());
				AllObjPropsInAnimation.erase(lineToDelete.value());
				lineToDelete = std::nullopt;
			}

			ImGui::EndNeoSequencer();
		}
	}
	else {
		static tinyspline::BSpline spline;
		static std::vector<tinyspline::real> data = {0.0, 0.0, 10.0, 10.0};
		static int hovered_point = -1;
		static int new_count = 0;
		int flags = (int)CurveEditorFlags::SHOW_GRID;
		if (SplineEditorExt("SPLINE", spline, data, 0.05f, {ImGui::GetContentRegionAvail().x, 250}, flags, &new_count, nullptr, &hovered_point)) {
		}

		//if (!lineSelected) {
		//	ImGui::Text("Track not selected");
		//}
		//else {
		//	if (!mAnimation->curves[lineSelected.value()].empty()) {
		//		int hovered_point = -1;
		//		int new_count = 0;
		//		int flags = (int)CurveEditorFlags::SHOW_GRID | (int)CurveEditorFlags::NO_MOVE_X | (int)CurveEditorFlags::NO_DELETE | (int)CurveEditorFlags::NO_ADD;
		//		float maxX = mAnimation->getFrameCount();
		//
		//		static std::vector<ANIMATION::Curve> values2_ = {
		//			ANIMATION::Curve({0, 0}, {1, 1}, {-1, 0}, {1, 0}),
		//			ANIMATION::Curve({1, 1}, {1, 1}, {-1, 0}, {1, 0})
		//		};
		//
		//		if (CurveEditorExt("curve", mAnimation->curves[lineSelected.value()],
		//			{ImGui::GetContentRegionAvail().x, 150},
		//			flags,
		//			10.0f,
		//			&new_count, nullptr, &hovered_point,
		//			{0, maxX}, {0, 1})) {
		//
		//
		//		}
		//	}
		//}
	}

	//Edit frame
	if (frameToEdit && lineToEdit) {
		if (mAnimation->frames.contains(frameToEdit.value())) {
			auto frame = frameToEdit.value();
			if (mAnimation->frames[frame].contains(lineToEdit.value())) {
				auto line = lineToEdit.value();

				auto& prop = mAnimation->frames[frame][line];

				std::visit([&line](auto&& arg) {
					using T = std::decay_t<decltype(arg)>;
					if constexpr (std::is_same_v<T, bool>) {
						if (ImGui::Checkbox(line.c_str(), &arg)) {
							
						}
					}
					else if constexpr (std::is_same_v<T, int>) {
						if (ImGui::DragInt(line.c_str(), &arg)) {

						}
					}
					else if constexpr (std::is_same_v<T, float>) {
						if (ImGui::DragFloat(line.c_str(), &arg)) {

						}
					}
					else if constexpr (std::is_same_v<T, MATH::Vector2f>) {
						std::array<float, 2> data = {arg.x, arg.y};
						if (ImGui::DragFloat2(line.c_str(), data.data())) {
							arg.x = data[0];
							arg.y = data[1];
						}
					}
					else if constexpr (std::is_same_v<T, MATH::Vector3f>) {
						std::array<float, 3> data = {arg.x, arg.y, arg.z};
						if (ImGui::DragFloat3(line.c_str(), data.data())) {
							arg.x = data[0];
							arg.y = data[1];
							arg.z = data[2];
						}
					}
					else if constexpr (std::is_same_v<T, MATH::Vector4f>) {
						std::array<float, 4> data = {arg.x, arg.y, arg.z, arg.w};
						if (ImGui::DragFloat4(line.c_str(), data.data())) {
							arg.x = data[0];
							arg.y = data[1];
							arg.z = data[2];
							arg.w = data[3];
						}
					}
					else if constexpr (std::is_same_v<T, std::string>) {
						if (ImGui::InputText(line.c_str(), &arg)) {

						}
					}
					}, prop);


				auto& curve = mAnimation->curves[line][frame];

				float v[5] = {curve.mLeftTangent.x, curve.mLeftTangent.y, curve.mRightTangent.x, curve.mRightTangent.y};
				if (Bezier("Curve", v)) {
					curve.mLeftTangent = {v[0], v[1]};
					curve.mRightTangent = {v[2], v[3]};
				}
			}
		}
	}

	if (mAnimation) {
		mAnimation->update(IKIGAI::TIME::Timer::GetInstance().getDeltaTime().count() * 1000.0f);
	}

	ImGui::End();

}
#endif
