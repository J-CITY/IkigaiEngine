#include "driverGl.h"
#include "renderModule/backends/driverFactory.h"

#ifdef OPENGL_BACKEND
#include "../interface/meshInterface.h"
#include "frameBufferGl.h"
#include "indexBufferGl.h"
#include "materialGl.h"
#include "shaderGl.h"
#include "storageBufferGl.h"
#include "textureGl.h"
#include "uniformBufferGl.h"
#include <coreModule/graphicsWrapper.hpp>
#include <iostream>
#include <string>

#include "modelGl.h"

constexpr GLenum FrontFaceTable[] = { GL_CW, GL_CCW };

constexpr GLenum BlendFuncTable[] = { GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX };

constexpr GLenum PrimitiveModeTable[] = { GL_POINTS, GL_LINES, GL_LINE_LOOP, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN,
#ifndef USING_GLES
	GL_LINES_ADJACENCY, GL_LINE_STRIP_ADJACENCY, GL_TRIANGLES_ADJACENCY, GL_TRIANGLE_STRIP_ADJACENCY, GL_PATCHES
#endif
};
// constexpr GLenum RenderingCapabilityTable[] = {
//	GL_BLEND,
//	GL_CULL_FACE,
//	GL_DEPTH_TEST,
//	GL_DITHER,
//	GL_POLYGON_OFFSET_FILL,
//	GL_SAMPLE_ALPHA_TO_COVERAGE,
//	GL_SAMPLE_COVERAGE,
//	GL_SCISSOR_TEST,
//	GL_STENCIL_TEST,
// #ifndef USING_GLES
//	GL_MULTISAMPLE
// #endif
// };
#ifndef USING_GLES
constexpr GLenum RasterizationModeTable[] = { GL_POINT, GL_LINE, GL_FILL };
#endif

constexpr GLenum ComparaisonAlgorithmTable[] = { GL_EQUAL, GL_NOTEQUAL, GL_LESS, GL_GREATER, GL_LEQUAL, GL_GEQUAL, GL_ALWAYS, GL_NEVER };

constexpr GLenum OperationTable[] = { GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_INCR_WRAP, GL_DECR, GL_DECR_WRAP, GL_INVERT };
constexpr GLenum CullFaceTable[] = { GL_NONE, GL_FRONT, GL_BACK, GL_FRONT_AND_BACK };
static_assert(sizeof(CullFaceTable) / sizeof(GLenum) == static_cast<size_t>(IKIGAI::RENDER::CullFace::FRONT_AND_BACK) + 1);
// constexpr GLenum PixelDataTypeTable[] = {
//	GL_BYTE,
//	GL_UNSIGNED_BYTE,
// #ifndef USING_GLES
//	GL_BITMAP,
// #endif
//	GL_SHORT,
//	GL_UNSIGNED_SHORT,
//	GL_INT,
//	GL_UNSIGNED_INT,
//	GL_FLOAT,
// #ifndef USING_GLES
//	GL_UNSIGNED_BYTE_3_3_2,
//	GL_UNSIGNED_BYTE_2_3_3_REV,
//	GL_UNSIGNED_SHORT_5_6_5,
//	GL_UNSIGNED_SHORT_5_6_5_REV,
//	GL_UNSIGNED_SHORT_4_4_4_4,
//	GL_UNSIGNED_SHORT_4_4_4_4_REV,
//	GL_UNSIGNED_SHORT_5_5_5_1,
//	GL_UNSIGNED_SHORT_1_5_5_5_REV,
//	GL_UNSIGNED_INT_8_8_8_8,
//	GL_UNSIGNED_INT_8_8_8_8_REV,
//	GL_UNSIGNED_INT_10_10_10_2,
//	GL_UNSIGNED_INT_2_10_10_10_REV
// #endif
// };
// constexpr GLenum PixelDataFormatTable[] = {
////#ifndef USING_GLES
//	GL_COLOR_INDEX,
//	GL_STENCIL_INDEX,
////#endif
//	GL_DEPTH_COMPONENT,
////#ifndef USING_GLES
//	GL_RED,
//	GL_GREEN,
//	GL_BLUE,
////#endif
//	GL_ALPHA,
//	GL_RGB,
////#ifndef USING_GLES
//	GL_BGR,
////#endif
//	GL_RGBA,
////#ifndef USING_GLES
//	GL_BGRA,
////#endif
//	GL_LUMINANCE,
//	GL_LUMINANCE_ALPHA
//};

