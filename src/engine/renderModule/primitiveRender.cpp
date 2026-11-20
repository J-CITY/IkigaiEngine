#include "primitiveRender.h"
#include "render.h"
#include "renderModule/vectorGraphics/vectorRenderer.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/serviceManager.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	using V3 = MATH::Vector3f;
	using V4 = MATH::Vector4f;
	constexpr float Pi = 3.14159265358979323846f;
	V3 cross(V3 a, V3 b) {
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}
	float length(V3 value) {
		return std::sqrt(value.dot(value));
	}
	V3 normalized(V3 value) {
		const auto magnitude = length(value);
		if (!std::isfinite(magnitude) || magnitude < .00001f) {
			throw std::invalid_argument("Primitive direction must be finite and nonzero");
		}
		return value / magnitude;
	}
	void validate(V3 point) {
		if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
			throw std::invalid_argument("Primitive position must be finite");
		}
	}
	void validate(PrimitiveStyle style) {
		if (!std::isfinite(style.width) || style.width <= 0 || !std::isfinite(style.color.x) || !std::isfinite(style.color.y)
			|| !std::isfinite(style.color.z) || !std::isfinite(style.color.w)) {
			throw std::invalid_argument("Invalid primitive style");
		}
	}
	void dimensions(float radius, unsigned segments) {
		if (!std::isfinite(radius) || radius <= 0 || segments < 3 || segments > 4096) {
			throw std::invalid_argument("Invalid primitive radius or segment count");
		}
	}
}
void PrimitiveRender::line(V3 start, V3 end, PrimitiveStyle style) {
	validate(start);
	validate(end);
	validate(style);
	mCommands.push_back({ { start, end, {} }, style, false });
}
void PrimitiveRender::polyline(std::span<const V3> points, bool closed, PrimitiveStyle style) {
	for (size_t i = 1; i < points.size(); ++i) {
		line(points[i - 1], points[i], style);
	}
	if (closed && points.size() > 2) {
		line(points.back(), points.front(), style);
	}
}
void PrimitiveRender::triangle(V3 a, V3 b, V3 c, bool filled, PrimitiveStyle style) {
	if (!filled) {
		line(a, b, style);
		line(b, c, style);
		line(c, a, style);
		return;
	}
	validate(a);
	validate(b);
	validate(c);
	validate(style);
	mCommands.push_back({ { a, b, c }, style, true });
}
void PrimitiveRender::rectangle(V3 origin, V3 horizontal, V3 vertical, bool filled, PrimitiveStyle style) {
	const std::array<V3, 4> points { origin, origin + horizontal, origin + horizontal + vertical, origin + vertical };
	if (!filled) {
		polyline(points, true, style);
		return;
	}
	triangle(points[0], points[1], points[2], true, style);
	triangle(points[0], points[2], points[3], true, style);
}
void PrimitiveRender::circle(V3 center, V3 normal, float radius, unsigned segments, bool filled, PrimitiveStyle style) {
	dimensions(radius, segments);
	normal = normalized(normal);
	const auto horizontal = normalized(cross(normal, std::abs(normal.y) < .9f ? V3 { 0, 1, 0 } : V3 { 1, 0, 0 })) * radius;
	const auto vertical = cross(normal, horizontal);
	auto point = [&](unsigned index) {
		const auto angle = 2 * Pi * index / segments;
		return center + horizontal * std::cos(angle) + vertical * std::sin(angle);
	};
	for (unsigned i = 0; i < segments; ++i) {
		if (filled) {
			triangle(center, point(i), point(i + 1), true, style);
		}
		else {
			line(point(i), point(i + 1), style);
		}
	}
}
void PrimitiveRender::box(V3 minimum, V3 maximum, PrimitiveStyle style) {
	validate(minimum);
	validate(maximum);
	if (minimum.x > maximum.x || minimum.y > maximum.y || minimum.z > maximum.z) {
		throw std::invalid_argument("Primitive box bounds are reversed");
	}
	std::array<V3, 8> corners;
	for (unsigned i = 0; i < 8; ++i) {
		corners[i] = { (i & 1) ? maximum.x : minimum.x, (i & 2) ? maximum.y : minimum.y, (i & 4) ? maximum.z : minimum.z };
	}
	for (unsigned i = 0; i < 8; ++i) {
		for (unsigned axis = 1; axis <= 4; axis *= 2) {
			if (!(i & axis)) {
				line(corners[i], corners[i | axis], style);
			}
		}
	}
}
void PrimitiveRender::sphere(V3 center, float radius, unsigned segments, PrimitiveStyle style) {
	circle(center, { 1, 0, 0 }, radius, segments, false, style);
	circle(center, { 0, 1, 0 }, radius, segments, false, style);
	circle(center, { 0, 0, 1 }, radius, segments, false, style);
}
void PrimitiveRender::capsule(V3 bottom, V3 top, float radius, unsigned segments, PrimitiveStyle style) {
	dimensions(radius, segments);
	if (length(top - bottom) < .00001f) {
		sphere(bottom, radius, segments, style);
		return;
	}
	const auto axis = normalized(top - bottom);
	const auto horizontal = normalized(cross(axis, std::abs(axis.y) < .9f ? V3 { 0, 1, 0 } : V3 { 1, 0, 0 }));
	const auto vertical = cross(axis, horizontal);
	circle(bottom, axis, radius, segments, false, style);
	circle(top, axis, radius, segments, false, style);
	for (auto radial : { horizontal, vertical, horizontal * (-1), vertical * (-1) }) {
		line(bottom + radial * radius, top + radial * radius, style);
		for (unsigned i = 0; i < segments / 2; ++i) {
			const float a = Pi * i / (segments / 2), b = Pi * (i + 1) / (segments / 2);
			line(top + (radial * std::cos(a) + axis * std::sin(a)) * radius, top + (radial * std::cos(b) + axis * std::sin(b)) * radius, style);
			line(bottom + (radial * std::cos(a) - axis * std::sin(a)) * radius, bottom + (radial * std::cos(b) - axis * std::sin(b)) * radius, style);
		}
	}
}
void PrimitiveRender::axes(const MATH::Matrix4f& transform, float scale, PrimitiveStyle style) {
	if (!std::isfinite(scale) || scale <= 0) {
		throw std::invalid_argument("Invalid primitive axes scale");
	}
	const auto origin = transform * V3 {};
	style.color = { 1, 0, 0, style.color.w };
	line(origin, transform * V3 { scale, 0, 0 }, style);
	style.color = { 0, 1, 0, style.color.w };
	line(origin, transform * V3 { 0, scale, 0 }, style);
	style.color = { 0, 0, 1, style.color.w };
	line(origin, transform * V3 { 0, 0, scale }, style);
}
void PrimitiveRender::grid(V3 origin, unsigned halfCells, float spacing, PrimitiveStyle style) {
	if (halfCells > 4096 || !std::isfinite(spacing) || spacing <= 0) {
		throw std::invalid_argument("Invalid primitive grid dimensions");
	}
	const auto extent = halfCells * spacing;
	for (int i = -static_cast<int>(halfCells); i <= static_cast<int>(halfCells); ++i) {
		line(origin + V3 { -extent, 0, i * spacing }, origin + V3 { extent, 0, i * spacing }, style);
		line(origin + V3 { i * spacing, 0, -extent }, origin + V3 { i * spacing, 0, extent }, style);
	}
}
void PrimitiveRender::clear() {
	mCommands.clear();
}
size_t PrimitiveRender::size() const {
	return mCommands.size();
}
void PrimitiveRender::render(Renderer& renderer, const MATH::Matrix4f& worldToClip, MATH::Vector2f viewport, PrimitiveSpace space) {
	if (mCommands.empty() || viewport.x <= 0 || viewport.y <= 0) {
		return;
	}
	if (!mMaterial) {
		mMaterial = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource("/materials/render/portable/gui.mat");
	}
	if (!mWhite) {
		TextureResource resource;
		resource.width = resource.height = 1;
		resource.channels = 4;
		resource.pixelType = PixelFormat::RGBA_INT;
		resource.useMipmap = false;
		resource.colorData = { 255, 255, 255, 255 };
		mWhite = renderer.createTexture(resource);
	}
	if (!mBatches) {
		mBatches = std::make_shared<VECTOR::VectorRenderer>();
	}
	mBatches->beginFrame(space == PrimitiveSpace::World);
	const bool zeroToOne = DriverInterface::settings.backend != RenderSettings::Backend::OPENGL;
	auto clipPoint = [&](V3 value) {
		return space == PrimitiveSpace::Screen
			? V4 { 2 * value.x / viewport.x - 1, 1 - 2 * value.y / viewport.y, zeroToOne ? value.z : 2 * value.z - 1, 1 }
			: worldToClip * V4 { value.x, value.y, value.z, 1 };
	};
	auto distance = [&](V4 p, unsigned plane) {
		switch (plane) {
		case 0:
			return p.w - .00001f;
		case 1:
			return zeroToOne ? p.z : p.z + p.w;
		case 2:
			return p.w - p.z;
		case 3:
			return p.x + p.w;
		case 4:
			return p.w - p.x;
		case 5:
			return p.y + p.w;
		default:
			return p.w - p.y;
		}
	};
	auto interpolate = [](V4 a, V4 b, float alpha) {
		return V4 { a.x + (b.x - a.x) * alpha, a.y + (b.y - a.y) * alpha, a.z + (b.z - a.z) * alpha, a.w + (b.w - a.w) * alpha };
	};
	auto ndc = [](V4 p) { return V3 { p.x / p.w, p.y / p.w, p.z / p.w }; };
	for (const auto& command : mCommands) {
		if (command.style.space != space) {
			continue;
		}
		std::vector<Vertex> vertices;
		std::vector<unsigned> indices;
		auto vertex = [&](V3 point) {
			Vertex v(point, { .5f, .5f });
			v.m_Weights = command.style.color;
			vertices.push_back(v);
		};
		if (command.filled) {
			std::vector<V4> polygon;
			for (auto point : command.points) {
				polygon.push_back(clipPoint(point));
			}
			for (unsigned plane = 0; plane < 7 && !polygon.empty(); ++plane) {
				std::vector<V4> output;
				auto previous = polygon.back();
				auto previousDistance = distance(previous, plane);
				for (auto current : polygon) {
					const auto currentDistance = distance(current, plane);
					if ((currentDistance >= 0) != (previousDistance >= 0)) {
						output.push_back(interpolate(previous, current, previousDistance / (previousDistance - currentDistance)));
					}
					if (currentDistance >= 0) {
						output.push_back(current);
					}
					previous = current;
					previousDistance = currentDistance;
				}
				polygon = std::move(output);
			}
			for (auto point : polygon) {
				vertex(ndc(point));
			}
			for (unsigned i = 2; i < vertices.size(); ++i) {
				indices.push_back(0);
				indices.push_back(i - 1);
				indices.push_back(i);
			}
		}
		else {
			auto start = clipPoint(command.points[0]);
			auto end = clipPoint(command.points[1]);
			bool visible = true;
			for (unsigned plane = 0; plane < 7; ++plane) {
				auto a = distance(start, plane), b = distance(end, plane);
				if (a < 0 && b < 0) {
					visible = false;
					break;
				}
				if ((a >= 0) != (b >= 0)) {
					auto point = interpolate(start, end, a / (a - b));
					if (a < 0) {
						start = point;
					}
					else {
						end = point;
					}
				}
			}
			if (!visible) {
				continue;
			}
			auto a = ndc(start), b = ndc(end);
			const auto dx = (b.x - a.x) * viewport.x, dy = (b.y - a.y) * viewport.y;
			const auto magnitude = std::sqrt(dx * dx + dy * dy);
			if (magnitude < .00001f) {
				continue;
			}
			const V3 offset { -dy / magnitude * command.style.width / viewport.x, dx / magnitude * command.style.width / viewport.y, 0 };
			vertex(a + offset);
			vertex(a - offset);
			vertex(b - offset);
			vertex(b + offset);
			indices = { 0, 1, 2, 0, 2, 3 };
		}
		if (indices.empty()) {
			continue;
		}
		mBatches->enqueue(std::move(vertices), indices, mWhite, mMaterial->getShader(), MATH::Matrix4f::Identity,
			{ BlendMode::SRC_ALPHA, BlendMode::ONE_MINUS_SRC_ALPHA, BlendMode::ONE, BlendMode::ONE_MINUS_SRC_ALPHA },
			{ command.style.depthTest && space == PrimitiveSpace::World ? DepthFunction::LESS_EQUAL : DepthFunction::ALWAYS, false }, {}, false);
	}
	mBatches->flush(renderer);
	renderer.setCull(CullFace::BACK);
}
}
