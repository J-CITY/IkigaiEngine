#pragma once
#include "mathModule/math.h"
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>
#include <string>
#include <vector>

namespace IKIGAI::SURFACE {
enum class BrushOperation { AddHeight, FlattenHeight, PaintLayer, FoliageDensity };
void from_json(const nlohmann::json& json, BrushOperation& operation);
void to_json(nlohmann::json& json, const BrushOperation& operation);

struct BrushStroke {
	BrushOperation Operation = BrushOperation::PaintLayer;
	MATH::Vector2f Center = {};
	float Radius = 4.0f;
	float Hardness = 0.5f;
	float Strength = 1.0f;
	float Value = 0.0f;
	unsigned LayerId = 1;
	void validate() const;
	template <class C> static void serde(C& context, BrushStroke& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&BrushStroke::Operation, "Operation")
			.field(&BrushStroke::Center, "Center")
			.field(&BrushStroke::Radius, "Radius", default_ { 4.0f })
			.field(&BrushStroke::Hardness, "Hardness", default_ { 0.5f })
			.field(&BrushStroke::Strength, "Strength", default_ { 1.0f })
			.field(&BrushStroke::Value, "Value", default_ { 0.0f })
			.field(&BrushStroke::LayerId, "LayerId", default_ { 1u });
	}
};
struct EditLayer {
	unsigned Id = 1;
	std::string Name = "Manual edits";
	bool Enabled = true;
	float Opacity = 1.0f;
	std::vector<BrushStroke> Strokes = {};
	template <class C> static void serde(C& context, EditLayer& value) {
		if constexpr (!C::is_serialize) {
			value.Strokes.clear();
		}
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&EditLayer::Id, "Id")
			.field(&EditLayer::Name, "Name", default_ { std::string { "Manual edits" } })
			.field(&EditLayer::Enabled, "Enabled", default_ { true })
			.field(&EditLayer::Opacity, "Opacity", default_ { 1.0f })
			.field(&EditLayer::Strokes, "Strokes", default_ { std::vector<BrushStroke> {} });
	}
};
float brushInfluence(const BrushStroke& stroke, float x, float z);
}
