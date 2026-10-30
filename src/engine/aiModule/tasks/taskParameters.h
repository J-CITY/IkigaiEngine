#pragma once
#include "aiModule/behaviorTree/behaviorTree.h"

namespace IKIGAI::AI {
struct EmptyParameters {
	template <class C> static auto serde(C& c, EmptyParameters& v) {
		serde::serde_struct(c, v);
	}
};
struct DurationParameters {
	double Duration = 0;
	template <class C> static auto serde(C& c, DurationParameters& v) {
		serde::serde_struct(c, v).field(&DurationParameters::Duration, "Duration");
	}
};
struct KeyParameters {
	std::string Key {};
	template <class C> static auto serde(C& c, KeyParameters& v) {
		serde::serde_struct(c, v).field(&KeyParameters::Key, "Key");
	}
};
struct SetValueParameters {
	std::string Key {};
	DATA::LiteralDescriptor Value {};
	template <class C> static auto serde(C& c, SetValueParameters& v) {
		serde::serde_struct(c, v).field(&SetValueParameters::Key, "Key").field(&SetValueParameters::Value, "Value");
	}
};
struct MoveParameters {
	std::string Target {};
	float Speed = 3;
	float AcceptanceRadius = 0.2f;
	double Timeout = 10;
	template <class C> static auto serde(C& c, MoveParameters& v) {
		using namespace serde::attribute;
		serde::serde_struct(c, v)
			.field(&MoveParameters::Target, "Target")
			.field(&MoveParameters::Speed, "Speed", default_ { 3.f })
			.field(&MoveParameters::AcceptanceRadius, "AcceptanceRadius", default_ { 0.2f })
			.field(&MoveParameters::Timeout, "Timeout", default_ { 10.0 });
	}
};
}
