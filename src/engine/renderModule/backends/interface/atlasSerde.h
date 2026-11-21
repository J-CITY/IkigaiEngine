#pragma once

#include "atlasInterface.h"
#include <serdepp/serializer.hpp>

namespace IKIGAI::RENDER {
template <class Context> constexpr void AtlasRect::serde(Context& context, AtlasRect& value) {
	using Self = AtlasRect;
	using namespace serde::attribute;
	serde::serde_struct(context, value).field(&Self::mX, "X").field(&Self::mY, "Y").field(&Self::mW, "W").field(&Self::mH, "H");
}

template <class Context> constexpr void AtlasData::serde(Context& context, AtlasData& value) {
	using Self = AtlasData;
	using namespace serde::attribute;
	serde::serde_struct(context, value).field(&Self::mRects, "Files").field(&Self::mPath, "Path");
}
}
