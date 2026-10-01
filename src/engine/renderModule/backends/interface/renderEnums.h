#pragma once
#include "mathModule/math.h"
#include "utilsModule/enum.h"

namespace IKIGAI::RENDER {
	enum class VertexAttribute {
		PER_VERTEX,
		PER_INSTANCE,
	};

	enum class PrimitiveMode {
		POINTS = 0,
		LINES,
		LINE_LOOP,
		LINE_STRIP,
		TRIANGLES,
		TRIANGLE_STRIP,
		TRIANGLE_FAN,
//#ifndef USING_GLES
		LINES_ADJACENCY,
		LINE_STRIP_ADJACENCY,
		TRIANGLES_ADJACENCY,
		TRIANGLE_STRIP_ADJACENCY,
		PATCHES,
//#endif
	};

	enum class RasterizationMode {
		POINT = 0,
		LINE,
		FILL
	};

	enum class CullFace {
		NONE = 0,
		FRONT,
		BACK,
		FRONT_AND_BACK
	};

	enum class DepthFunction {
		EQUAL = 0,
		NOT_EQUAL,
		LESS,
		GREATER,
		LESS_EQUAL,
		GREATER_EQUAL,
		ALWAYS,
		NEVER
	};

	enum class BlendMode {
		NONE = 0,
		ZERO,
		ONE,
		SRC_COLOR,
		ONE_MINUS_SRC_COLOR,
		SRC_ALPHA,
		ONE_MINUS_SRC_ALPHA,
		DST_ALPHA,
		ONE_MINUS_DST_ALPHA,
		DST_COLOR,
		ONE_MINUS_DST_COLOR,
		CONSTANT_COLOR,
		ONE_MINUS_CONSTANT_COLOR,
		CONSTANT_ALPHA,
		ONE_MINUS_CONSTANT_ALPHA
	};

	enum class RenderingCapability {
		BLEND = 0,
		CULL_FACE,
		DEPTH_TEST,
		DITHER,
		POLYGON_OFFSET_FILL,
		SAMPLE_ALPHA_TO_COVERAGE,
		SAMPLE_COVERAGE,
		SCISSOR_TEST,
		STENCIL_TEST,
		MULTISAMPLE
	};

	enum class BlendFunction {
		ADD = 0,
		SUB,
		REVERT_SUB,
		MIN,
		MAX
	};

	enum class StencilOperation {
		KEEP = 0,
		ZERO,
		REPLACE,
		INCREMENT,
		INCREMENT_WRAP,
		DECREMENT,
		DECREMENT_WRAP,
		INVERT
	};

	enum class TriangleOrientation {
		CW = 0,
		CCW
	};

	struct Depth {
		Depth() {}
		Depth(DepthFunction func, bool writeMask = true) : mFunc(func), mWriteMask(writeMask) {}

		bool mWriteMask = true;
		DepthFunction mFunc = DepthFunction::ALWAYS;
	};

	struct Stencil {
		uint8_t mReadMask = 255;
		uint8_t mWriteMask = 255;

		StencilOperation mDepthFail = StencilOperation::KEEP;
		StencilOperation mFail = StencilOperation::KEEP;
		DepthFunction mFunc = DepthFunction::ALWAYS;
		StencilOperation mPass = StencilOperation::KEEP;
	};

	//TODO: add operator== for cmp float
	struct Scissor {
		MATH::Vector2f mPosition = {0.0f, 0.0f};
		MATH::Vector2f mSize = {0.0f, 0.0f};
	};

	enum class Color: int {
		NONE = 0,
		R = 1,
		G = 2,
		B = 4,
		A = 8
	};

	DEFINE_ENUM_CLASS_BITWISE_OPERATORS(Color)

	struct Blending {
		Blending(BlendMode color_src, BlendMode color_dst, BlendMode alpha_src, BlendMode alpha_dst) :
			mColorSrc(color_src), mColorDst(color_dst), mAlphaSrc(alpha_src), mAlphaDst(alpha_dst) {
		}
		Blending(BlendMode src, BlendMode dst) : Blending(src, dst, src, dst) {
		}

