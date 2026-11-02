#include "vectorComponents.h"
#include <functional>
namespace {
void combine(uint64_t& h, size_t value) {
	h ^= value + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
}
template <class T> void fieldHash(uint64_t& h, const T& v) {
	combine(h, std::hash<T> { }(v));
}
void fieldHash(uint64_t& h, const IKIGAI::MATH::Vector2f& v) {
	fieldHash(h, v.x);
	fieldHash(h, v.y);
}
void fieldHash(uint64_t& h, const IKIGAI::MATH::Vector4f& v) {
	fieldHash(h, v.x);
	fieldHash(h, v.y);
	fieldHash(h, v.z);
	fieldHash(h, v.w);
}
}
namespace IKIGAI::ECS {
SdfTextComponent::SdfTextComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "SdfTextComponent";
}
SdfTextComponent::SdfTextComponent(UTILS::Ref<Object> object, const Descriptor& d)
	: ComponentBase(object) {
	__NAME__ = "SdfTextComponent";
	Text = d.Text;
	Font = d.Font;
	Size = d.Size;
	WrapWidth = d.WrapWidth;
	Alignment = d.Alignment;
	LetterSpacing = d.LetterSpacing;
	LineSpacing = d.LineSpacing;
	Color = d.Color;
	OutlineWidth = d.OutlineWidth;
	OutlineColor = d.OutlineColor;
	ShadowOffset = d.ShadowOffset;
	ShadowColor = d.ShadowColor;
	Order = d.Order;
}
SdfTextComponent::Descriptor SdfTextComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<SdfTextComponent>();
	d.Text = Text;
	d.Font = Font;
	d.Size = Size;
	d.WrapWidth = WrapWidth;
	d.Alignment = Alignment;
	d.LetterSpacing = LetterSpacing;
	d.LineSpacing = LineSpacing;
	d.Color = Color;
	d.OutlineWidth = OutlineWidth;
	d.OutlineColor = OutlineColor;
	d.ShadowOffset = ShadowOffset;
	d.ShadowColor = ShadowColor;
	d.Order = Order;
	return d;
}
VectorShapeComponent::VectorShapeComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "VectorShapeComponent";
}
VectorShapeComponent::VectorShapeComponent(UTILS::Ref<Object> object, const Descriptor& d)
	: ComponentBase(object) {
	__NAME__ = "VectorShapeComponent";
	Shape = d.Shape;
	Size = d.Size;
	Radii = d.Radii;
	CornerRadii = d.CornerRadii;
	StartAngle = d.StartAngle;
	EndAngle = d.EndAngle;
	Points = d.Points;
	Color = d.Color;
	Filled = d.Filled;
	StrokeColor = d.StrokeColor;
	StrokeWidth = d.StrokeWidth;
	Cap = d.Cap;
	Join = d.Join;
	MiterLimit = d.MiterLimit;
	Gradient = d.Gradient;
	GradientColor = d.GradientColor;
	GradientStart = d.GradientStart;
	GradientEnd = d.GradientEnd;
	Order = d.Order;
}
VectorShapeComponent::Descriptor VectorShapeComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<VectorShapeComponent>();
	d.Shape = Shape;
	d.Size = Size;
	d.Radii = Radii;
	d.CornerRadii = CornerRadii;
	d.StartAngle = StartAngle;
	d.EndAngle = EndAngle;
	d.Points = Points;
	d.Color = Color;
	d.Filled = Filled;
	d.StrokeColor = StrokeColor;
	d.StrokeWidth = StrokeWidth;
	d.Cap = Cap;
	d.Join = Join;
	d.MiterLimit = MiterLimit;
	d.Gradient = Gradient;
	d.GradientColor = GradientColor;
	d.GradientStart = GradientStart;
	d.GradientEnd = GradientEnd;
	d.Order = Order;
	return d;
}
VectorPathComponent::VectorPathComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "VectorPathComponent";
}
VectorPathComponent::VectorPathComponent(UTILS::Ref<Object> object, const Descriptor& d)
	: ComponentBase(object) {
	__NAME__ = "VectorPathComponent";
	Commands = d.Commands;
	PathAsset = d.PathAsset;
	Color = d.Color;
	Filled = d.Filled;
	StrokeColor = d.StrokeColor;
	StrokeWidth = d.StrokeWidth;
	Cap = d.Cap;
	Join = d.Join;
	MiterLimit = d.MiterLimit;
	Gradient = d.Gradient;
	GradientColor = d.GradientColor;
	GradientStart = d.GradientStart;
	GradientEnd = d.GradientEnd;
	Order = d.Order;
}
VectorPathComponent::Descriptor VectorPathComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<VectorPathComponent>();
	d.Commands = Commands;
	d.PathAsset = PathAsset;
	d.Color = Color;
	d.Filled = Filled;
	d.StrokeColor = StrokeColor;
	d.StrokeWidth = StrokeWidth;
	d.Cap = Cap;
	d.Join = Join;
	d.MiterLimit = MiterLimit;
	d.Gradient = Gradient;
	d.GradientColor = GradientColor;
	d.GradientStart = GradientStart;
	d.GradientEnd = GradientEnd;
	d.Order = Order;
	return d;
}
VectorCanvasComponent::VectorCanvasComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "VectorCanvasComponent";
}
VectorCanvasComponent::VectorCanvasComponent(UTILS::Ref<Object> object, const Descriptor& d)
	: ComponentBase(object) {
	__NAME__ = "VectorCanvasComponent";
	WorldSpace = d.WorldSpace;
	DepthTest = d.DepthTest;
	SortingLayer = d.SortingLayer;
	Order = d.Order;
	DpiScale = d.DpiScale;
}
VectorCanvasComponent::Descriptor VectorCanvasComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<VectorCanvasComponent>();
	d.WorldSpace = WorldSpace;
	d.DepthTest = DepthTest;
	d.SortingLayer = SortingLayer;
	d.Order = Order;
	d.DpiScale = DpiScale;
	return d;
}

