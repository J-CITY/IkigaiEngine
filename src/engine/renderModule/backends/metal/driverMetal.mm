#ifdef METAL_BACKEND

#include "driverMetal.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#include <SDL3/SDL.h>

#include "deviceMetal.h"
#include "frameBufferMetal.h"
#include "materialMetal.h"
#include "modelMetal.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/modelManager.h"
#include "resourceModule/serviceManager.h"
#include "utilsModule/imguiHelper/imguiBackend/imguiBackend.h"
#include "utilsModule/log/loggerDefine.h"
#include "windowModule/window/window.h"

namespace IKIGAI::RENDER {

	namespace {
		MTLPrimitiveType ToPrimitive(PrimitiveMode mode) {
			switch (mode) {
			case PrimitiveMode::POINTS: return MTLPrimitiveTypePoint;
			case PrimitiveMode::LINES:
			case PrimitiveMode::LINES_ADJACENCY: return MTLPrimitiveTypeLine;
			case PrimitiveMode::LINE_STRIP:
			case PrimitiveMode::LINE_LOOP:
			case PrimitiveMode::LINE_STRIP_ADJACENCY: return MTLPrimitiveTypeLineStrip;
			case PrimitiveMode::TRIANGLE_STRIP:
			case PrimitiveMode::TRIANGLE_STRIP_ADJACENCY: return MTLPrimitiveTypeTriangleStrip;
			default: return MTLPrimitiveTypeTriangle;
			}
		}

		MTLBlendFactor ToBlendFactor(BlendMode mode) {
			switch (mode) {
			case BlendMode::ZERO:
			case BlendMode::NONE: return MTLBlendFactorZero;
			case BlendMode::ONE: return MTLBlendFactorOne;
			case BlendMode::SRC_COLOR: return MTLBlendFactorSourceColor;
			case BlendMode::ONE_MINUS_SRC_COLOR: return MTLBlendFactorOneMinusSourceColor;
			case BlendMode::SRC_ALPHA: return MTLBlendFactorSourceAlpha;
			case BlendMode::ONE_MINUS_SRC_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
			case BlendMode::DST_ALPHA: return MTLBlendFactorDestinationAlpha;
			case BlendMode::ONE_MINUS_DST_ALPHA: return MTLBlendFactorOneMinusDestinationAlpha;
			case BlendMode::DST_COLOR: return MTLBlendFactorDestinationColor;
			case BlendMode::ONE_MINUS_DST_COLOR: return MTLBlendFactorOneMinusDestinationColor;
			case BlendMode::CONSTANT_COLOR: return MTLBlendFactorBlendColor;
			case BlendMode::ONE_MINUS_CONSTANT_COLOR: return MTLBlendFactorOneMinusBlendColor;
			case BlendMode::CONSTANT_ALPHA: return MTLBlendFactorBlendAlpha;
			case BlendMode::ONE_MINUS_CONSTANT_ALPHA: return MTLBlendFactorOneMinusBlendAlpha;
			}
			return MTLBlendFactorOne;
		}

		MTLBlendOperation ToBlendOp(BlendFunction function) {
			switch (function) {
			case BlendFunction::SUB: return MTLBlendOperationSubtract;
			case BlendFunction::REVERT_SUB: return MTLBlendOperationReverseSubtract;
			case BlendFunction::MIN: return MTLBlendOperationMin;
			case BlendFunction::MAX: return MTLBlendOperationMax;
			case BlendFunction::ADD: return MTLBlendOperationAdd;
			}
			return MTLBlendOperationAdd;
		}

		MTLColorWriteMask ToColorMask(Color mask) {
			MTLColorWriteMask result = MTLColorWriteMaskNone;
			if ((mask & Color::R) != Color::NONE) result |= MTLColorWriteMaskRed;
			if ((mask & Color::G) != Color::NONE) result |= MTLColorWriteMaskGreen;
			if ((mask & Color::B) != Color::NONE) result |= MTLColorWriteMaskBlue;
			if ((mask & Color::A) != Color::NONE) result |= MTLColorWriteMaskAlpha;
			return result;
		}

		MTLCompareFunction ToCompare(DepthFunction function) {
			switch (function) {
			case DepthFunction::NEVER: return MTLCompareFunctionNever;
			case DepthFunction::EQUAL: return MTLCompareFunctionEqual;
			case DepthFunction::NOT_EQUAL: return MTLCompareFunctionNotEqual;
			case DepthFunction::LESS: return MTLCompareFunctionLess;
			case DepthFunction::GREATER: return MTLCompareFunctionGreater;
			case DepthFunction::LESS_EQUAL: return MTLCompareFunctionLessEqual;
			case DepthFunction::GREATER_EQUAL: return MTLCompareFunctionGreaterEqual;
			case DepthFunction::ALWAYS: return MTLCompareFunctionAlways;
			}
			return MTLCompareFunctionAlways;
		}

		MTLStencilOperation ToStencilOp(StencilOperation operation) {
			switch (operation) {
			case StencilOperation::KEEP: return MTLStencilOperationKeep;
			case StencilOperation::ZERO: return MTLStencilOperationZero;
			case StencilOperation::REPLACE: return MTLStencilOperationReplace;
			case StencilOperation::INCREMENT: return MTLStencilOperationIncrementClamp;
			case StencilOperation::INCREMENT_WRAP: return MTLStencilOperationIncrementWrap;
			case StencilOperation::DECREMENT: return MTLStencilOperationDecrementClamp;
			case StencilOperation::DECREMENT_WRAP: return MTLStencilOperationDecrementWrap;
			case StencilOperation::INVERT: return MTLStencilOperationInvert;
			}
			return MTLStencilOperationKeep;
		}

		MTLCullMode ToCull(CullFace face) {
			switch (face) {
			case CullFace::FRONT:
			case CullFace::FRONT_AND_BACK: return MTLCullModeFront;
			case CullFace::BACK: return MTLCullModeBack;
			case CullFace::NONE: return MTLCullModeNone;
			}
			return MTLCullModeNone;
		}