constexpr GLenum DepthFunctionTable[] = {
	GL_EQUAL,
	GL_NOTEQUAL,
	GL_LESS,
	GL_GREATER,
	GL_LEQUAL,
	GL_GEQUAL,
	GL_ALWAYS,
	GL_NEVER,
};

constexpr GLenum BlendTable[] = {
	0x0,
	GL_ZERO,
	GL_ONE,
	GL_SRC_COLOR,
	GL_ONE_MINUS_SRC_COLOR,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_DST_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_DST_COLOR,
	GL_ONE_MINUS_DST_COLOR,
	GL_CONSTANT_COLOR,
	GL_ONE_MINUS_CONSTANT_COLOR,
	GL_CONSTANT_ALPHA,
	GL_ONE_MINUS_CONSTANT_ALPHA,
};
static_assert(sizeof(BlendTable) / sizeof(GLenum) == static_cast<size_t>(IKIGAI::RENDER::BlendMode::ONE_MINUS_CONSTANT_ALPHA) + 1);

IKIGAI::RENDER::DriverGl::DriverGl() {
	DriverGl::init();
}

IKIGAI::RENDER::DriverGl::~DriverGl() {
	begin();
	mBindlessTables.clear();
	mVertexStreams.clear();
	// TODO: delete context
}

void IKIGAI::RENDER::DriverGl::initGlew() {
#if defined(_WIN32) || defined(WIN32) || (defined(__linux__) && !defined(__ANDROID__))
	glewExperimental = GL_TRUE;
	const GLenum error = glewInit();
	if (error != GLEW_OK) {
		std::string message = "GlManager::ERROR init GLEW: ";
		std::string glewError = reinterpret_cast<const char*>(glewGetErrorString(error));
		// LOG_INFO(message + glewError);
	}
#endif
}

void IKIGAI::RENDER::DriverGl::init() {
	initGlew();
#ifndef USING_GLES
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
#endif

#if (defined(BEDUG) || defined(_DEBUG)) && !defined(__APPLE__) && !defined(USING_GLES)
	if (glDebugMessageCallback) {
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(GLDebugMessageCallback, nullptr);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
	}
#endif

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glCullFace(GL_BACK);
}

// TODO: Remove it
void IKIGAI::RENDER::DriverGl::setVertexBuffer(std::shared_ptr<VertexBufferInterface> buffer) {
	resetVertexStreams();
	mVertexBuffer = std::static_pointer_cast<VertexBufferGl>(buffer);
}
// TODO: Remove it
void IKIGAI::RENDER::DriverGl::setIndexBuffer(std::shared_ptr<IndexBufferInterface> buffer) {
	mIndexBuffer = std::static_pointer_cast<IndexBufferGl>(buffer);
}

void IKIGAI::RENDER::DriverGl::setShader(std::shared_ptr<ShaderInterface> shader) {
	mShader = std::static_pointer_cast<ShaderGl>(shader);
}

void IKIGAI::RENDER::DriverGl::onResize() {
	setDirty(Dirty::VIEWPORT);
	setDirty(Dirty::SCISSOR);
}

void IKIGAI::RENDER::DriverGl::resize(size_t width, size_t height) {
	mWidth = width;
	mHeight = height;
	glViewport(0, 0, (GLsizei)width, (GLsizei)height);
}

// TODO: add support for FB
void IKIGAI::RENDER::DriverGl::setMSAA(bool value) {
	mMSAA = value;
#ifndef USING_GLES
	if (value) {
		glEnable(GL_MULTISAMPLE);
	}
	else {
		glDisable(GL_MULTISAMPLE);
	}
#endif
}

void IKIGAI::RENDER::DriverGl::setPushConstant(ShaderType stage, uint32_t offset, uint32_t size, const void* data) {
	if (mShader) {
		if (size == sizeof(MATH::Matrix4f)) {
			mShader->setMat4("engine_Model.model", *reinterpret_cast<const MATH::Matrix4f*>(static_cast<const uint8_t*>(data) + offset));
		}
	}
}

void IKIGAI::RENDER::DriverGl::setPrimitiveMode(PrimitiveMode param) {
	mPrimitiveMode = param;
	setDirty(Dirty::PRIMITIVE_MODE);
}

void IKIGAI::RENDER::DriverGl::setRasterization(RasterizationMode param) {
	mRasterization = param;
	setDirty(Dirty::RASTERIZATION_MODE);
}

void IKIGAI::RENDER::DriverGl::setViewport(const Viewport& param) {
	mViewport = param;
	setDirty(Dirty::VIEWPORT);
}

