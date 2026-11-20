#include "inputActionStorage.h"

#include "resourceModule/jsonLoader.h"
#include "resourceModule/playerProfile.h"
#include "utilsModule/log/loggerDefine.h"
#include "utilsModule/serviceManager.h"
#include "windowModule/inputManager/inputActions.h"
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace IKIGAI::CORE {

bool LoadInputActions(INPUT_SYSTEM::InputActions& actions, const std::string& path) {
	auto result = UTILS::FromJson<INPUT_SYSTEM::InputConfig>(path);
	if (result.isErr()) {
		LOG_ERROR << "Failed to load input resource '" << path << "': " << result.unwrapErr().text;
		return false;
	}
	actions.setConfig(result.unwrap());
	return true;
}

bool LoadInputBindingOverrides(INPUT_SYSTEM::InputActions& actions, const std::string& path) {
	std::optional<INPUT_SYSTEM::InputBindingOverrides> loaded;
	if (UTILS::ServiceManager::Check<RESOURCES::PlayerProfileService>()) {
		// A profile that never saved overrides has no file; reading it would throw on first run.
		const auto profile = UTILS::ServiceManager::Get<RESOURCES::PlayerProfileService>().profile();
		const auto bytes = profile.exists(path) ? profile.readFile(path) : std::vector<std::uint8_t> {};
		if (!bytes.empty()) {
			if (auto parsed = UTILS::FromJsonStr<INPUT_SYSTEM::InputBindingOverrides>(std::string(bytes.begin(), bytes.end())); parsed.isOk()) {
				loaded = parsed.unwrap();
			}
		}
	}
	if (!loaded) {
		if (auto parsed = UTILS::FromJson<INPUT_SYSTEM::InputBindingOverrides>(path); parsed.isOk()) {
			loaded = parsed.unwrap();
		}
	}
	if (!loaded) {
		LOG_WARNING << "No input binding overrides loaded from '" << path << "'";
		return false;
	}
	actions.setBindingOverrides(std::move(*loaded));
	return true;
}

bool SaveInputBindingOverrides(const INPUT_SYSTEM::InputActions& actions, const std::string& path) {
	auto result = UTILS::ToJsonStr(actions.getBindingOverrides(), 2);
	if (result.isErr()) {
		LOG_ERROR << "Failed to serialize input binding overrides: " << result.unwrapErr().text;
		return false;
	}
	if (!UTILS::ServiceManager::Check<RESOURCES::PlayerProfileService>()) {
		LOG_ERROR << "PlayerProfileService unavailable; cannot save binding overrides to '" << path << "'";
		return false;
	}
	const auto text = result.unwrap();
	std::vector<std::uint8_t> bytes(text.begin(), text.end());
	UTILS::ServiceManager::Get<RESOURCES::PlayerProfileService>().profile().writeFile(path, bytes).wait();
	return true;
}

}
