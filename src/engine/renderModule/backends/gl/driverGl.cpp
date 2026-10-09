#include "driverGl.h"

#ifdef OPENGL_BACKEND
#include "../interface/meshInterface.h"
#include "frameBufferGl.h"
#include "indexBufferGl.h"
#include "shaderGl.h"
#include "storageBufferGl.h"
#include "materialGl.h"
#include "textureGl.h"
#include "uniformBufferGl.h"
#include <coreModule/graphicsWrapper.hpp>
#include <iostream>
#include <string>

#include "modelGl.h"

constexpr GLenum FrontFaceTable[] = {GL_CW, GL_CCW};

constexpr GLenum BlendFuncTable[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT,
                                     GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX};

constexpr GLenum PrimitiveModeTable[] = {GL_POINTS,
                                         GL_LINES,
                                         GL_LINE_LOOP,
                                         GL_LINE_STRIP,
                                         GL_TRIANGLES,
                                         GL_TRIANGLE_STRIP,
                                         GL_TRIANGLE_FAN,
#ifndef USING_GLES
                                         GL_LINES_ADJACENCY,
                                         GL_LINE_STRIP_ADJACENCY,
                                         GL_TRIANGLES_ADJACENCY,
                                         GL_TRIANGLE_STRIP_ADJACENCY,
                                         GL_PATCHES
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
constexpr GLenum RasterizationModeTable[] = {GL_POINT, GL_LINE, GL_FILL};
#endif

constexpr GLenum ComparaisonAlgorithmTable[] = {
    GL_EQUAL, GL_NOTEQUAL,   GL_LESS, GL_GREATER, GL_LEQUAL,
    GL_GEQUAL, GL_ALWAYS, GL_NEVER};

constexpr GLenum OperationTable[] = {GL_KEEP,      GL_ZERO,      GL_REPLACE,
                                     GL_INCR,      GL_INCR_WRAP, GL_DECR,
                                     GL_DECR_WRAP, GL_INVERT};
constexpr GLenum CullFaceTable[] = {GL_FRONT, GL_BACK, GL_FRONT_AND_BACK};
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
    GL_EQUAL,  GL_NOTEQUAL, GL_LESS,   GL_GREATER,
    GL_LEQUAL, GL_GEQUAL,   GL_ALWAYS, GL_NEVER,
};

constexpr GLenum BlendTable[] = {
    0x0,
    GL_ZERO,
    GL_ONE,
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

IKIGAI::RENDER::DriverGl::DriverGl() { DriverGl::init(); }

IKIGAI::RENDER::DriverGl::~DriverGl() {
  // TODO: delete context
}

void IKIGAI::RENDER::DriverGl::initGlew() {
#if defined(_WIN32) || defined(WIN32) || (defined(__linux__) && !defined(__ANDROID__))
  glewExperimental = GL_TRUE;
  const GLenum error = glewInit();
  if (error != GLEW_OK) {
    std::string message = "GlManager::ERROR init GLEW: ";
    std::string glewError =
        reinterpret_cast<const char *>(glewGetErrorString(error));
    // LOG_INFO(message + glewError);
  }
#endif
}

void IKIGAI::RENDER::DriverGl::init() {
  initGlew();

#if (defined(BEDUG) || defined(_DEBUG)) && !defined(__APPLE__)
  GLint flags;
  glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
  if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(GLDebugMessageCallback, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr,
                          GL_TRUE);
  }
#endif

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glCullFace(GL_BACK);
}

// TODO: Remove it
void IKIGAI::RENDER::DriverGl::setVertexBuffer(
    std::shared_ptr<VertexBufferInterface> buffer) {
  mVertexBuffer = std::static_pointer_cast<VertexBufferGl>(buffer);
}
// TODO: Remove it
void IKIGAI::RENDER::DriverGl::setIndexBuffer(
    std::shared_ptr<IndexBufferInterface> buffer) {
  mIndexBuffer = std::static_pointer_cast<IndexBufferGl>(buffer);
}