void IKIGAI::RENDER::DriverGl::resetViewport() {
	mViewport = std::nullopt;
	setDirty(Dirty::VIEWPORT);
}

void IKIGAI::RENDER::DriverGl::setScissor(const Scissor& param) {
	mScissor = param;
	setDirty(Dirty::SCISSOR);
}

void IKIGAI::RENDER::DriverGl::resetScissor() {
	mScissor = std::nullopt;
	setDirty(Dirty::SCISSOR);
}

void IKIGAI::RENDER::DriverGl::setBlend(const Blending& param) {
	setDirty(Dirty::BLENDING);
	mBlend = param;
}

void IKIGAI::RENDER::DriverGl::resetBlend() {
	setDirty(Dirty::BLENDING);
	mBlend = std::nullopt;
}

void IKIGAI::RENDER::DriverGl::setDepth(const Depth& param) {
	setDirty(Dirty::DEPTH);
	mDepth = param;
}

void IKIGAI::RENDER::DriverGl::resetDepth() {
	mDepth = std::nullopt;
	setDirty(Dirty::DEPTH);
}

void IKIGAI::RENDER::DriverGl::setStencil(const Stencil& param) {
	mStencil = param;
}

void IKIGAI::RENDER::DriverGl::resetStencil() {
	mStencil = std::nullopt;
	setDirty(Dirty::STENCIL);
}

void IKIGAI::RENDER::DriverGl::setCull(CullFace param) {
	setDirty(Dirty::CULLING);
	mCullFace = param;
}

void IKIGAI::RENDER::DriverGl::setClearColor(const MATH::Vector4f& color) {
	mClearColor = color;
}

void IKIGAI::RENDER::DriverGl::setClearColor(float r, float g, float b, float a) {
	mClearColor = { r, g, b, a };
}

void IKIGAI::RENDER::DriverGl::clear(bool clearColor, bool clearDepth, bool clearStencil) {
	const auto hasScissor = glIsEnabled(GL_SCISSOR_TEST);

	if (hasScissor) {
		glDisable(GL_SCISSOR_TEST);
	}

	GLbitfield flags = 0;

	if (clearColor) {
		flags |= GL_COLOR_BUFFER_BIT;
		glClearColor(mClearColor.r, mClearColor.g, mClearColor.b, mClearColor.a);
	}
	if (clearDepth) {
		flags |= GL_DEPTH_BUFFER_BIT;
		glClearDepthf(mDepthValue);
	}
	if (clearStencil) {
		flags |= GL_STENCIL_BUFFER_BIT;
		glClearStencil(mStencilValue);
	}
	GLboolean depthMask;
	GLboolean colorMask[4];
	glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
	if (clearColor) {
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
	if (clearDepth) {
		glDepthMask(GL_TRUE);
	}
	glClear(flags);
	if (clearColor) {
		glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
	}
	if (clearDepth) {
		glDepthMask(depthMask);
	}

	if (hasScissor) {
		glEnable(GL_SCISSOR_TEST);
	}
}

std::vector<float> IKIGAI::RENDER::DriverGl::readTexture(std::shared_ptr<TextureInterface> texture, unsigned mip, unsigned layer) {
#if !defined(USING_GLES)
	auto t = std::static_pointer_cast<TextureGl>(texture);
	const size_t w = std::max<size_t>(1, t->getWidth() >> mip), h = std::max<size_t>(1, t->getHeight() >> mip);
	const bool depth = t->getType() == TextureType::DEPTH;
	const auto target = t->getType() == TextureType::TEXTURE_CUBE ? GL_TEXTURE_CUBE_MAP
		: t->getType() == TextureType::TEXTURE_3D				  ? GL_TEXTURE_3D
																  : GL_TEXTURE_2D;
	std::vector<float> values(w * h * (depth ? 1 : 4) * (target == GL_TEXTURE_3D ? std::max<size_t>(1, t->getDepth() >> mip) : 1));
	glBindTexture(target, t->id);
	glGetTexImage(target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer : target, mip, depth ? GL_DEPTH_COMPONENT : GL_RGBA,
		GL_FLOAT, values.data());
	if (target == GL_TEXTURE_3D) {
		const auto count = w * h * 4;
		return { values.begin() + layer * count, values.begin() + (layer + 1) * count };
	}
	return values;
#else
	throw std::runtime_error("Readback requires desktop OpenGL");
#endif
}

void IKIGAI::RENDER::DriverGl::dispatch(uint32_t x, uint32_t y, uint32_t z) {
#if !defined(USING_GLES) && !defined(__APPLE__)
	applyState();
	for (const auto& u : mShader->getReflection().mUniforms) {
		if (u.mType == ShaderReflection::UniformType::IMAGE_3D || u.mType == ShaderReflection::UniformType::IMAGE_2D) {
			const auto t = mTextures.at(u.mBind);
			glBindImageTexture(u.mBind, t->id, 0, t->getType() == TextureType::TEXTURE_3D, 0, GL_READ_WRITE, GL_RGBA32F);
		}
	}
	glDispatchCompute(x, y, z);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
#else
	throw std::runtime_error("Compute pipeline requires desktop OpenGL 4.3+");
#endif
}

void IKIGAI::RENDER::DriverGl::draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count) {
	if (!vertex_count || !instance_count) {
		return;
	}
	validateVertexInputs(*mShader);
	validateVertexDraw({ vertex_count, vertex_offset, instance_count });
	applyState();
	glDrawArraysInstanced(PrimitiveModeTable[static_cast<size_t>(mPrimitiveMode)], vertex_offset, vertex_count, instance_count);
}

