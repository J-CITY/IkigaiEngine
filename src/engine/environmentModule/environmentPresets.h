#pragma once
#include "environment.h"

namespace IKIGAI::ENVIRONMENT {
enum class Preset { Custom, Clear, WhiteClouds, Overcast, Storm, Rain, Snow };
void from_json(const nlohmann::json& json, Preset& value);
void to_json(nlohmann::json& json, const Preset& value);
Settings presetSettings(Preset preset, const Settings& base);

class PresetTransition {
public:
	void reset(const Settings& settings);
	void start(const Settings& target, float seconds);
	void update(double seconds);
	const Settings& current() const;
	float progress() const;
	bool active() const;

private:
	Settings mCurrent = {};
	Settings mStart = {};
	Settings mTarget = {};
	double mElapsed = 0.0;
	float mDuration = 0.0f;
};
}
