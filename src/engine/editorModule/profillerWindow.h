#pragma once
#include "editorWindow.h"
#ifdef USE_EDITOR
#include "profilerModule/memoryProfiler.h"
#include <chrono>
#include <optional>
#include <string>
#if IKIGAI_ENABLE_MEMORY_TRACKING
#include "imgui.h"
#endif
namespace IKIGAI::EDITOR {
	class ProfillerWindow : public EditorWindow {
	public:
		virtual ~ProfillerWindow() override = default;
		void draw() override;
		bool captureMemorySnapshot();
		void clearMemorySnapshot();
		const PROFILER::MemorySnapshot* memorySnapshot() const;
	private:
		void drawFrameTime();
		void drawRenderStatistics();
		void drawVirtualGeometry();
		void drawMemory();
		std::size_t mFrameId = 0;
#if IKIGAI_ENABLE_MEMORY_TRACKING
		void rebuildMemoryRows();
		void drawCpuMemory();
		void drawGpuMemory();
		std::optional<PROFILER::MemorySnapshot> mMemorySnapshot;
		std::chrono::steady_clock::time_point mCapturedAt;
		std::string mMemoryError;
		ImGuiTextFilter mMemoryFilter;
		std::vector<std::size_t> mCpuRows, mGpuRows;
		int mCpuCategory = 0, mGpuKind = 0, mGpuBackend = 0;
		bool mPendingOnly = false;
#endif
	};
}
#endif
