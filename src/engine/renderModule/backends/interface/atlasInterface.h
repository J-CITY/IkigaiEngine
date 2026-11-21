#pragma once
#include <map>
#include <string>

namespace IKIGAI::RENDER {

	struct AtlasRect {
		AtlasRect() = default;
		AtlasRect(float x, float y, float w, float h) : mX(x), mY(y), mW(w), mH(h) {}

		float mX = 0.0f;
		float mY = 0.0f;
		float mW = 0.0f;
		float mH = 0.0f;

		template <class Context> constexpr static void serde(Context& context, AtlasRect& value);
	};

	struct AtlasData {
		AtlasData() = default;
		std::map<std::string, AtlasRect> mRects;
		std::string mPath;

		template <class Context> constexpr static void serde(Context& context, AtlasData& value);

		void setRects(std::map<std::string, AtlasRect> rects) { mRects = rects; }
		std::map<std::string, AtlasRect> getRects() { return mRects; }
	};

} // namespace IKIGAI::RENDER
