#pragma once

#include <optional>
#include <string>

namespace IKIGAI {
struct Config {
	inline static std::optional<int> WINDOW_WIDTH;
	inline static std::optional<int> WINDOW_HEIGHT;
	inline static std::optional<bool> WINDOW_FULLSCREEN;
	inline static std::optional<std::string> WINDOW_TITLE;
	inline static std::string ROOT;
	inline static bool PROFILE_LOADING = false;
	inline static bool RENDER_CPU_OVERLAP = false;
	inline static std::string CONTENT_RELEASE;
	inline static std::string STORAGE_ROOT;
	inline static std::string PROFILE_ID = "default";
	inline static std::string ORGANIZATION = "Ikigai";
	inline static std::string APPLICATION = "IkigaiEngine";
	inline static std::string ASSETS_PATH = "assets/";
	inline static std::string ENGINE_ASSETS_PATH = ASSETS_PATH + "engine/";
	inline static std::string USER_ASSETS_PATH = ASSETS_PATH + "game/";
};
};
