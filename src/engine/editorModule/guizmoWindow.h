#pragma once
#include "editorWindow.h"
#ifdef USE_EDITOR
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <filesystem>

#include "imgui.h"
#include "editorModule/imguiHelper/ImGuizmo.h"

namespace IKIGAI::EDITOR {
	class GuizmoWindow: public EditorWindow{

	public:
		GuizmoWindow(const std::string& path);
		virtual ~GuizmoWindow() override = default;
		void draw() override;

	private:
		void drawGuizmo(int w, int h);
		ImGuizmo::OPERATION mCurrentGizmoOperation = ImGuizmo::TRANSLATE;

		bool mIsPerspective = true;

		ImGuiWindowFlags mGizmoWindowFlags = 0;
	};
}
#endif
