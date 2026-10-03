#pragma once
#include <memory>
#include <unordered_map>
#include "window/window.h"
#ifdef OCULUS
#include <android_native_app_glue.h>
#endif

namespace IKIGAI::WINDOW {

	class WindowManager {
	public:
		WindowManager() = default;
		~WindowManager() = default;

		std::shared_ptr<Window> createMainWindow(const WindowSettings& settings);
		std::shared_ptr<Window> createSecondaryWindow(const WindowSettings& settings);
#ifdef OCULUS
		void setAndroidApp(android_app* app) { mAndroidApp = app; }
#endif

		std::shared_ptr<Window> getMainWindow();
		std::shared_ptr<Window> getWindow(unsigned int id);

		void removeWindow(unsigned int id);
		void clear();

		void update(); // Calls pollEvent for all windows

		const std::unordered_map<unsigned int, std::shared_ptr<Window>>& getWindows() const { return mWindows; }

	private:
		std::unordered_map<unsigned int, std::shared_ptr<Window>> mWindows;
		unsigned int mMainWindowID = 0;
#ifdef OCULUS
		android_app* mAndroidApp = nullptr;
#endif
	};

}