uint64_t SdfTextComponent::configurationHash() const {
	uint64_t h = 0;
	fieldHash(h, Text);
	fieldHash(h, Font);
	fieldHash(h, Size);
	fieldHash(h, WrapWidth);
	fieldHash(h, Alignment);
	fieldHash(h, LetterSpacing);
	fieldHash(h, LineSpacing);
	fieldHash(h, Color);
	fieldHash(h, OutlineWidth);
	fieldHash(h, OutlineColor);
	fieldHash(h, ShadowOffset);
	fieldHash(h, ShadowColor);
	fieldHash(h, Order);
	return h;
}

uint64_t VectorShapeComponent::configurationHash() const {
	uint64_t h = 0;
	fieldHash(h, Shape);
	fieldHash(h, Size);
	fieldHash(h, Radii);
	fieldHash(h, CornerRadii);
	fieldHash(h, StartAngle);
	fieldHash(h, EndAngle);
	fieldHash(h, Points);
	fieldHash(h, Color);
	fieldHash(h, Filled);
	fieldHash(h, StrokeColor);
	fieldHash(h, StrokeWidth);
	fieldHash(h, Cap);
	fieldHash(h, Join);
	fieldHash(h, MiterLimit);
	fieldHash(h, Gradient);
	fieldHash(h, GradientColor);
	fieldHash(h, GradientStart);
	fieldHash(h, GradientEnd);
	fieldHash(h, Order);
	return h;
}

uint64_t VectorPathComponent::configurationHash() const {
	uint64_t h = 0;
	fieldHash(h, Commands);
	fieldHash(h, PathAsset);
	fieldHash(h, Color);
	fieldHash(h, Filled);
	fieldHash(h, StrokeColor);
	fieldHash(h, StrokeWidth);
	fieldHash(h, Cap);
	fieldHash(h, Join);
	fieldHash(h, MiterLimit);
	fieldHash(h, Gradient);
	fieldHash(h, GradientColor);
	fieldHash(h, GradientStart);
	fieldHash(h, GradientEnd);
	fieldHash(h, Order);
	return h;
}

uint64_t VectorCanvasComponent::configurationHash() const {
	uint64_t h = 0;
	fieldHash(h, WorldSpace);
	fieldHash(h, DepthTest);
	fieldHash(h, SortingLayer);
	fieldHash(h, Order);
	fieldHash(h, DpiScale);
	return h;
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::SdfTextComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::VectorShapeComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::VectorPathComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::VectorCanvasComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	return true;
}();
}