void IKIGAI::RENDER::DriverGl::drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count) {
	if (!index_count || !instance_count) {
		return;
	}
	if (!mIndexBuffer) {
		throw std::runtime_error("Index buffer missing");
	}
	applyState();
	auto index_size = mIndexBuffer->getStride();
	auto index_type = index_size == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
	if (index_offset > mIndexBuffer->getSize() || index_count > mIndexBuffer->getSize() - index_offset) {
		throw std::runtime_error("Indexed draw exceeds index buffer");
	}
	validateVertexInputs(*mShader);
	validateVertexDraw({ index_count, index_offset, instance_count, 0, 0, true });
	glDrawElementsInstanced(PrimitiveModeTable[static_cast<size_t>(mPrimitiveMode)], index_count, index_type,
		reinterpret_cast<const void*>(uintptr_t(index_offset) * index_size), instance_count);
}

void IKIGAI::RENDER::DriverGl::draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances) {
	if (instances > 0) {
		applyState();
		mFrameInfo.mBatchCount++;
		mFrameInfo.mInstanceCount += instances;
		mFrameInfo.mPolyCount += (mesh.getIndexCount() / 3) * instances;

		mesh.bind();
		validateVertexInputs(*mShader);
		validateVertexDraw({ static_cast<uint32_t>(mesh.getIndexCount() ? mesh.getIndexCount() : mesh.getVertexCount()), 0, instances, 0, 0,
			mesh.getIndexCount() > 0 });
		if (mesh.getIndexCount() > 0) {
			// EBO
			if (instances == 1) {
				glDrawElements(PrimitiveModeTable[static_cast<GLenum>(primitive)], mesh.getIndexCount(), GL_UNSIGNED_INT, nullptr);
			}
			else {
				// #ifndef USING_GLES
				glDrawElementsInstanced(
					PrimitiveModeTable[static_cast<GLenum>(primitive)], mesh.getIndexCount(), GL_UNSIGNED_INT, nullptr, instances);
				// #endif
			}
		}
		else {
			// EBO
			if (instances == 1) {
				// glPatchParameteri(GL_PATCH_VERTICES, 4);
				// glDrawArrays(GL_PATCHES, 0, 4 * 20 * 20);
				glDrawArrays(PrimitiveModeTable[static_cast<GLenum>(primitive)], 0, mesh.getVertexCount());
			}
			else {
				// #ifndef USING_GLES
				glDrawArraysInstanced(PrimitiveModeTable[static_cast<GLenum>(primitive)], 0, mesh.getVertexCount(), instances);
				// #endif
			}
		}
		mesh.unbind();
	}
}

void IKIGAI::RENDER::DriverGl::setTexture(size_t bind, std::shared_ptr<TextureInterface> data) {
	mTextures[bind] = std::static_pointer_cast<TextureGl>(data);
}

void IKIGAI::RENDER::DriverGl::setUniformBuffer(size_t bind, std::shared_ptr<UniformBufferInterface> data) {
	mUniformBuffers[bind] = std::static_pointer_cast<UniformBufferGl>(data);
}

void IKIGAI::RENDER::DriverGl::setStorageBuffer(size_t bind, std::shared_ptr<StorageBufferInterface> data) {
	mStorageBuffers[bind] = std::static_pointer_cast<StorageBufferGl>(data);
}

void IKIGAI::RENDER::DriverGl::setTexture(const std::string& name, std::shared_ptr<TextureInterface> data) {
	const auto& reflection = mShader->getReflection();
	const auto bind = reflection.mUniforms[reflection.mNameToUniforms.at(name)].mBind;
	mTextures[bind] = std::static_pointer_cast<TextureGl>(data);
}

