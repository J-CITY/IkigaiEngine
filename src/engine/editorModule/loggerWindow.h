#pragma once
#ifdef USE_EDITOR
#include <list>

#include "editorWindow.h"
#include "editorModule/imguiHelper/TextEditor.h"
#include "utilsModule/log/logger.h"

namespace IKIGAI::EDITOR {


	struct ImGuiLogger {
		std::list<TextEditor::Record> mBuffer;
		int mBufferSize = 10;
		std::string mSearch;

		TextEditor editor;

		ImGuiLogger();

		bool ScrollToBottom = true;

		void addLog(const TextEditor::Record& record);

		void addLog(UTILS::LOGG::Log::Level level, const std::string& word);

		void clear() {
			mBuffer.clear();
		}

		std::map<UTILS::LOGG::Log::Level, bool> mLogLevels = {
			{UTILS::LOGG::Log::Level::Debug, true},
			{UTILS::LOGG::Log::Level::Error, true},
			{UTILS::LOGG::Log::Level::Info, true},
			{UTILS::LOGG::Log::Level::Warning, true},
		};
		const std::map<UTILS::LOGG::Log::Level, std::string> mLogLevelsToString = {
			{UTILS::LOGG::Log::Level::Debug, "DEBUG:"},
			{UTILS::LOGG::Log::Level::Error, "ERROR:"},
			{UTILS::LOGG::Log::Level::Info, "INFO:"},
			{UTILS::LOGG::Log::Level::Warning, "WARNING:"},
		};

		void draw();
	};

	class LoggerWindow : public EditorWindow {
	public:
		virtual ~LoggerWindow() override = default;
		void draw() override;
	};
}
#endif