		const ShaderReflection::Uniform* FindUniform(const ShaderMetal& shader, const std::string& name) {
			const auto& reflection = shader.getReflection();
			if (!reflection.mNameToUniforms.contains(name)) {
				return nullptr;
			}
			return &reflection.mUniforms[reflection.mNameToUniforms.at(name)];
		}
	}

	id<MTLDevice> DeviceMetal::Get() {
		auto* driver = dynamic_cast<DriverMetal*>(DriverInterface::Get());
		return driver ? driver->getDevice() : nil;
	}

	std::unique_ptr<DriverInterface> CreateDriverMetal() {
		return std::make_unique<DriverMetal>();
	}

	DriverMetal::DriverMetal() = default;

	DriverMetal::~DriverMetal() {
		cleanup();
	}

	void DriverMetal::updateDrawableSize(size_t width, size_t height) {
		if (!mLayer) {
			return;
		}
		int pixelsWide = static_cast<int>(width);
		int pixelsHigh = static_cast<int>(height);
		if (auto* window = RESOURCES::ServiceManager::Get<WINDOW::Window>().getSDLWindow()) {
			SDL_GetWindowSizeInPixels(window, &pixelsWide, &pixelsHigh);
		}
		if (pixelsWide <= 0 || pixelsHigh <= 0) {
			return;
		}
		if (mWidth == static_cast<size_t>(pixelsWide) && mHeight == static_cast<size_t>(pixelsHigh)
			&& mLayer.drawableSize.width == pixelsWide && mLayer.drawableSize.height == pixelsHigh) {
			return;
		}
		mWidth = static_cast<size_t>(pixelsWide);
		mHeight = static_cast<size_t>(pixelsHigh);
		mLayer.drawableSize = CGSizeMake(pixelsWide, pixelsHigh);
		mDepthTarget.reset();
		mMsaaColor = nil;
		endPass();
	}

	void DriverMetal::init() {
		mDevice = MTLCreateSystemDefaultDevice();
		if (!mDevice) {
			LOG_ERROR << "Metal: MTLCreateSystemDefaultDevice failed";
			return;
		}
		mCommandQueue = [mDevice newCommandQueue];
		mInFlightSemaphore = dispatch_semaphore_create(1);

		auto& window = RESOURCES::ServiceManager::Get<WINDOW::Window>();
		mLayer = (__bridge CAMetalLayer*)window.getMetalLayer();
		if (!mLayer) {
			LOG_ERROR << "Metal: SDL metal layer is missing";
			return;
		}
		mLayer.device = mDevice;
		mLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		mLayer.framebufferOnly = YES;
		mLayer.maximumDrawableCount = 3;
		updateDrawableSize(window.getSize().x, window.getSize().y);
		begin();
	}

	void DriverMetal::begin() {
		if (!mDevice || !mLayer) {
			return;
		}
		if (!mHoldingFrame) {
			dispatch_semaphore_wait(mInFlightSemaphore, DISPATCH_TIME_FOREVER);
			mHoldingFrame = true;
		}
		mCurrentDrawable = [mLayer nextDrawable];
		if (!mCurrentDrawable) {
			dispatch_semaphore_signal(mInFlightSemaphore);
			mHoldingFrame = false;
			LOG_ERROR << "Metal: nextDrawable returned nil";
			return;
		}
		mCurrentCommandBuffer = [mCommandQueue commandBuffer];
		mCurrentEncoder = nil;
		mPushBytes = 0;
	}

	void DriverMetal::endPass() {
		if (!mCurrentEncoder) {
			return;
		}
		[mCurrentEncoder endEncoding];
		mCurrentEncoder = nil;
	}