		BlendFunction mColorFunc = BlendFunction::ADD;
		BlendMode mColorSrc;
		BlendMode mColorDst;
		BlendFunction mAlphaFunc = BlendFunction::ADD;
		BlendMode mAlphaSrc;
		BlendMode mAlphaDst;
		Color mColorMask = Color::R | Color::G | Color::B | Color::A;
	};

	struct Viewport {
		MATH::Vector2f mPosition = {0.0f, 0.0f};
		MATH::Vector2f mSize = {0.0f, 0.0f};
		float mMinDepth = 0.0f;
		float mMaxDepth = 1.0f;
	};


	enum class PixelFormat {
		R_FLOAT,
		RG_FLOAT,
		RGB_FLOAT,
		RGBA_FLOAT,
		R_INT,
		RG_INT,
		RGB_INT,
		RGBA_INT,

		R32_INT,
		RG32_INT,
		RGB32_INT,
		RGBA32_INT,

		DEPTH_24_UNORM_STENCIL_8_UINT,
		DEPTH32_FLOAT,
		DEPTH32_FLOAT_S8X24_UINT,
		DEPTH_32_FLOAT_STENCIL_8_UINT,
		BGRA_INT,
	};

	enum class MinMagFilter {
		LINEAR,
		NEAREST
	};

	enum class WrapFilter {
		CLAMP_TO_BORDER,
		CLAMP_TO_EDGE,
		MIRRORED_REPEAT,
		MIRROR_CLAMP_TO_EDGE,
		REPEAT
	};

	enum class CullingOptions {
		NONE = 0,
		FRUSTUM_PER_MODEL,
		FRUSTUM_PER_MESH
	};
	inline CullingOptions operator~ (CullingOptions a) { return (CullingOptions)~(int)a; }
	inline CullingOptions operator| (CullingOptions a, CullingOptions b) { return (CullingOptions)((int)a | (int)b); }
	inline CullingOptions operator& (CullingOptions a, CullingOptions b) { return (CullingOptions)((int)a & (int)b); }
	inline CullingOptions operator^ (CullingOptions a, CullingOptions b) { return (CullingOptions)((int)a ^ (int)b); }
	inline CullingOptions& operator|= (CullingOptions& a, CullingOptions b) { return (CullingOptions&)((int&)a |= (int)b); }
	inline CullingOptions& operator&= (CullingOptions& a, CullingOptions b) { return (CullingOptions&)((int&)a &= (int)b); }
	inline CullingOptions& operator^= (CullingOptions& a, CullingOptions b) { return (CullingOptions&)((int&)a ^= (int)b); }
	inline bool isFlagSet(CullingOptions flag, CullingOptions mask) { return (int)flag & (int)mask; }


	/**
	 * \brief ///////////////////////////////////////
	 */
	enum class PixelDataType {
		BYTE = 0,
		UNSIGNED_BYTE,
//#ifndef USING_GLES
		BITMAP,
//#endif
		SHORT,
		UNSIGNED_SHORT,
		INT,
		UNSIGNED_INT,
		FLOAT,
//#ifndef USING_GLES
		UNSIGNED_BYTE_3_3_2,
		UNSIGNED_BYTE_2_3_3_REV,
		UNSIGNED_SHORT_5_6_5,
		UNSIGNED_SHORT_5_6_5_REV,
		UNSIGNED_SHORT_4_4_4_4,
		UNSIGNED_SHORT_4_4_4_4_REV,
		UNSIGNED_SHORT_5_5_5_1,
		UNSIGNED_SHORT_1_5_5_5_REV,
		UNSIGNED_INT_8_8_8_8,
		UNSIGNED_INT_8_8_8_8_REV,
		UNSIGNED_INT_10_10_10_2,
		UNSIGNED_INT_2_10_10_10_REV
//#endif
	};
	enum class PixelDataFormat {
//#ifndef USING_GLES
		COLOR_INDEX = 0,
		STENCIL_INDEX,
//#endif
		DEPTH_COMPONENT,
//#ifndef USING_GLES
		RED,
		GREEN,
		BLUE,
//#endif
		ALPHA,
		RGB,
//#ifndef USING_GLES
		BGR,
//#endif
		RGBA,
//#ifndef USING_GLES
		BGRA,
//#endif
		LUMINANCE,
		LUMINANCE_ALPHA
	};


	


}