void IKIGAI::RENDER::DriverGl::setShader(
    std::shared_ptr<ShaderInterface> shader) {
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
  } else {
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

void IKIGAI::RENDER::DriverGl::setViewport(const Viewport &param) {
  mViewport = param;
  setDirty(Dirty::VIEWPORT);
}

void IKIGAI::RENDER::DriverGl::resetViewport() {
  mViewport = std::nullopt;
  setDirty(Dirty::VIEWPORT);
}

void IKIGAI::RENDER::DriverGl::setScissor(const Scissor &param) {
  mScissor = param;
  setDirty(Dirty::SCISSOR);
}

void IKIGAI::RENDER::DriverGl::resetScissor() {
  mViewport = std::nullopt;
  setDirty(Dirty::SCISSOR);
}

void IKIGAI::RENDER::DriverGl::setBlend(const Blending &param) {
  mBlend = param;
}

void IKIGAI::RENDER::DriverGl::resetBlend() { mBlend = std::nullopt; }

void IKIGAI::RENDER::DriverGl::setDepth(const Depth &param)
{
    setDirty(Dirty::DEPTH);
	mDepth = param;
}

void IKIGAI::RENDER::DriverGl::resetDepth() { mDepth = std::nullopt; }

void IKIGAI::RENDER::DriverGl::setStencil(const Stencil &param) {
  mStencil = param;
}

void IKIGAI::RENDER::DriverGl::resetStencil() { mStencil = std::nullopt; }

void IKIGAI::RENDER::DriverGl::setCull(CullFace param) { mCullFace = param; }

void IKIGAI::RENDER::DriverGl::setClearColor(const MATH::Vector4f &color) {
  mClearColor = color;
}

void IKIGAI::RENDER::DriverGl::setClearColor(float r, float g, float b,
                                             float a) {
  mClearColor = {r, g, b, a};
}

void IKIGAI::RENDER::DriverGl::clear(bool clearColor, bool clearDepth,
                                     bool clearStencil) {
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
  glClear(flags);

  if (hasScissor) {
    glEnable(GL_SCISSOR_TEST);
  }
}

void IKIGAI::RENDER::DriverGl::draw(uint32_t vertex_count,
                                    uint32_t vertex_offset,
                                    uint32_t instance_count) {
  applyState();
  glDrawArraysInstanced(PrimitiveModeTable[static_cast<size_t>(mPrimitiveMode)],
                        vertex_offset, vertex_count, instance_count);
}

void IKIGAI::RENDER::DriverGl::drawIndexed(uint32_t index_count,
                                           uint32_t index_offset,
                                           uint32_t instance_count) {
  applyState();
  auto index_size = mIndexBuffer->getStride();
  auto index_type = index_size == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
  // glDrawElementsInstanced(PrimitiveModeTable[static_cast<size_t>(mPrimitiveMode)],
  // index_count, index_type, (void*)(index_offset * index_size),
  // instance_count);
  glDrawElements(PrimitiveModeTable[static_cast<size_t>(mPrimitiveMode)],
                 index_count, index_type, nullptr);
}

void IKIGAI::RENDER::DriverGl::draw(const MeshInterface &mesh,
                                    PrimitiveMode primitive,
                                    uint32_t instances) {
  if (instances > 0) {
    applyState();
    mFrameInfo.mBatchCount++;
    mFrameInfo.mInstanceCount += instances;
    mFrameInfo.mPolyCount += (mesh.getIndexCount() / 3) * instances;

    mesh.bind();
    if (mesh.getIndexCount() > 0) {
      // EBO
      if (instances == 1) {
        glDrawElements(PrimitiveModeTable[static_cast<GLenum>(primitive)],
                       mesh.getIndexCount(), GL_UNSIGNED_INT, nullptr);
      } else {
        // #ifndef USING_GLES
        glDrawElementsInstanced(
            PrimitiveModeTable[static_cast<GLenum>(primitive)],
            mesh.getIndexCount(), GL_UNSIGNED_INT, nullptr, instances);
        // #endif
      }
    } else {
      // EBO
      if (instances == 1) {
        // glPatchParameteri(GL_PATCH_VERTICES, 4);
        // glDrawArrays(GL_PATCHES, 0, 4 * 20 * 20);
        glDrawArrays(PrimitiveModeTable[static_cast<GLenum>(primitive)], 0,
                     mesh.getVertexCount());
      } else {
        // #ifndef USING_GLES
        glDrawArraysInstanced(
            PrimitiveModeTable[static_cast<GLenum>(primitive)], 0,
            mesh.getVertexCount(), instances);
        // #endif
      }
    }
    mesh.unbind();
  }
}

void IKIGAI::RENDER::DriverGl::setTexture(
    size_t bind, std::shared_ptr<TextureInterface> data) {
  mTextures[bind] = std::static_pointer_cast<TextureGl>(data);
}

void IKIGAI::RENDER::DriverGl::setUniformBuffer(
    size_t bind, std::shared_ptr<UniformBufferInterface> data) {
  mUniformBuffers[bind] = std::static_pointer_cast<UniformBufferGl>(data);
}

void IKIGAI::RENDER::DriverGl::setStorageBuffer(
    size_t bind, std::shared_ptr<StorageBufferInterface> data) {
  mStorageBuffers[bind] = std::static_pointer_cast<StorageBufferGl>(data);
}

void IKIGAI::RENDER::DriverGl::setTexture(
    const std::string &name, std::shared_ptr<TextureInterface> data) {
  const auto &reflection = mShader->getReflection();
  const auto bind =
      reflection.mUniforms[reflection.mNameToUniforms.at(name)].mBind;
  mTextures[bind] = std::static_pointer_cast<TextureGl>(data);
}

void IKIGAI::RENDER::DriverGl::setUniformBuffer(
    const std::string &name, std::shared_ptr<UniformBufferInterface> data) {
  const auto &reflection = mShader->getReflection();
  const auto bind =
      reflection.mUniforms[reflection.mNameToUniforms.at(name)].mBind;
  mUniformBuffers[bind] = std::static_pointer_cast<UniformBufferGl>(data);
}

void IKIGAI::RENDER::DriverGl::setStorageBuffer(
    const std::string &name, std::shared_ptr<StorageBufferInterface> data) {
  const auto &reflection = mShader->getReflection();
  const auto bind =
      reflection.mUniforms[reflection.mNameToUniforms.at(name)].mBind;
  mStorageBuffers[bind] = std::static_pointer_cast<StorageBufferGl>(data);
}

std::shared_ptr<IKIGAI::RENDER::UniformBufferInterface>
IKIGAI::RENDER::DriverGl::createUniformBuffer(const void *data, size_t size) {
  return std::make_shared<UniformBufferGl>(data, size);
}

std::shared_ptr<IKIGAI::RENDER::StorageBufferInterface>
IKIGAI::RENDER::DriverGl::createStorageBuffer(const void *data, size_t size,
                                              size_t stride) {
  return std::make_shared<StorageBufferGl>(data, size, stride);
}

std::shared_ptr<IKIGAI::RENDER::TextureInterface> IKIGAI::RENDER::DriverGl::createTexture(const std::string &path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureGl::Create(path, generateMipmap, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::TextureInterface> IKIGAI::RENDER::DriverGl::createTextureAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
    return TextureAtlasGl::CreateAtlas(path, generateMipmap, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::TextureInterface> IKIGAI::RENDER::DriverGl::createTexture(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
    return TextureGl::CreateFromMemory(name, data, generateMipmap, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::TextureInterface> IKIGAI::RENDER::DriverGl::createTexture(const TextureResource &res, UTILS::IAllocator* allocator, TextureDeleter deleter) {
  return TextureGl::Create(res, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::TextureInterface> IKIGAI::RENDER::DriverGl::createTexture(const TextureResource &res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
    return TextureGl::Create(res, fileData, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::TextureInterface> IKIGAI::RENDER::DriverGl::createTextureAtlas(const TextureResource &res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
    return TextureAtlasGl::CreateAtlasFromResource(res, fileData, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::ShaderInterface>
IKIGAI::RENDER::DriverGl::createShader(const std::string &vertexPath,
                                       const std::string &fragmentPath) {
  std::map<ShaderType, std::string> paths;
  paths[ShaderType::VERTEX] = vertexPath;
  paths[ShaderType::FRAGMENT] = fragmentPath;
  return ShaderGl::CreateFromPath(paths);
}

std::shared_ptr<IKIGAI::RENDER::ShaderInterface>
IKIGAI::RENDER::DriverGl::createShader(const ShaderResource &res, UTILS::IAllocator* allocator, ShaderDeleter deleter) {
	return AllocateShader<ShaderGl>(allocator, deleter, res);
}

std::shared_ptr<IKIGAI::RENDER::ModelInterface>
IKIGAI::RENDER::DriverGl::createModel(const std::string& path, UTILS::IAllocator* allocator, ModelDeleter deleter) {
	return AllocateModel<ModelGl>(allocator, std::move(deleter), path);
}

std::shared_ptr<IKIGAI::RENDER::MaterialInterface>
IKIGAI::RENDER::DriverGl::createMaterial(const MaterialResource& res, UTILS::IAllocator* allocator, MaterialDeleter deleter) {
	return AllocateMaterial<MaterialGl>(allocator, std::move(deleter), res);
}

std::shared_ptr<IKIGAI::RENDER::FrameBufferInterface>
IKIGAI::RENDER::DriverGl::createFrameBuffer(
    const std::vector<std::shared_ptr<TextureInterface>> &textures,
    std::shared_ptr<TextureInterface> depth) {
  return std::make_shared<FrameBufferGl>(textures, depth);
}

void IKIGAI::RENDER::DriverGl::applyState() {
  if (mShader) {
    // mShader->bind();
  }

  if (isDirty(Dirty::DEPTH)) {
    clearDirty(Dirty::DEPTH);
    if (mDepth) {
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(
          ComparaisonAlgorithmTable[static_cast<unsigned>(mDepth->mFunc)]);
      glDepthMask(mDepth->mWriteMask);
    } else {
      glDisable(GL_DEPTH_TEST);
      glDepthMask(true);
    }
  }

  if (isDirty(Dirty::STENCIL)) {
    clearDirty(Dirty::STENCIL);
    if (mStencil) {
      glEnable(GL_STENCIL_TEST);
      glStencilMask(mStencil->mWriteMask);
      glStencilOp(OperationTable[static_cast<unsigned>(mStencil->mFail)],
                  OperationTable[static_cast<unsigned>(mStencil->mDepthFail)],
                  OperationTable[static_cast<unsigned>(mStencil->mPass)]);
      glStencilFunc(DepthFunctionTable[static_cast<unsigned>(mStencil->mFunc)],
                    1, mStencil->mReadMask);
    } else {
      glDisable(GL_STENCIL_TEST);
    }
  }

  if (isDirty(Dirty::BLENDING)) {
    clearDirty(Dirty::BLENDING);
    if (mBlend) {
      glEnable(GL_BLEND);
      glBlendEquationSeparate(
          BlendFuncTable[static_cast<unsigned>(mBlend->mColorFunc)],
          BlendFuncTable[static_cast<unsigned>(mBlend->mAlphaFunc)]);
      glBlendFuncSeparate(BlendTable[static_cast<unsigned>(mBlend->mColorSrc)],
                          BlendTable[static_cast<unsigned>(mBlend->mColorDst)],
                          BlendTable[static_cast<unsigned>(mBlend->mAlphaSrc)],
                          BlendTable[static_cast<unsigned>(mBlend->mAlphaDst)]);
      glColorMask(static_cast<bool>(mBlend->mColorMask & Color::R),
                  static_cast<bool>(mBlend->mColorMask & Color::B),
                  static_cast<bool>(mBlend->mColorMask & Color::B),
                  static_cast<bool>(mBlend->mColorMask & Color::A));
    } else {
      glDisable(GL_BLEND);
    }
  }

  if (isDirty(Dirty::CULLING)) {
    clearDirty(Dirty::CULLING);
    if (mCullFace == CullFace::NONE) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
      glCullFace(CullFaceTable[static_cast<unsigned>(mCullFace)]);
    }
  }

  if (isDirty(Dirty::RASTERIZATION_MODE)) {
    clearDirty(Dirty::RASTERIZATION_MODE);
#ifndef USING_GLES
    glPolygonMode(
        GL_FRONT_AND_BACK,
        RasterizationModeTable[static_cast<unsigned>(mRasterization)]);
#endif
  }

  if (isDirty(Dirty::TRIANGULATION_ORDER)) {
    clearDirty(Dirty::TRIANGULATION_ORDER);
    glFrontFace(FrontFaceTable[static_cast<unsigned>(mTriangleOrientation)]);
  }

  if (isDirty(Dirty::VIEWPORT)) {
    clearDirty(Dirty::VIEWPORT);

    auto viewport = mViewport.value_or(
        Viewport{{0.0f, 0.0f}, {(float)mWidth, (float)mHeight}});

    glViewport((GLint)viewport.mPosition.x, (GLint)viewport.mPosition.y,
               (GLint)viewport.mSize.x, (GLint)viewport.mSize.y);

    glDepthRangef((GLfloat)viewport.mMinDepth, (GLfloat)viewport.mMaxDepth);
  }

  if (isDirty(Dirty::SCISSOR)) {
    clearDirty(Dirty::SCISSOR);

    if (mScissor) {
      auto value = mScissor.value();

      glEnable(GL_SCISSOR_TEST);
      glScissor(
          (GLint)glm::round(value.mPosition.x),
          (GLint)glm::round(mHeight - value.mPosition.y -
                            value.mSize.y), // TODO: need different calculations
                                            // when render target
          (GLint)glm::round(value.mSize.x), (GLint)glm::round(value.mSize.y));
    } else {
      glDisable(GL_SCISSOR_TEST);
    }
  }

    for (auto& [bind, uniform] : mUniformBuffers)
    {
        glBindBufferBase(GL_UNIFORM_BUFFER, bind, uniform->getId());
    }
    for (auto& [bind, storage] : mStorageBuffers)
    {
#if defined(IKIGAI_GL_HAS_SSBO)
    	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bind, storage->getId());
#elif defined(GL_UNIFORM_BUFFER)
    	glBindBufferBase(GL_UNIFORM_BUFFER, bind, storage->getId());
#endif
    }
}

void IKIGAI::RENDER::DriverGl::submit() {}
void IKIGAI::RENDER::DriverGl::setFrameBuffer(
    std::shared_ptr<FrameBufferInterface> frameBuffer) {
  if (frameBuffer) {
    std::static_pointer_cast<FrameBufferGl>(frameBuffer)->bind();
  } else {
    resetFrameBuffer();
  }
}
void IKIGAI::RENDER::DriverGl::resetFrameBuffer() {
  BindDefaultFramebufferGl();
}

#if !defined(OCULUS) && !defined(__APPLE__)
void APIENTRY IKIGAI::RENDER::DriverGl::GLDebugMessageCallback(
    uint32_t source, uint32_t type, uint32_t id, uint32_t severity,
    int32_t length, const char *message, const void *userParam) {
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

#endif