	void DriverMetal::end() {
		endPass();
		if (!mCurrentCommandBuffer) {
			return;
		}
		if (mCurrentDrawable) {
			[mCurrentCommandBuffer presentDrawable:mCurrentDrawable];
		}
		if (mHoldingFrame) {
			dispatch_semaphore_t semaphore = mInFlightSemaphore;
			[mCurrentCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
				dispatch_semaphore_signal(semaphore);
			}];
			mHoldingFrame = false;
		}
		[mCurrentCommandBuffer commit];
		mCurrentCommandBuffer = nil;
		mCurrentDrawable = nil;
	}

	void DriverMetal::submit() {
		if (auto* imgui = IKIGAI::IMGUI::Get()) {
			ensurePass();
			if (mCurrentEncoder) {
				imgui->renderDrawData();
			}
		}
		end();
		begin();
	}

	void DriverMetal::cleanup() {
		endPass();
		if (mCurrentCommandBuffer) {
			if (mHoldingFrame) {
				dispatch_semaphore_t semaphore = mInFlightSemaphore;
				[mCurrentCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
					dispatch_semaphore_signal(semaphore);
				}];
				mHoldingFrame = false;
			}
			[mCurrentCommandBuffer commit];
			[mCurrentCommandBuffer waitUntilCompleted];
			mCurrentCommandBuffer = nil;
			mCurrentDrawable = nil;
		} else if (mHoldingFrame) {
			dispatch_semaphore_signal(mInFlightSemaphore);
			mHoldingFrame = false;
		}
		mPipelines.clear();
		mComputePipelines.clear();
		mDepthStates.clear();
		mStageVertexOutput = nil;
		mStagePatchOutput = nil;
		mStageTessFactors = nil;
		mDepthTarget.reset();
		mMsaaColor = nil;
	}

	void DriverMetal::resize(size_t width, size_t height) {
		updateDrawableSize(width, height);
	}

	void DriverMetal::ensureTargets(NSUInteger width, NSUInteger height, NSUInteger samples) {
		if (width == 0 || height == 0) {
			return;
		}
		const bool sameDepth = mDepthTarget && mDepthTarget->getTexture()
			&& mDepthTarget->getWidth() == width && mDepthTarget->getHeight() == height
			&& mDepthTarget->getTexture().sampleCount == samples;
		if (!sameDepth) {
			mDepthTarget = std::make_shared<TextureMetal>(static_cast<uint32_t>(width), static_cast<uint32_t>(height), MTLPixelFormatDepth32Float_Stencil8, samples, true);
		}
		if (samples > 1) {
			const bool sameColor = mMsaaColor && mMsaaColor.width == width && mMsaaColor.height == height && mMsaaColor.sampleCount == samples;
			if (!sameColor) {
				MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
				desc.textureType = MTLTextureType2DMultisample;
				desc.sampleCount = samples;
				desc.storageMode = MTLStorageModePrivate;
				desc.usage = MTLTextureUsageRenderTarget;
				mMsaaColor = [mDevice newTextureWithDescriptor:desc];
			}
		} else {
			mMsaaColor = nil;
		}
		mTargetWidth = width;
		mTargetHeight = height;
		mTargetSamples = samples;
	}

	void DriverMetal::beginPass() {
		if (!mCurrentCommandBuffer) {
			return;
		}
		endPass();

		auto framebuffer = mCurrentState.mFrameBuffer;
		const bool swapchain = !framebuffer;
		if (swapchain && !mCurrentDrawable) {
			return;
		}

		MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
		mColorFormats.clear();
		mDepthFormat = MTLPixelFormatInvalid;
		mDepthHasStencil = false;
		mSampleCount = 1;

		if (swapchain) {
			id<MTLTexture> drawableTexture = mCurrentDrawable.texture;
			const NSUInteger samples = mMSAA ? 4 : 1;
			ensureTargets(drawableTexture.width, drawableTexture.height, samples);
			mPassWidth = drawableTexture.width;
			mPassHeight = drawableTexture.height;
			id<MTLTexture> colorTarget = drawableTexture;
			if (samples > 1 && mMsaaColor) {
				pass.colorAttachments[0].texture = mMsaaColor;
				pass.colorAttachments[0].resolveTexture = drawableTexture;
				pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
				colorTarget = mMsaaColor;
			} else {
				pass.colorAttachments[0].texture = drawableTexture;
				pass.colorAttachments[0].storeAction = MTLStoreActionStore;
			}
			pass.colorAttachments[0].loadAction = mLoadColor;
			pass.colorAttachments[0].clearColor = MTLClearColorMake(mClearColor.x, mClearColor.y, mClearColor.z, mClearColor.w);
			mColorFormats.push_back(drawableTexture.pixelFormat);
			mSampleCount = colorTarget.sampleCount;
		} else {
			const auto& colors = framebuffer->getTextures();
			mPassWidth = static_cast<NSUInteger>(std::max<size_t>(framebuffer->getWidth(), 1));
			mPassHeight = static_cast<NSUInteger>(std::max<size_t>(framebuffer->getHeight(), 1));
			for (size_t index = 0; index < colors.size(); ++index) {
				auto texture = std::static_pointer_cast<TextureMetal>(colors[index]);
				if (!texture || !texture->getTexture()) {
					continue;
				}
				pass.colorAttachments[index].texture = texture->getTexture();
				pass.colorAttachments[index].loadAction = index == 0 ? mLoadColor : MTLLoadActionLoad;
				pass.colorAttachments[index].storeAction = MTLStoreActionStore;
				pass.colorAttachments[index].clearColor = MTLClearColorMake(mClearColor.x, mClearColor.y, mClearColor.z, mClearColor.w);
				mColorFormats.push_back(texture->getMetalFormat());
				mSampleCount = texture->getTexture().sampleCount;
				mPassWidth = texture->getTexture().width;
				mPassHeight = texture->getTexture().height;
			}
		}

		id<MTLTexture> depthTexture = nil;
		if (swapchain) {
			depthTexture = mDepthTarget ? mDepthTarget->getTexture() : nil;
		} else if (framebuffer->getDepth()) {
			auto depth = std::static_pointer_cast<TextureMetal>(framebuffer->getDepth());
			depthTexture = depth ? depth->getTexture() : nil;
		}
		if (depthTexture) {
			mDepthFormat = depthTexture.pixelFormat;
			mDepthHasStencil = MetalFormatHasStencil(mDepthFormat);
			pass.depthAttachment.texture = depthTexture;
			pass.depthAttachment.loadAction = mLoadDepth;
			pass.depthAttachment.storeAction = MTLStoreActionStore;
			pass.depthAttachment.clearDepth = 1.0;
			if (mDepthHasStencil) {
				pass.stencilAttachment.texture = depthTexture;
				pass.stencilAttachment.loadAction = mLoadStencil;
				pass.stencilAttachment.storeAction = MTLStoreActionStore;
				pass.stencilAttachment.clearStencil = 0;
			}
		}

		mLoadColor = MTLLoadActionLoad;
		mLoadDepth = MTLLoadActionLoad;
		mLoadStencil = MTLLoadActionLoad;
		mCurrentEncoder = [mCurrentCommandBuffer renderCommandEncoderWithDescriptor:pass];
	}

	void DriverMetal::ensurePass() {
		if (!mCurrentEncoder) {
			beginPass();
		}
	}

	std::string DriverMetal::pipelineKey() const {
		std::ostringstream key;
		if (mCurrentState.mShader) {
			key << mCurrentState.mShader->getId() << ':' << mCurrentState.mShader->getGeneration();
		} else {
			key << "0";
		}
		key << ":s" << mSampleCount;
		key << ":d" << static_cast<int>(mDepthFormat);
		for (MTLPixelFormat format : mColorFormats) {
			key << ":c" << static_cast<int>(format);
		}
		const size_t stride = mVertexBuffer ? mVertexBuffer->getStride() : 0;
		key << ":v" << stride;
		if (mCurrentState.mShader && mCurrentState.mShader->hasTessellation()) {
			key << ":tess" << mCurrentState.mShader->tessellationControlPoints()
				<< (mCurrentState.mShader->tessellationTriangles() ? "tri" : "quad")
				<< static_cast<int>(mCurrentState.mShader->tessellationPartition());
		}
		if (mCurrentState.mShader && mCurrentState.mShader->hasGeometry()) {
			key << ":geom";
		}
		if (mBlendMode) {
			key << ":b" << static_cast<int>(mBlendMode->mColorSrc) << ',' << static_cast<int>(mBlendMode->mColorDst)
				<< ',' << static_cast<int>(mBlendMode->mAlphaSrc) << ',' << static_cast<int>(mBlendMode->mAlphaDst)
				<< ',' << static_cast<int>(mBlendMode->mColorFunc) << ',' << static_cast<int>(mBlendMode->mAlphaFunc)
				<< ',' << static_cast<int>(mBlendMode->mColorMask);
		} else {
			key << ":noblend";
		}
		return key.str();
	}

	void DriverMetal::ensurePipeline() {
		auto shader = mCurrentState.mShader;
		if (!shader || !mCurrentEncoder) {
			return;
		}
		id<MTLFunction> vertexFunction = shader->rasterVertexFunction();
		if (!vertexFunction) {
			return;
		}
		const std::string key = pipelineKey();
		if (!mPipelines.contains(key)) {
			MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
			desc.vertexFunction = vertexFunction;
			desc.fragmentFunction = shader->getFragmentFunction();
			if (shader->hasTessellation()) {
				desc.tessellationPartitionMode = shader->tessellationPartition();
				desc.tessellationFactorStepFunction = MTLTessellationFactorStepFunctionPerPatch;
				desc.tessellationFactorFormat = MTLTessellationFactorFormatHalf;
				desc.tessellationControlPointIndexType = MTLTessellationControlPointIndexTypeUInt32;
				desc.maxTessellationFactor = 64;
			}
			desc.rasterSampleCount = mSampleCount;
			if (mDepthFormat != MTLPixelFormatInvalid) {
				desc.depthAttachmentPixelFormat = mDepthFormat;
				if (mDepthHasStencil) {
					desc.stencilAttachmentPixelFormat = mDepthFormat;
				}
			}
			const Blending blend = mBlendMode.value_or(Blending(BlendMode::ONE, BlendMode::ZERO));
			const bool blendEnabled = mBlendMode.has_value();
			for (size_t index = 0; index < mColorFormats.size(); ++index) {
				desc.colorAttachments[index].pixelFormat = mColorFormats[index];
				desc.colorAttachments[index].blendingEnabled = blendEnabled ? YES : NO;
				desc.colorAttachments[index].sourceRGBBlendFactor = ToBlendFactor(blend.mColorSrc);
				desc.colorAttachments[index].destinationRGBBlendFactor = ToBlendFactor(blend.mColorDst);
				desc.colorAttachments[index].rgbBlendOperation = ToBlendOp(blend.mColorFunc);
				desc.colorAttachments[index].sourceAlphaBlendFactor = ToBlendFactor(blend.mAlphaSrc);
				desc.colorAttachments[index].destinationAlphaBlendFactor = ToBlendFactor(blend.mAlphaDst);
				desc.colorAttachments[index].alphaBlendOperation = ToBlendOp(blend.mAlphaFunc);
				desc.colorAttachments[index].writeMask = blendEnabled ? ToColorMask(blend.mColorMask) : MTLColorWriteMaskAll;
			}

			const auto& inputs = shader->getReflection().mInputParams;
			if (!shader->hasTessellation() && !inputs.empty()) {
				MTLVertexDescriptor* vertex = [[MTLVertexDescriptor alloc] init];
				NSUInteger packed = 0;
				for (const auto& param : inputs) {
					MTLVertexAttributeDescriptor* attribute = vertex.attributes[param.mLocation];
					attribute.format = ToMetalVertexFormat(param.mFormat);
					attribute.offset = param.mOffset;
					attribute.bufferIndex = kMetalVertexBufferIndex;
					packed = std::max(packed, static_cast<NSUInteger>(param.mOffset + param.mSize));
				}
				const NSUInteger stride = mVertexBuffer && mVertexBuffer->getStride() > 0 ? mVertexBuffer->getStride() : packed;
				vertex.layouts[kMetalVertexBufferIndex].stride = stride;
				vertex.layouts[kMetalVertexBufferIndex].stepFunction = MTLVertexStepFunctionPerVertex;
				vertex.layouts[kMetalVertexBufferIndex].stepRate = 1;
				desc.vertexDescriptor = vertex;
			}

			NSError* error = nil;
			id<MTLRenderPipelineState> pipeline = [mDevice newRenderPipelineStateWithDescriptor:desc error:&error];
			if (!pipeline) {
				const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
				LOG_ERROR << "Metal pipeline failed: " << message;
			} else {
				mPipelines[key] = pipeline;
			}
		}
		if (mPipelines.contains(key)) {
			[mCurrentEncoder setRenderPipelineState:mPipelines[key]];
		}
	}

	void DriverMetal::applyFixedState() {
		if (!mCurrentEncoder) {
			return;
		}
		const float width = static_cast<float>(mPassWidth);
		const float height = static_cast<float>(mPassHeight);
		const Viewport viewport = mViewport.value_or(Viewport{{0.0f, 0.0f}, {width, height}});
		MTLViewport metalViewport;
		metalViewport.originX = viewport.mPosition.x;
		metalViewport.originY = viewport.mPosition.y;
		metalViewport.width = std::max(viewport.mSize.x, 1.0f);
		metalViewport.height = std::max(viewport.mSize.y, 1.0f);
		metalViewport.znear = viewport.mMinDepth;
		metalViewport.zfar = viewport.mMaxDepth;
		[mCurrentEncoder setViewport:metalViewport];

		Scissor scissor = mScissor.value_or(Scissor{{0.0f, 0.0f}, {width, height}});
		NSUInteger x = scissor.mPosition.x > 0.0f ? static_cast<NSUInteger>(scissor.mPosition.x) : 0;
		NSUInteger y = scissor.mPosition.y > 0.0f ? static_cast<NSUInteger>(scissor.mPosition.y) : 0;
		NSUInteger w = scissor.mSize.x > 0.0f ? static_cast<NSUInteger>(scissor.mSize.x) : mPassWidth;
		NSUInteger h = scissor.mSize.y > 0.0f ? static_cast<NSUInteger>(scissor.mSize.y) : mPassHeight;
		if (x >= mPassWidth) x = 0;
		if (y >= mPassHeight) y = 0;
		if (x + w > mPassWidth) w = mPassWidth - x;
		if (y + h > mPassHeight) h = mPassHeight - y;
		if (w == 0 || h == 0) {
			x = 0;
			y = 0;
			w = std::max<NSUInteger>(mPassWidth, 1);
			h = std::max<NSUInteger>(mPassHeight, 1);
		}
		[mCurrentEncoder setScissorRect:MTLScissorRect{x, y, w, h}];
		[mCurrentEncoder setCullMode:ToCull(mCullFace)];
		[mCurrentEncoder setFrontFacingWinding:mTriangleOrientation == TriangleOrientation::CW ? MTLWindingClockwise : MTLWindingCounterClockwise];
		[mCurrentEncoder setTriangleFillMode:mRasterizationMode == RasterizationMode::LINE ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];

		std::ostringstream depthKey;
		if (mDepthMode) {
			depthKey << static_cast<int>(mDepthMode->mFunc) << ':' << mDepthMode->mWriteMask;
		} else {
			depthKey << "off";
		}
		if (mStencilMode) {
			const Stencil& stencil = *mStencilMode;
			depthKey << ":s" << static_cast<int>(stencil.mFunc) << ',' << static_cast<int>(stencil.mFail) << ','
				<< static_cast<int>(stencil.mPass) << ',' << static_cast<int>(stencil.mDepthFail) << ','
				<< static_cast<int>(stencil.mReadMask) << ',' << static_cast<int>(stencil.mWriteMask);
		}
		const std::string depthName = depthKey.str();
		if (!mDepthStates.contains(depthName)) {
			MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
			if (mDepthMode) {
				desc.depthCompareFunction = ToCompare(mDepthMode->mFunc);
				desc.depthWriteEnabled = mDepthMode->mWriteMask ? YES : NO;
			} else {
				desc.depthCompareFunction = MTLCompareFunctionAlways;
				desc.depthWriteEnabled = NO;
			}
			if (mStencilMode && mDepthHasStencil) {
				MTLStencilDescriptor* stencil = [[MTLStencilDescriptor alloc] init];
				stencil.stencilCompareFunction = ToCompare(mStencilMode->mFunc);
				stencil.stencilFailureOperation = ToStencilOp(mStencilMode->mFail);
				stencil.depthFailureOperation = ToStencilOp(mStencilMode->mDepthFail);
				stencil.depthStencilPassOperation = ToStencilOp(mStencilMode->mPass);
				stencil.readMask = mStencilMode->mReadMask;
				stencil.writeMask = mStencilMode->mWriteMask;
				desc.frontFaceStencil = stencil;
				desc.backFaceStencil = stencil;
			}
			mDepthStates[depthName] = [mDevice newDepthStencilStateWithDescriptor:desc];
		}
		[mCurrentEncoder setDepthStencilState:mDepthStates[depthName]];
	}

	void DriverMetal::bindResources() {
		if (!mCurrentEncoder) {
			return;
		}
		if (mVertexBuffer && mVertexBuffer->getBuffer()) {
			[mCurrentEncoder setVertexBuffer:mVertexBuffer->getBuffer() offset:0 atIndex:kMetalVertexBufferIndex];
		}
		for (const auto& [binding, texture] : mTextures) {
			if (!texture || !texture->getTexture()) {
				continue;
			}
			[mCurrentEncoder setVertexTexture:texture->getTexture() atIndex:binding];
			[mCurrentEncoder setFragmentTexture:texture->getTexture() atIndex:binding];
			if (texture->getSampler()) {
				[mCurrentEncoder setVertexSamplerState:texture->getSampler() atIndex:binding];
				[mCurrentEncoder setFragmentSamplerState:texture->getSampler() atIndex:binding];
			}
		}
		auto bindBuffer = [&](size_t binding, id<MTLBuffer> buffer) {
			if (!buffer) {
				return;
			}
			const NSUInteger index = MetalBufferIndexForBinding(static_cast<uint32_t>(binding));
			[mCurrentEncoder setVertexBuffer:buffer offset:0 atIndex:index];
			[mCurrentEncoder setFragmentBuffer:buffer offset:0 atIndex:index];
		};
		for (const auto& [binding, buffer] : mUniformBuffers) {
			bindBuffer(binding, buffer ? buffer->getBuffer() : nil);
		}
		for (const auto& [binding, buffer] : mStorageBuffers) {
			bindBuffer(binding, buffer ? buffer->getBuffer() : nil);
		}
		if (mPushBytes > 0) {
			const NSUInteger length = std::max<NSUInteger>((mPushBytes + 3u) & ~3u, 4u);
			[mCurrentEncoder setVertexBytes:mPushConstants.data() length:length atIndex:kMetalPushConstantBufferIndex];
			[mCurrentEncoder setFragmentBytes:mPushConstants.data() length:length atIndex:kMetalPushConstantBufferIndex];
		}
	}

	void DriverMetal::setPrimitiveMode(PrimitiveMode topology) { mPrimitiveMode = topology; }
	void DriverMetal::setRasterization(RasterizationMode mode) { mRasterizationMode = mode; }
	void DriverMetal::setViewport(const Viewport& viewport) { mViewport = viewport; }
	void DriverMetal::resetViewport() { mViewport.reset(); }
	void DriverMetal::setScissor(const Scissor& scissor) { mScissor = scissor; }
	void DriverMetal::resetScissor() { mScissor.reset(); }

	void DriverMetal::setShader(std::shared_ptr<ShaderInterface> shader) {
		mCurrentState.mShader = std::static_pointer_cast<ShaderMetal>(shader);
	}

	void DriverMetal::setVertexBuffer(std::shared_ptr<VertexBufferInterface> buffer) {
		mVertexBuffer = std::static_pointer_cast<VertexBufferMetal>(buffer);
	}

	void DriverMetal::setIndexBuffer(std::shared_ptr<IndexBufferInterface> buffer) {
		mIndexBuffer = std::static_pointer_cast<IndexBufferMetal>(buffer);
	}

	void DriverMetal::setBlending(const Blending& value) { mBlendMode = value; }
	void DriverMetal::resetBlending() { mBlendMode.reset(); }
	void DriverMetal::setDepth(const Depth& depth) { mDepthMode = depth; }
	void DriverMetal::resetDepth() { mDepthMode.reset(); }
	void DriverMetal::setStencil(const Stencil& stencil) { mStencilMode = stencil; }
	void DriverMetal::resetStencil() { mStencilMode.reset(); }
	void DriverMetal::setCull(CullFace cull_mode) { mCullFace = cull_mode; }
	void DriverMetal::setTriangleOrientation(TriangleOrientation value) { mTriangleOrientation = value; }

	void DriverMetal::clear(bool clearColor, bool clearDepth, bool clearStencil) {
		mLoadColor = clearColor ? MTLLoadActionClear : MTLLoadActionLoad;
		mLoadDepth = clearDepth ? MTLLoadActionClear : MTLLoadActionLoad;
		mLoadStencil = clearStencil ? MTLLoadActionClear : MTLLoadActionLoad;
		endPass();
		beginPass();
	}

	id<MTLBuffer> DriverMetal::ensureBuffer(id<MTLBuffer> slot, NSUInteger length) {
		if (length < 16) {
			length = 16;
		}
		if (slot && slot.length >= length) {
			return slot;
		}
		return [mDevice newBufferWithLength:length options:MTLResourceStorageModePrivate];
	}

	id<MTLComputePipelineState> DriverMetal::computePipeline(id<MTLFunction> function) {
		if (!function || !mDevice) {
			return nil;
		}
		const std::string key = std::to_string(reinterpret_cast<uintptr_t>((__bridge void*)function));
		if (!mComputePipelines.contains(key)) {
			NSError* error = nil;
			id<MTLComputePipelineState> state = [mDevice newComputePipelineStateWithFunction:function error:&error];
			if (!state) {
				const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
				LOG_ERROR << "Metal compute pipeline failed: " << message;
				return nil;
			}
			mComputePipelines[key] = state;
		}
		return mComputePipelines[key];
	}

	void DriverMetal::bindComputeResources(id<MTLComputeCommandEncoder> encoder) {
		if (!encoder) {
			return;
		}
		for (const auto& [binding, texture] : mTextures) {
			if (!texture || !texture->getTexture()) {
				continue;
			}
			[encoder setTexture:texture->getTexture() atIndex:binding];
			if (texture->getSampler()) {
				[encoder setSamplerState:texture->getSampler() atIndex:binding];
			}
		}
		for (const auto& [binding, buffer] : mUniformBuffers) {
			if (buffer && buffer->getBuffer()) {
				[encoder setBuffer:buffer->getBuffer() offset:0 atIndex:MetalBufferIndexForBinding(static_cast<uint32_t>(binding))];
			}
		}
		for (const auto& [binding, buffer] : mStorageBuffers) {
			if (buffer && buffer->getBuffer()) {
				[encoder setBuffer:buffer->getBuffer() offset:0 atIndex:MetalBufferIndexForBinding(static_cast<uint32_t>(binding))];
			}
		}
		if (mPushBytes > 0) {
			const NSUInteger length = std::max<NSUInteger>((mPushBytes + 3u) & ~3u, 4u);
			[encoder setBytes:mPushConstants.data() length:length atIndex:kMetalPushConstantBufferIndex];
		}
	}

	void DriverMetal::encodePreDraw(uint32_t count, bool indexed) {
		auto shader = mCurrentState.mShader;
		if (!shader || !mCurrentCommandBuffer || count == 0) {
			return;
		}
		id<MTLFunction> geometry = shader->getGeometryFunction();
		const bool geometryKernel = geometry && geometry.functionType == MTLFunctionTypeKernel;
		if (!shader->hasTessellation() && !geometryKernel) {
			return;
		}

		endPass();
		id<MTLComputeCommandEncoder> compute = [mCurrentCommandBuffer computeCommandEncoder];
		const auto dispatch = [&](NSUInteger threads) {
			const NSUInteger group = std::min<NSUInteger>(64, std::max<NSUInteger>(threads, 1));
			[compute dispatchThreads:MTLSizeMake(threads, 1, 1) threadsPerThreadgroup:MTLSizeMake(group, 1, 1)];
		};

		if (shader->hasTessellation()) {
			const uint32_t controlPoints = std::max(1u, shader->tessellationControlPoints());
			const uint32_t patches = count / controlPoints;
			if (patches > 0) {
				const NSUInteger outputVertices = count;
				mStageVertexOutput = ensureBuffer(mStageVertexOutput, outputVertices * 256);
				mStagePatchOutput = ensureBuffer(mStagePatchOutput, static_cast<NSUInteger>(patches) * 256);
				mStageTessFactors = ensureBuffer(mStageTessFactors, static_cast<NSUInteger>(patches) * sizeof(MTLQuadTessellationFactorsHalf));
				const uint32_t params[4] = { controlPoints, patches, static_cast<uint32_t>(outputVertices), 0 };

				if (auto state = computePipeline(shader->getTessellationVertexFunction(indexed))) {
					[compute setComputePipelineState:state];
					bindComputeResources(compute);
					if (mVertexBuffer && mVertexBuffer->getBuffer()) {
						[compute setBuffer:mVertexBuffer->getBuffer() offset:0 atIndex:kMetalShaderInputBufferIndex];
					}
					if (indexed && mIndexBuffer && mIndexBuffer->getBuffer()) {
						[compute setBuffer:mIndexBuffer->getBuffer() offset:0 atIndex:kMetalShaderIndexBufferIndex];
					}
					[compute setBuffer:mStageVertexOutput offset:0 atIndex:kMetalShaderOutputBufferIndex];
					[compute setBytes:params length:sizeof(params) atIndex:kMetalIndirectParamsBufferIndex];
					dispatch(outputVertices);
				}
				if (auto state = computePipeline(shader->getTessellationControlFunction())) {
					[compute setComputePipelineState:state];
					bindComputeResources(compute);
					[compute setBuffer:mStageVertexOutput offset:0 atIndex:kMetalShaderInputBufferIndex];
					[compute setBuffer:mStagePatchOutput offset:0 atIndex:kMetalPatchOutputBufferIndex];
					[compute setBuffer:mStageTessFactors offset:0 atIndex:kMetalTessFactorBufferIndex];
					[compute setBytes:params length:sizeof(params) atIndex:kMetalIndirectParamsBufferIndex];
					dispatch(static_cast<NSUInteger>(patches) * controlPoints);
				}
			}
		}

		if (geometryKernel) {
			if (auto state = computePipeline(geometry)) {
				[compute setComputePipelineState:state];
				bindComputeResources(compute);
				if (mVertexBuffer && mVertexBuffer->getBuffer()) {
					[compute setBuffer:mVertexBuffer->getBuffer() offset:0 atIndex:kMetalVertexBufferIndex];
					[compute setBuffer:mVertexBuffer->getBuffer() offset:0 atIndex:kMetalShaderInputBufferIndex];
				}
				if (indexed && mIndexBuffer && mIndexBuffer->getBuffer()) {
					[compute setBuffer:mIndexBuffer->getBuffer() offset:0 atIndex:kMetalShaderIndexBufferIndex];
				}
				dispatch(count);
			}
		}
		[compute endEncoding];
	}

	void DriverMetal::setClearColor(const MATH::Vector4f& color) { mClearColor = color; }
	void DriverMetal::setClearColor(float r, float g, float b, float a) { mClearColor = {r, g, b, a}; }

	void DriverMetal::draw(uint32_t count, uint32_t offset, uint32_t instance) {
		if (count == 0 || instance == 0) {
			return;
		}
		encodePreDraw(count, false);
		ensurePass();
		ensurePipeline();
		applyFixedState();
		bindResources();
		if (!mCurrentEncoder) {
			return;
		}
		auto shader = mCurrentState.mShader;
		if (shader && shader->hasTessellation() && mStageTessFactors) {
			const NSUInteger controlPoints = std::max<NSUInteger>(shader->tessellationControlPoints(), 1);
			const NSUInteger patches = count / controlPoints;
			if (patches == 0) {
				return;
			}
			[mCurrentEncoder setVertexBuffer:mStageVertexOutput offset:0 atIndex:kMetalShaderInputBufferIndex];
			[mCurrentEncoder setVertexBuffer:mStagePatchOutput offset:0 atIndex:kMetalShaderPatchInputBufferIndex];
			[mCurrentEncoder setVertexBuffer:mStageTessFactors offset:0 atIndex:kMetalTessFactorBufferIndex];
			[mCurrentEncoder setTessellationFactorBuffer:mStageTessFactors offset:0 instanceStride:0];
			[mCurrentEncoder drawPatches:controlPoints patchStart:0 patchCount:patches patchIndexBuffer:nil patchIndexBufferOffset:0 instanceCount:instance baseInstance:0];
			return;
		}
		[mCurrentEncoder drawPrimitives:ToPrimitive(mPrimitiveMode) vertexStart:offset vertexCount:count instanceCount:instance];
	}

	void DriverMetal::drawIndexed(uint32_t count, uint32_t offset, uint32_t instance) {
		if (count == 0 || instance == 0 || !mIndexBuffer || !mIndexBuffer->getBuffer()) {
			return;
		}
		encodePreDraw(count, true);
		ensurePass();
		ensurePipeline();
		applyFixedState();
		bindResources();
		if (!mCurrentEncoder) {
			return;
		}
		auto shader = mCurrentState.mShader;
		if (shader && shader->hasTessellation() && mStageTessFactors) {
			const NSUInteger controlPoints = std::max<NSUInteger>(shader->tessellationControlPoints(), 1);
			const NSUInteger patches = count / controlPoints;
			if (patches == 0) {
				return;
			}
			[mCurrentEncoder setVertexBuffer:mStageVertexOutput offset:0 atIndex:kMetalShaderInputBufferIndex];
			[mCurrentEncoder setVertexBuffer:mStagePatchOutput offset:0 atIndex:kMetalShaderPatchInputBufferIndex];
			[mCurrentEncoder setVertexBuffer:mStageTessFactors offset:0 atIndex:kMetalTessFactorBufferIndex];
			[mCurrentEncoder setTessellationFactorBuffer:mStageTessFactors offset:0 instanceStride:0];
			[mCurrentEncoder drawIndexedPatches:controlPoints
									 patchStart:0
									 patchCount:patches
							   patchIndexBuffer:nil
						 patchIndexBufferOffset:0
						controlPointIndexBuffer:mIndexBuffer->getBuffer()
				  controlPointIndexBufferOffset:static_cast<NSUInteger>(offset) * mIndexBuffer->getStride()
								  instanceCount:instance
								   baseInstance:0];
			return;
		}
		const NSUInteger stride = mIndexBuffer->getStride() == 0 ? sizeof(uint32_t) : mIndexBuffer->getStride();
		const MTLIndexType indexType = stride == sizeof(uint16_t) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
		[mCurrentEncoder drawIndexedPrimitives:ToPrimitive(mPrimitiveMode)
									indexCount:count
									 indexType:indexType
								   indexBuffer:mIndexBuffer->getBuffer()
							 indexBufferOffset:static_cast<NSUInteger>(offset) * stride
								 instanceCount:instance];
	}

	void DriverMetal::draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances) {
		if (instances == 0) {
			return;
		}
		setPrimitiveMode(primitive);
		mesh.bind();
		if (mesh.getIndexCount() > 0) {
			drawIndexed(static_cast<uint32_t>(mesh.getIndexCount()), 0, instances);
		} else {
			draw(static_cast<uint32_t>(mesh.getVertexCount()), 0, instances);
		}
		mesh.unbind();
	}

	void DriverMetal::setPushConstant(ShaderType, uint32_t offset, uint32_t size, const void* data) {
		if (!data || size == 0 || offset + size > mPushConstants.size()) {
			return;
		}
		std::memcpy(mPushConstants.data() + offset, data, size);
		mPushBytes = std::max(mPushBytes, offset + size);
	}

	void DriverMetal::setTexture(size_t bind, std::shared_ptr<TextureInterface> data) {
		mTextures[bind] = std::static_pointer_cast<TextureMetal>(data);
	}

	void DriverMetal::setUniformBuffer(size_t bind, std::shared_ptr<UniformBufferInterface> data) {
		mUniformBuffers[bind] = std::static_pointer_cast<UniformBufferMetal>(data);
	}

	void DriverMetal::setStorageBuffer(size_t bind, std::shared_ptr<StorageBufferInterface> data) {
		mStorageBuffers[bind] = std::static_pointer_cast<StorageBufferMetal>(data);
	}

	void DriverMetal::setTexture(const std::string& name, std::shared_ptr<TextureInterface> data) {
		if (!mCurrentState.mShader) {
			return;
		}
		if (const auto* uniform = FindUniform(*mCurrentState.mShader, name)) {
			setTexture(uniform->mBind, data);
		}
	}

	void DriverMetal::setUniformBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> data) {
		if (!mCurrentState.mShader) {
			return;
		}
		if (const auto* uniform = FindUniform(*mCurrentState.mShader, name)) {
			setUniformBuffer(uniform->mBind, data);
		}
	}

	void DriverMetal::setStorageBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> data) {
		if (!mCurrentState.mShader) {
			return;
		}
		if (const auto* uniform = FindUniform(*mCurrentState.mShader, name)) {
			setStorageBuffer(uniform->mBind, data);
		}
	}

	void DriverMetal::setMSAA(bool value) {
		if (mMSAA == value) {
			return;
		}
		mMSAA = value;
		mMsaaColor = nil;
		mDepthTarget.reset();
		endPass();
	}

	std::shared_ptr<UniformBufferInterface> DriverMetal::createUniformBuffer(const void* data, size_t size) {
		auto buffer = std::make_shared<UniformBufferMetal>(size);
		if (data) {
			buffer->setData(data, size);
		}
		return buffer;
	}

	std::shared_ptr<StorageBufferInterface> DriverMetal::createStorageBuffer(const void* data, size_t size, size_t stride) {
		auto buffer = std::make_shared<StorageBufferMetal>(size, stride);
		if (data) {
			buffer->setData(data, size, stride);
		}
		return buffer;
	}

	std::shared_ptr<TextureInterface> DriverMetal::createTexture(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
		TextureResource descriptor;
		descriptor.path = path;
		descriptor.useMipmap = generateMipmap;
		descriptor.pathTexture.push_back(path);
		return TextureMetal::Create(descriptor, allocator, deleter);
	}

	std::shared_ptr<TextureInterface> DriverMetal::createTextureAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
		return TextureAtlasMetal::CreateAtlas(path, generateMipmap, allocator, deleter);
	}

	std::shared_ptr<TextureInterface> DriverMetal::createTexture(const TextureResource& res, UTILS::IAllocator* allocator, TextureDeleter deleter) {
		return TextureMetal::Create(res, allocator, deleter);
	}

	std::shared_ptr<TextureInterface> DriverMetal::createTexture(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
		return TextureMetal::Create(res, fileData, allocator, deleter);
	}

	std::shared_ptr<TextureInterface> DriverMetal::createTextureAtlas(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
		return TextureAtlasMetal::CreateAtlasFromResource(res, fileData, allocator, deleter);
	}

	std::shared_ptr<TextureInterface> DriverMetal::createTexture(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
		TextureResource res;
		res.path = name;
		res.useMipmap = generateMipmap;
		res.pathTexture.push_back(name);
		return TextureMetal::Create(res, std::vector<std::vector<uint8_t>>{data}, allocator, deleter);
	}

	std::shared_ptr<ShaderInterface> DriverMetal::createShader(const std::string& vertexPath, const std::string& fragmentPath) {
		return std::make_shared<ShaderMetal>(vertexPath, fragmentPath);
	}

	std::shared_ptr<ShaderInterface> DriverMetal::createShader(const ShaderResource& res, UTILS::IAllocator* allocator, ShaderDeleter deleter) {
		return AllocateShader<ShaderMetal>(allocator, deleter, res);
	}

	std::shared_ptr<ModelInterface> DriverMetal::createModel(const std::string& path, UTILS::IAllocator* allocator, ModelDeleter deleter) {
		ModelDeleter finalDeleter = [deleter](ModelInterface* model) {
			RESOURCES::ServiceManager::Get<RESOURCES::ModelLoader>().unloadResource(model->getPath());
			if (deleter) {
				deleter(model);
			}
		};
		return AllocateModel<ModelMetal>(allocator, std::move(finalDeleter), path);
	}

	std::shared_ptr<MaterialInterface> DriverMetal::createMaterial(const MaterialResource& res, UTILS::IAllocator* allocator, MaterialDeleter deleter) {
		MaterialDeleter finalDeleter = [deleter](MaterialInterface* material) {
			RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().unloadResource(material->getPath());
			if (deleter) {
				deleter(material);
			}
		};
		return AllocateMaterial<MaterialMetal>(allocator, std::move(finalDeleter), res);
	}

	std::shared_ptr<FrameBufferInterface> DriverMetal::createFrameBuffer(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth) {
		return std::make_shared<FrameBufferMetal>(textures, depth);
	}

	void DriverMetal::setFrameBuffer(std::shared_ptr<FrameBufferInterface> frameBuffer) {
		if (mCurrentState.mFrameBuffer == frameBuffer) {
			return;
		}
		mCurrentState.mFrameBuffer = frameBuffer;
		endPass();
	}

	void DriverMetal::resetFrameBuffer() {
		if (!mCurrentState.mFrameBuffer) {
			return;
		}
		mCurrentState.mFrameBuffer.reset();
		endPass();
	}

}
#endif