void IKIGAI::RENDER::DriverGl::setUniformBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> data) {
	const auto& reflection = mShader->getReflection();
	const auto bind = reflection.mUniforms[reflection.mNameToUniforms.at(name)].mBind;
	mUniformBuffers[bind] = std::static_pointer_cast<UniformBufferGl>(data);
}

void IKIGAI::RENDER::DriverGl::setStorageBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> data) {
	const auto& reflection = mShader->getReflection();
	const auto bind = reflection.mUniforms[reflection.mNameToUniforms.at(name)].mBind;
	mStorageBuffers[bind] = std::static_pointer_cast<StorageBufferGl>(data);
}

std::shared_ptr<IKIGAI::RENDER::UniformBufferInterface> IKIGAI::RENDER::DriverGl::createUniformBuffer(const void* data, size_t size) {
	return std::make_shared<UniformBufferGl>(data, size);
}

std::shared_ptr<IKIGAI::RENDER::StorageBufferInterface> IKIGAI::RENDER::DriverGl::createStorageBuffer(const void* data, size_t size, size_t stride) {
	return std::make_shared<StorageBufferGl>(data, size, stride);
}

std::shared_ptr<IKIGAI::RENDER::TextureInterface> IKIGAI::RENDER::DriverGl::createTexture(
	const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureGl::Create(path, generateMipmap, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::TextureInterface> IKIGAI::RENDER::DriverGl::createTextureAtlas(
	const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureAtlasGl::CreateAtlas(path, generateMipmap, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::TextureInterface> IKIGAI::RENDER::DriverGl::createTexture(
	const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureGl::CreateFromMemory(name, data, generateMipmap, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::TextureInterface> IKIGAI::RENDER::DriverGl::createTexture(
	const TextureResource& res, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureGl::Create(res, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::TextureInterface> IKIGAI::RENDER::DriverGl::createTexture(
	const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureGl::Create(res, fileData, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::TextureInterface> IKIGAI::RENDER::DriverGl::createTextureAtlas(
	const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureAtlasGl::CreateAtlasFromResource(res, fileData, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::ShaderInterface> IKIGAI::RENDER::DriverGl::createShader(
	const std::string& vertexPath, const std::string& fragmentPath) {
	std::map<ShaderType, std::string> paths;
	paths[ShaderType::VERTEX] = vertexPath;
	paths[ShaderType::FRAGMENT] = fragmentPath;
	return ShaderGl::CreateFromPath(paths);
}

std::shared_ptr<IKIGAI::RENDER::ShaderInterface> IKIGAI::RENDER::DriverGl::createShader(
	const ShaderResource& res, UTILS::IAllocator* allocator, ShaderDeleter deleter) {
	return AllocateShader<ShaderGl>(allocator, deleter, res);
}

std::shared_ptr<IKIGAI::RENDER::ModelInterface> IKIGAI::RENDER::DriverGl::createModel(
	const std::string& path, UTILS::IAllocator* allocator, ModelDeleter deleter) {
	return AllocateModel<ModelGl>(allocator, std::move(deleter), path);
}

std::shared_ptr<IKIGAI::RENDER::MaterialInterface> IKIGAI::RENDER::DriverGl::createMaterial(
	const MaterialResource& res, UTILS::IAllocator* allocator, MaterialDeleter deleter) {
	return AllocateMaterial<MaterialGl>(allocator, std::move(deleter), res);
}

std::shared_ptr<IKIGAI::RENDER::FrameBufferInterface> IKIGAI::RENDER::DriverGl::createFrameBuffer(
	const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth, unsigned mip, unsigned layer) {
	return std::make_shared<FrameBufferGl>(textures, depth, mip, layer);
}

void IKIGAI::RENDER::DriverGl::applyState() {
	if (mShader) {
		mShader->bind();
		bindBindlessResources();
	}
	// Bind only resources declared by this shader; stale storage bindings are not sampled.
	if (mShader) {
		for (const auto& u : mShader->getReflection().mUniforms) {
			if (!mShader->isBindlessResource(u)
				&& (u.mType == ShaderReflection::UniformType::SAMPLER_2D || u.mType == ShaderReflection::UniformType::SAMPLER_CUBE
					|| u.mType == ShaderReflection::UniformType::SAMPLER_3D || u.mType == ShaderReflection::UniformType::SAMPLER_2D_ARRAY)) {
				auto it = mTextures.find(u.mBind);
				if (it != mTextures.end() && it->second) {
					it->second->bind(static_cast<int>(u.mBind));
				}
			}
		}
	}

	if (isDirty(Dirty::DEPTH)) {
		clearDirty(Dirty::DEPTH);
		if (mDepth) {
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(ComparaisonAlgorithmTable[static_cast<unsigned>(mDepth->mFunc)]);
			glDepthMask(mDepth->mWriteMask);
		}
		else {
			glDisable(GL_DEPTH_TEST);
			glDepthMask(true);
		}
	}

	if (isDirty(Dirty::STENCIL)) {
		clearDirty(Dirty::STENCIL);
		if (mStencil) {
			glEnable(GL_STENCIL_TEST);
			glStencilMask(mStencil->mWriteMask);
			glStencilOp(OperationTable[static_cast<unsigned>(mStencil->mFail)], OperationTable[static_cast<unsigned>(mStencil->mDepthFail)],
				OperationTable[static_cast<unsigned>(mStencil->mPass)]);
			glStencilFunc(DepthFunctionTable[static_cast<unsigned>(mStencil->mFunc)], 1, mStencil->mReadMask);
		}
		else {
			glDisable(GL_STENCIL_TEST);
		}
	}

	if (isDirty(Dirty::BLENDING)) {
		clearDirty(Dirty::BLENDING);
		if (mBlend) {
			glEnable(GL_BLEND);
			glBlendEquationSeparate(
				BlendFuncTable[static_cast<unsigned>(mBlend->mColorFunc)], BlendFuncTable[static_cast<unsigned>(mBlend->mAlphaFunc)]);
			glBlendFuncSeparate(BlendTable[static_cast<unsigned>(mBlend->mColorSrc)], BlendTable[static_cast<unsigned>(mBlend->mColorDst)],
				BlendTable[static_cast<unsigned>(mBlend->mAlphaSrc)], BlendTable[static_cast<unsigned>(mBlend->mAlphaDst)]);
			glColorMask(static_cast<bool>(mBlend->mColorMask & Color::R), static_cast<bool>(mBlend->mColorMask & Color::G),
				static_cast<bool>(mBlend->mColorMask & Color::B), static_cast<bool>(mBlend->mColorMask & Color::A));
		}
		else {
			glDisable(GL_BLEND);
		}
	}

	if (isDirty(Dirty::CULLING)) {
		clearDirty(Dirty::CULLING);
		if (mCullFace == CullFace::NONE) {
			glDisable(GL_CULL_FACE);
		}
		else {
			glEnable(GL_CULL_FACE);
			glCullFace(CullFaceTable[static_cast<unsigned>(mCullFace)]);
		}
	}

	if (isDirty(Dirty::RASTERIZATION_MODE)) {
		clearDirty(Dirty::RASTERIZATION_MODE);
#ifndef USING_GLES
		glPolygonMode(GL_FRONT_AND_BACK, RasterizationModeTable[static_cast<unsigned>(mRasterization)]);
#endif
	}

	if (isDirty(Dirty::TRIANGULATION_ORDER)) {
		clearDirty(Dirty::TRIANGULATION_ORDER);
		glFrontFace(FrontFaceTable[static_cast<unsigned>(mTriangleOrientation)]);
	}

	if (isDirty(Dirty::VIEWPORT)) {
		clearDirty(Dirty::VIEWPORT);

		auto viewport = mViewport.value_or(Viewport { { 0.0f, 0.0f }, { (float)mWidth, (float)mHeight } });

		glViewport((GLint)viewport.mPosition.x, (GLint)viewport.mPosition.y, (GLint)viewport.mSize.x, (GLint)viewport.mSize.y);

		glDepthRangef((GLfloat)viewport.mMinDepth, (GLfloat)viewport.mMaxDepth);
	}

	if (isDirty(Dirty::SCISSOR)) {
		clearDirty(Dirty::SCISSOR);

		if (mScissor) {
			auto value = mScissor.value();

			glEnable(GL_SCISSOR_TEST);
			glScissor((GLint)glm::round(value.mPosition.x),
				(GLint)glm::round(mHeight - value.mPosition.y - value.mSize.y), // TODO: need different calculations
																				// when render target
				(GLint)glm::round(value.mSize.x), (GLint)glm::round(value.mSize.y));
		}
		else {
			glDisable(GL_SCISSOR_TEST);
		}
	}

	for (auto& [bind, uniform] : mUniformBuffers) {
		glBindBufferBase(GL_UNIFORM_BUFFER, bind, uniform->getId());
	}
	for (auto& [bind, storage] : mStorageBuffers) {
#if defined(IKIGAI_GL_HAS_SSBO)
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bind, storage->getId());
#elif defined(GL_UNIFORM_BUFFER)
		glBindBufferBase(GL_UNIFORM_BUFFER, bind, storage->getId());
#endif
	}
}

void IKIGAI::RENDER::DriverGl::submit() {
	end();
	begin();
}

void IKIGAI::RENDER::DriverGl::end() {
	requireOwnerThread();
	const auto token = completionSource()->submitted();
	glFinish();
	completionSource()->completed(token);
}

void IKIGAI::RENDER::DriverGl::waitForCompletion(GpuCompletionToken token) {
	requireOwnerThread();
	completionSource()->validate(token);
	if (!completionSource()->isComplete(token)) {
		glFinish();
		completionSource()->completed(completionSource()->lastSubmitted());
	}
}
void IKIGAI::RENDER::DriverGl::setFrameBuffer(std::shared_ptr<FrameBufferInterface> frameBuffer) {
	if (frameBuffer) {
		std::static_pointer_cast<FrameBufferGl>(frameBuffer)->bind();
	}
	else {
		resetFrameBuffer();
	}
}
void IKIGAI::RENDER::DriverGl::resetFrameBuffer() {
	BindDefaultFramebufferGl();
}

#if !defined(OCULUS) && !defined(__APPLE__)
void APIENTRY IKIGAI::RENDER::DriverGl::GLDebugMessageCallback(
	uint32_t source, uint32_t type, uint32_t id, uint32_t severity, int32_t length, const char* message, const void* userParam) {
#ifndef USING_GLES
	if (type != GL_DEBUG_TYPE_ERROR && type != GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR) {
		return;
	}
#endif
	if (id == 131169 || id == 131185 || id == 131218 || id == 131204) {
		return;
	}

	std::string output;

	output += "OpenGL Debug Message:\n";
	output += "Debug message (" + std::to_string(id) + "): " + message + "\n";
#ifndef USING_GLES
	switch (source) {
	case GL_DEBUG_SOURCE_API:
		output += "Source: API";
		break;
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
		output += "Source: Window System";
		break;
	case GL_DEBUG_SOURCE_SHADER_COMPILER:
		output += "Source: Shader Compiler";
		break;
	case GL_DEBUG_SOURCE_THIRD_PARTY:
		output += "Source: Third Party";
		break;
	case GL_DEBUG_SOURCE_APPLICATION:
		output += "Source: Application";
		break;
	case GL_DEBUG_SOURCE_OTHER:
		output += "Source: Other";
		break;
	}

	output += "\n";

	switch (type) {
	case GL_DEBUG_TYPE_ERROR:
		output += "Type: Error";
		break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
		output += "Type: Deprecated Behaviour";
		break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
		output += "Type: Undefined Behaviour";
		break;
	case GL_DEBUG_TYPE_PORTABILITY:
		output += "Type: Portability";
		break;
	case GL_DEBUG_TYPE_PERFORMANCE:
		output += "Type: Performance";
		break;
	case GL_DEBUG_TYPE_MARKER:
		output += "Type: Marker";
		break;
	case GL_DEBUG_TYPE_PUSH_GROUP:
		output += "Type: Push Group";
		break;
	case GL_DEBUG_TYPE_POP_GROUP:
		output += "Type: Pop Group";
		break;
	case GL_DEBUG_TYPE_OTHER:
		output += "Type: Other";
		break;
	}

	output += "\n";

	switch (severity) {
	case GL_DEBUG_SEVERITY_HIGH:
		output += "Severity: High";
		break;
	case GL_DEBUG_SEVERITY_MEDIUM:
		output += "Severity: Medium";
		break;
	case GL_DEBUG_SEVERITY_LOW:
		output += "Severity: Low";
		break;
	case GL_DEBUG_SEVERITY_NOTIFICATION:
		output += "Severity: Notification";
		break;
	}

	switch (severity) {
	case GL_DEBUG_SEVERITY_HIGH:
		std::cout << (output);
		break;
	case GL_DEBUG_SEVERITY_MEDIUM:
		std::cout << (output);
		break;
	case GL_DEBUG_SEVERITY_LOW:
		std::cout << (output);
		break;
	case GL_DEBUG_SEVERITY_NOTIFICATION:
		std::cout << (output);
		break;
	}
#endif
}
#endif

void IKIGAI::RENDER::DriverGl::draw(const DrawArguments& args) {
	if (!args.count || !args.instanceCount) {
		return;
	}
	validateVertexDraw(args);
	validateVertexInputs(*mShader);
	applyState();
	if (args.firstInstance || args.baseVertex) {
#if !defined(USING_GLES) && !defined(__APPLE__)
		if (!GLEW_VERSION_4_2 && !GLEW_ARB_base_instance) {
			throw std::runtime_error("Base instance draws require GL 4.2 or ARB_base_instance");
		}
		if (args.indexed) {
			if (!mIndexBuffer) {
				throw std::runtime_error("Index buffer missing");
			}
			const auto stride = mIndexBuffer->getStride();
			glDrawElementsInstancedBaseVertexBaseInstance(PrimitiveModeTable[static_cast<size_t>(mPrimitiveMode)], args.count,
				stride == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, reinterpret_cast<const void*>(uintptr_t(args.first) * stride), args.instanceCount,
				args.baseVertex, args.firstInstance);
		}
		else {
			glDrawArraysInstancedBaseInstance(
				PrimitiveModeTable[static_cast<size_t>(mPrimitiveMode)], args.first, args.count, args.instanceCount, args.firstInstance);
		}
#else
		throw std::runtime_error("Base instance/vertex requires desktop OpenGL");
#endif
	}
	else {
		if (args.indexed) {
			drawIndexed(args.count, args.first, args.instanceCount);
		}
		else {
			draw(args.count, args.first, args.instanceCount);
		}
	}
}

IKIGAI::RENDER::BindlessCapabilities IKIGAI::RENDER::DriverGl::bindlessCapabilities() const {
#if !defined(USING_GLES) && !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
	if (GLEW_ARB_bindless_texture && GLEW_NV_gpu_shader5) {
		return { true, false, false, true, false, 128, 0 };
	}
#endif
	return {};
}
uint32_t IKIGAI::RENDER::DriverGl::maxFragmentSampledTextures() const {
	GLint count = 0;
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &count);
	return count > 0 ? static_cast<uint32_t>(count) : 0;
}
bool IKIGAI::RENDER::DriverGl::supportsVertexPulling() const {
#if defined(IKIGAI_GL_HAS_SSBO)
#if defined(USING_GLES)
	return true;
#else
	return GLEW_VERSION_4_3;
#endif
#else
	return false;
#endif
}
void IKIGAI::RENDER::DriverGl::begin() {
#if !defined(USING_GLES) && !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
	if (!mBindlessResidentHandles.empty()) {
		glFinish();
		for (auto handle : mBindlessResidentHandles) {
			glMakeTextureHandleNonResidentARB(handle);
		}
		mBindlessResidentHandles.clear();
	}
#endif
	mBindlessFrameResources.clear();
}
void IKIGAI::RENDER::DriverGl::bindBindlessResources() {
#if !defined(USING_GLES) && !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
	if (!mShader || mShader->bindingMode() != ShaderBindingMode::Bindless) {
		return;
	}
	for (const auto& uniform : mShader->getReflection().mUniforms) {
		if (mShader->isBindlessResource(uniform)) {
			const auto count = mShader->descriptorCount(uniform);
			const auto& table = bindlessTable(uniform, count);
			std::vector<GLuint64> handles;
			for (uint32_t i = 0; i < count; ++i) {
				const auto entry = table.get(i);
				mBindlessFrameResources.push_back(entry);
				const auto texture = std::static_pointer_cast<TextureGl>(entry.texture);
				const GLuint64 handle = glGetTextureHandleARB(texture->id);
				if (std::find(mBindlessResidentHandles.begin(), mBindlessResidentHandles.end(), handle) == mBindlessResidentHandles.end()) {
					glMakeTextureHandleResidentARB(handle);
					mBindlessResidentHandles.push_back(handle);
				}
				handles.push_back(handle);
			}
			const auto location = glGetUniformLocation(mShader->getId(), (uniform.mName + "[0]").c_str());
			if (location < 0) {
				throw std::runtime_error("Bindless sampler array uniform is inactive: " + uniform.mName);
			}
			glUniformHandleui64vARB(location, count, handles.data());
			const auto error = glGetError();
			if (error != GL_NO_ERROR) {
				throw std::runtime_error("Bindless GL uniform update failed: " + std::to_string(error));
			}
		}
	}
#endif
}
namespace {
const bool kRegisterGlBackend = [] {
	IKIGAI::RENDER::RegisterRenderBackend(IKIGAI::RENDER::RenderSettings::Backend::OPENGL,
		[] { return std::make_unique<IKIGAI::RENDER::DriverGl>(); });
	return true;
}();
}
#endif
