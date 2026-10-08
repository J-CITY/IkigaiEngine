#include "shaderGl.h"
#ifdef OPENGL_BACKEND
#include <array>
#include <coreModule/graphicsWrapper.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
// #include <coreModule/resourceManager/textureManager.h>
#include "../interface/reflectionStructs.h"
#include "spirv_reflect.h"
#include "utilsModule/assertion.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/log/loggerDefine.h"
#include "utilsModule/pathGetter.h"
#include "utilsModule/stringUtils.h"
#include <renderModule/backends/interface/resourceStruct.h>
#include <resourceModule/serviceManager.h>
#include <resourceModule/fileSystem/fileSystem.h>

namespace {
std::map<IKIGAI::RENDER::ShaderType, uint32_t> ToGlEnum = {
    {IKIGAI::RENDER::ShaderType::VERTEX, GL_VERTEX_SHADER},
    {IKIGAI::RENDER::ShaderType::FRAGMENT, GL_FRAGMENT_SHADER},
#ifndef USING_GLES
#ifdef GL_COMPUTE_SHADER
    {IKIGAI::RENDER::ShaderType::COMPUTE, GL_COMPUTE_SHADER},
#endif
    {IKIGAI::RENDER::ShaderType::TESSELLATION_CONTROL, GL_TESS_CONTROL_SHADER},
    {IKIGAI::RENDER::ShaderType::TESSELLATION_EVALUATION,
     GL_TESS_EVALUATION_SHADER},
    {IKIGAI::RENDER::ShaderType::GEOMETRY, GL_GEOMETRY_SHADER},
#endif
};

// Конвертация vector<uint32_t> (SPIR-V) в hex-строку
std::string SpirvToHex(const std::vector<uint32_t> &spirv) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (uint32_t word : spirv) {
    oss << std::setw(8) << word;
  }
  return oss.str();
}

// Конвертация hex-строки обратно в vector<uint32_t>
std::vector<uint32_t> HexToSpirv(const std::string &hex) {
  std::vector<uint32_t> result;
  result.reserve(hex.size() / 8);
  for (size_t i = 0; i + 8 <= hex.size(); i += 8) {
    uint32_t word =
        static_cast<uint32_t>(std::stoul(hex.substr(i, 8), nullptr, 16));
    result.push_back(word);
  }
  return result;
}

struct SpirvCache {
  std::map<std::string, std::string> stages;

  template <class Context>
  constexpr static auto serde(Context &context, SpirvCache &value) {
    using Self = SpirvCache;
    serde::serde_struct(context, value).field(&Self::stages, "Stages");
  }
};
} // namespace

std::shared_ptr<IKIGAI::RENDER::ShaderGl>
IKIGAI::RENDER::ShaderGl::CreateFromSource(
    const std::map<ShaderType, std::string> &source) {
  ShaderResource res;
  res.sources = source;
  return std::make_shared<ShaderGl>(res);
}

std::shared_ptr<IKIGAI::RENDER::ShaderGl>
IKIGAI::RENDER::ShaderGl::CreateFromPath(
    const std::map<ShaderType, std::string> &paths) {
  ShaderResource res;
  // TODO:
  res.setPaths(paths);
  return Create(res);
}

std::shared_ptr<IKIGAI::RENDER::ShaderGl>
IKIGAI::RENDER::ShaderGl::Create(const ShaderResource &resource) {
  const auto useBinary = resource.useBinary;
  bool needSaveBinarySource = false;
  auto &fs =
      IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::FileSystem>();

  // Try load spirv bin
  if (useBinary) {
    const auto cacheJsonPath = resource.path + ".spirv.json";
    if (fs.isFileExist(cacheJsonPath)) {
      // Читаем JSON-кэш и десериализуем SpirvCache
      if (auto file = fs.getFile(cacheJsonPath); file && file->isOpened()) {
        const auto jsonStr = file->readStr();
        if (auto res = IKIGAI::UTILS::FromJsonStr<SpirvCache>(jsonStr);
            !res.isErr()) {
          const auto &cache = res.unwrap();
          for (const auto &[key, hexStr] : cache.stages) {
            resource.spirvSources[static_cast<ShaderType>(std::stoi(key))] =
                HexToSpirv(hexStr);
          }
          return std::make_shared<ShaderGl>(resource);
        } else {
          LOG_INFO << "Failed to parse SPIR-V cache: " << cacheJsonPath;
        }
      }
    } else {
      needSaveBinarySource = true;
    }
  }

  // Load sources
  for (const auto &[type, path] : resource.paths) {
    if (!path.empty()) {
      auto file = fs.getFile(path);
      if (file && file->isOpened()) {
        resource.sources[ShaderResource::toEnum[type]] = file->readStr();
      } else {
        LOG_ERROR << "Failed to load: " << path;
      }
    }
  }

  auto shader = std::make_shared<ShaderGl>(resource);

  // Save spirv bin
  if (needSaveBinarySource && shader->mId) {
    const auto cacheJsonPath = shader->mPath + ".spirv.json";
    if (!fs.isFileExist(cacheJsonPath)) {
      SpirvCache cache;
      for (const auto &[type, spirv] : resource.spirvSources) {
        cache.stages[std::to_string(static_cast<int>(type))] = SpirvToHex(spirv);
      }
      if (auto res = IKIGAI::UTILS::ToJsonStr(cache, 2); !res.isErr()) {
        if (auto file =
                fs.getFile(cacheJsonPath, IKIGAI::RESOURCES::FileMode::WRITE);
            file) {
          file->write(res.unwrap());
        }
      } else {
        LOG_ERROR << "Failed to serialize SPIR-V cache: " << cacheJsonPath;
      }
    }
  }

  return shader;
}

IKIGAI::RENDER::ShaderGl::ShaderGl(const ShaderResource &res) { create(res); }

void IKIGAI::RENDER::ShaderGl::recompile(const ShaderResource &res) {
  clear();
  create(res);
}

IKIGAI::RENDER::ShaderGl::~ShaderGl() { clear(); }

void IKIGAI::RENDER::ShaderGl::clear() const {
  glDeleteShader(static_cast<unsigned>(mId));
}

void IKIGAI::RENDER::ShaderGl::create(const ShaderResource &res) {
  mPath = res.path;
  mShaderPaths = res.getPaths();
#ifdef __EMSCRIPTEN__
  std::cout << "[web] ShaderGl::create " << mPath << std::endl;
  std::cout.flush();
#endif

  // TODO: add to resources
  std::vector<std::string> defines;

  // SPIRV compilation
  if (!res.spirvSources.empty()) {
    for (auto &[type, spirv] : res.spirvSources) {
#ifdef __EMSCRIPTEN__
      std::cout << "[web] GetReflection begin stage=" << static_cast<int>(type)
                << std::endl;
      std::cout.flush();
#endif
      GetReflection(mReflection, spirv, type);
#ifdef __EMSCRIPTEN__
      std::cout << "[web] GetReflection ok stage=" << static_cast<int>(type)
                << std::endl;
      std::cout.flush();
#endif
    }
  } else {
    for (auto &[type, source] : res.sources) {
      if (source.empty()) {
        IKIGAI_COUT("Shader source is empty: " << mPath << " stage " << static_cast<int>(type));
        continue;
      }
      try {
        res.spirvSources[type] = CompileGlslToSpirv(type, source, defines);
      } catch (const std::exception& e) {
        IKIGAI_COUT("Shader compile failed: " << mPath << " " << e.what());
        continue;
      }
#ifdef __EMSCRIPTEN__
      std::cout << "[web] CompileGlslToSpirv ok stage=" << static_cast<int>(type)
                << std::endl;
      std::cout.flush();
      std::cout << "[web] GetReflection begin stage=" << static_cast<int>(type)
                << std::endl;
      std::cout.flush();
#endif
      GetReflection(mReflection, res.spirvSources[type], type);
#ifdef __EMSCRIPTEN__
      std::cout << "[web] GetReflection ok stage=" << static_cast<int>(type)
                << std::endl;
      std::cout.flush();
#endif
    }
  }

  // move it
  bool es = false;
  bool enable_420pack_extension = true;
  bool force_flattened_io_blocks = true;
  unsigned int version = 450;
#if defined(IOS)
  es = true;
  version = 300;
  enable_420pack_extension = false;
  force_flattened_io_blocks = false;
#elif defined(WINDOWS)
  es = false;
  version = 450;
  enable_420pack_extension = true;
  force_flattened_io_blocks = true;
#elif defined(__APPLE__)
  // macOS OpenGL is 4.1. SSBOs need 4.3, so the cross-compile remaps them to UBOs.
  es = false;
  version = 410;
  enable_420pack_extension = false;
  force_flattened_io_blocks = true;
#elif defined(EMSCRIPTEN)
  es = true;
  version = 300;
  enable_420pack_extension = false;
  force_flattened_io_blocks = false;
#elif defined(__ANDROID__)
  es = true;
#if IKIGAI_GLES_VERSION >= 320
  version = 320;
#elif IKIGAI_GLES_VERSION >= 310
  version = 310;
#else
  version = 300;
#endif
  enable_420pack_extension = false;
  // GLES links struct varyings by name. Vert uses Out, frag uses fs_in, so Mali
  // rejects the program. Flatten after renaming both sides to "varying", same as WebGL2.
  force_flattened_io_blocks = true;
#endif

  std::map<ShaderType, std::string> sources;
  for (auto &[type, source] : res.spirvSources) {
    sources[type] =
        CompileSpirvToGlsl(source, es, version, enable_420pack_extension,
                           force_flattened_io_blocks);
#ifdef __EMSCRIPTEN__
    std::cout << "[web] CompileSpirvToGlsl ok stage=" << static_cast<int>(type)
              << std::endl;
    std::cout.flush();
#endif
  }

#ifdef __EMSCRIPTEN__
  std::cout << "[web] ShaderGl::compile begin " << mPath << std::endl;
  std::cout.flush();
#endif
  IKIGAI_COUT("Shader cross-compile path=" << mPath
            << " stages=" << sources.size()
            << " es=" << es << " version=" << version
            << " flat=" << force_flattened_io_blocks);
  compile(sources);
#ifdef __EMSCRIPTEN__
  std::cout << "[web] ShaderGl::compile done " << mPath << " id=" << mId
            << std::endl;
  std::cout.flush();
#endif

  bool need_fix_uniform = (es && version <= 300) ||
                          (!es && version < 420 && !enable_420pack_extension);

#ifdef GL_UNIFORM_BUFFER
  if (need_fix_uniform) {
    const auto program = static_cast<GLuint>(mId);
    glUseProgram(program);
    for (const auto &uniform : mReflection.mUniforms) {
      if (uniform.mType != ShaderReflection::UniformType::UNIFORM_BUFFER &&
          uniform.mType != ShaderReflection::UniformType::STORAGE_BUFFER) {
        continue;
      }
      auto blockIndex =
          glGetUniformBlockIndex(program, uniform.mName.c_str());
      if (blockIndex == GL_INVALID_INDEX) {
        GLint blockCount = 0;
        glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &blockCount);
        const auto wanted = UTILS::ToLower(uniform.mName);
        for (GLint i = 0; i < blockCount; ++i) {
          GLchar name[256];
          GLsizei length = 0;
          glGetActiveUniformBlockName(program, static_cast<GLuint>(i), 256,
                                      &length, name);
          if (UTILS::ToLower(name) == wanted) {
            blockIndex = static_cast<GLuint>(i);
            break;
          }
        }
      }
      if (blockIndex != GL_INVALID_INDEX) {
        glUniformBlockBinding(program, blockIndex,
                              static_cast<GLuint>(uniform.mBind));
      }
    }
    glUseProgram(0);
  }
#endif
#ifdef __EMSCRIPTEN__
  std::cout << "[web] ShaderGl::create done " << mPath << " id=" << mId << std::endl;
  std::cout.flush();
#endif
}

void IKIGAI::RENDER::ShaderGl::compile(
    const std::map<ShaderType, std::string> &source) {
  mId = glCreateProgram();

  // TODO: check if compute, then only compute
  std::map<ShaderType, uint32_t> shaderIds;
  for (auto &[type, src] : source) {
    if (!ToGlEnum.contains(type)) {
      // LOG_WARNING("");
      continue;
    }
    const char *shaderCode = src.c_str();
    auto id = glCreateShader(ToGlEnum.at(type));
    glShaderSource(id, 1, &shaderCode, nullptr);
    glCompileShader(id);
    shaderIds[type] = id;
    CheckCompileErrors(id, "Shader: " + mShaderPaths[type]);

    glAttachShader(static_cast<unsigned>(mId), id);
  }
  glLinkProgram(static_cast<unsigned>(mId));
  CheckCompileErrors(static_cast<unsigned>(mId), "PROGRAM");
  GLint linked = 0;
  glGetProgramiv(static_cast<unsigned>(mId), GL_LINK_STATUS, &linked);
  IKIGAI_COUT("Shader link path=" << mPath << " program=" << mId
            << " stages=" << shaderIds.size() << " ok=" << linked);
  for (auto &[type, id] : shaderIds) {
    glDeleteShader(id);
  }
}

void IKIGAI::RENDER::ShaderGl::bind() {
  glUseProgram(static_cast<unsigned>(mId));
}

void IKIGAI::RENDER::ShaderGl::unbind() { glUseProgram(0); }

void IKIGAI::RENDER::ShaderGl::setBool(const std::string &name,
                                       bool value) const {
  glUniform1i(glGetUniformLocation(mId, name.c_str()), (int)value);
}

void IKIGAI::RENDER::ShaderGl::setInt(const std::string &name,
                                      int value) const {
  glUniform1i(glGetUniformLocation(mId, name.c_str()), value);
}

void IKIGAI::RENDER::ShaderGl::setFloat(const std::string &name,
                                        float value) const {
  glUniform1f(glGetUniformLocation(mId, name.c_str()), value);
}

void IKIGAI::RENDER::ShaderGl::setVec2(const std::string &name,
                                       const glm::vec2 &value) const {
  glUniform2fv(glGetUniformLocation(mId, name.c_str()), 1, &value[0]);
}

void IKIGAI::RENDER::ShaderGl::setVec2(const std::string &name, float x,
                                       float y) const {
  glUniform2f(glGetUniformLocation(mId, name.c_str()), x, y);
}

void IKIGAI::RENDER::ShaderGl::setVec3(const std::string &name,
                                       const glm::vec3 &value) const {
  glUniform3fv(glGetUniformLocation(mId, name.c_str()), 1, &value[0]);
}

void IKIGAI::RENDER::ShaderGl::setVec3(const std::string &name, float x,
                                       float y, float z) const {
  glUniform3f(glGetUniformLocation(mId, name.c_str()), x, y, z);
}

void IKIGAI::RENDER::ShaderGl::setVec4(const std::string &name,
                                       const glm::vec4 &value) const {
  glUniform4fv(glGetUniformLocation(mId, name.c_str()), 1, &value[0]);
}

void IKIGAI::RENDER::ShaderGl::setVec4(const std::string &name, float x,
                                       float y, float z, float w) {
  glUniform4f(glGetUniformLocation(mId, name.c_str()), x, y, z, w);
}

void IKIGAI::RENDER::ShaderGl::setMat2(const std::string &name,
                                       const glm::mat2 &mat) const {
  glUniformMatrix2fv(glGetUniformLocation(mId, name.c_str()), 1, GL_FALSE,
                     &mat[0][0]);
}

void IKIGAI::RENDER::ShaderGl::setMat3(const std::string &name,
                                       const glm::mat3 &mat) const {
  glUniformMatrix3fv(glGetUniformLocation(mId, name.c_str()), 1, GL_FALSE,
                     &mat[0][0]);
}

void IKIGAI::RENDER::ShaderGl::setMat4(const std::string &name,
                                       const glm::mat4 &mat) const {
  glUniformMatrix4fv(glGetUniformLocation(mId, name.c_str()), 1, GL_FALSE,
                     &mat[0][0]);
}

void IKIGAI::RENDER::ShaderGl::setVec2(
    const std::string &name, const IKIGAI::MATH::Vector2f &vec2) const {
  glUniform2f(getUniformLocation(name), vec2.x, vec2.y);
}

void IKIGAI::RENDER::ShaderGl::setVec3(
    const std::string &name, const IKIGAI::MATH::Vector3f &vec3) const {
  glUniform3f(getUniformLocation(name), vec3.x, vec3.y, vec3.z);
}

void IKIGAI::RENDER::ShaderGl::setVec4(
    const std::string &name, const IKIGAI::MATH::Vector4f &vec4) const {
  glUniform4f(getUniformLocation(name), vec4.x, vec4.y, vec4.z, vec4.w);
}

void IKIGAI::RENDER::ShaderGl::setMat4(
    const std::string &name, const IKIGAI::MATH::Matrix4f &mat4) const {
#ifndef USING_GLES
  glUniformMatrix4fv(getUniformLocation(name), 1, GL_TRUE, mat4.getData());
#else
  auto m = IKIGAI::MATH::Matrix4f::Transpose(mat4);
  glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, m.getData());
#endif
}

void IKIGAI::RENDER::ShaderGl::setMat3(
    const std::string &name, const IKIGAI::MATH::Matrix3f &mat3) const {
#ifndef USING_GLES
  glUniformMatrix3fv(getUniformLocation(name), 1, GL_TRUE, mat3.getData());
#else
  auto m = IKIGAI::MATH::Matrix3f::Transpose(mat3);
  glUniformMatrix3fv(getUniformLocation(name), 1, GL_FALSE, m.getData());
#endif
}

IKIGAI::MATH::Vector2f
IKIGAI::RENDER::ShaderGl::getUniformVec2(const std::string &name) const {
  GLfloat values[2];
#ifndef USING_GLES
#ifdef __APPLE__
  glGetUniformfv(static_cast<unsigned>(mId), getUniformLocation(name), values);
#else
  glGetnUniformfv(static_cast<unsigned>(mId), getUniformLocation(name),
                  2 * sizeof(float), values);
#endif
#endif
  return reinterpret_cast<IKIGAI::MATH::Vector2f &>(values);
}

IKIGAI::MATH::Vector3f
IKIGAI::RENDER::ShaderGl::getUniformVec3(const std::string &name) const {
  GLfloat values[3];
#ifndef USING_GLES
#ifdef __APPLE__
  glGetUniformfv(static_cast<unsigned>(mId), getUniformLocation(name), values);
#else
  glGetnUniformfv(static_cast<unsigned>(mId), getUniformLocation(name),
                  3 * sizeof(float), values);
#endif
#endif
  return reinterpret_cast<IKIGAI::MATH::Vector3f &>(values);
}

IKIGAI::MATH::Vector4f
IKIGAI::RENDER::ShaderGl::getUniformVec4(const std::string &name) const {
  GLfloat values[4];
#ifndef USING_GLES
#ifdef __APPLE__
  glGetUniformfv(static_cast<unsigned>(mId), getUniformLocation(name), values);
#else
  glGetnUniformfv(static_cast<unsigned>(mId), getUniformLocation(name),
                  4 * sizeof(float), values);
#endif
#endif
  return reinterpret_cast<IKIGAI::MATH::Vector4f &>(values);
}

IKIGAI::MATH::Matrix3f
IKIGAI::RENDER::ShaderGl::getUniformMat3(const std::string &name) const {
  GLfloat values[16];
#ifndef USING_GLES
#ifdef __APPLE__
  glGetUniformfv(static_cast<unsigned>(mId), getUniformLocation(name), values);
#else
  glGetnUniformfv(static_cast<unsigned>(mId), getUniformLocation(name),
                  9 * sizeof(float), values);
#endif
#endif
  return reinterpret_cast<IKIGAI::MATH::Matrix3f &>(values);
}

IKIGAI::MATH::Matrix4f
IKIGAI::RENDER::ShaderGl::getUniformMat4(const std::string &name) const {
  GLfloat values[16];
#ifndef USING_GLES
#ifdef __APPLE__
  glGetUniformfv(static_cast<unsigned>(mId), getUniformLocation(name), values);
#else
  glGetnUniformfv(static_cast<unsigned>(mId), getUniformLocation(name),
                  16 * sizeof(float), values);
#endif
#endif
  return reinterpret_cast<IKIGAI::MATH::Matrix4f &>(values);
}

int IKIGAI::RENDER::ShaderGl::getUniformInt(const std::string &name) const {
  int value;
  glGetUniformiv(static_cast<unsigned>(mId), getUniformLocation(name), &value);
  return value;
}

float IKIGAI::RENDER::ShaderGl::getUniformFloat(const std::string &name) const {
  float value;
  glGetUniformfv(static_cast<unsigned>(mId), getUniformLocation(name), &value);
  return value;
}

int IKIGAI::RENDER::ShaderGl::getUniformLocation(
    const std::string &name) const {
  if (uniformLocationCache.contains(name)) {
    return uniformLocationCache.at(name);
  }
  const int location =
      glGetUniformLocation(static_cast<unsigned>(mId), name.c_str());
  if (location == -1) {
    LOG_ERROR << ("Uniform: '" + name + "' doesn't exist\n");
    return location;
  }
  uniformLocationCache[name] = location;
  return location;
}

void IKIGAI::RENDER::ShaderGl::CheckCompileErrors(GLuint shader,
                                                  const std::string &type) {
  GLint success;
  GLchar infoLog[1024];
  if (type != "PROGRAM") {
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
      glGetShaderInfoLog(shader, 1024, NULL, infoLog);
      IKIGAI_COUT("Shader compilation failed (" << type << "): " << infoLog);
    }
  } else {
    glGetProgramiv(shader, GL_LINK_STATUS, &success);
    if (!success) {
      glGetProgramInfoLog(shader, 1024, NULL, infoLog);
      IKIGAI_COUT("Shader link failed: " << infoLog);
    }
  }
}

#endif

//---------------------------------------------//
/*
void IKIGAI::RENDER::ShaderGl::loadBinaryShader(const std::vector<char>& buffer)
{ #ifndef USING_GLES mId = glCreateProgram(); GLint formats = 0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
        std::vector<GLint> binaryFormats;
        binaryFormats.resize(formats);
        glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, binaryFormats.data());

        glProgramBinary(static_cast<unsigned>(mId), binaryFormats[0],
buffer.data(), buffer.size());
        // Check for success/failure
        GLint status;
        glGetProgramiv(static_cast<unsigned>(mId), GL_LINK_STATUS, &status);
        if (GL_FALSE == status) {
                // Handle failure ...
        }
        //glValidateProgram(ID);
#endif
}

void IKIGAI::RENDER::ShaderGl::SaveBinary() {
#ifndef USING_GLES
        GLint length = 0;
        glGetProgramiv(static_cast<unsigned>(mId), GL_PROGRAM_BINARY_LENGTH,
&length);

        std::vector<GLubyte> buffer(length);
        GLenum format = 0;
        glGetProgramBinary(static_cast<unsigned>(mId), length, nullptr, &format,
buffer.data()); std::vector<uint8_t> data(buffer.begin(), buffer.end());
        IKIGAI::RESOURCES::ServiceManager::Get<
IKIGAI::RESOURCES::FileSystem>().getFile(binPath,
IKIGAI::RESOURCES::FileMode::WRITE)->write(data); #endif
}

bool IKIGAI::RENDER::ShaderGl::CheckBinarySupport() {
#ifndef USING_GLES
        GLint formats = 0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
        if (formats < 1) {
                //LOG_INFO("Driver does not support any binary formats.");
                return false;
        }
        return true;
#else
        return false;
#endif
}

void IKIGAI::RENDER::ShaderGl::readReflection() {
        mReflection = ShaderReflection();

        struct UData {
                int id = 0;
                std::string name;
                IKIGAI::RENDER::ShaderReflection::UniformType type =
IKIGAI::RENDER::ShaderReflection::UniformType::NONE; int arraySize = 0; int
offset = 0; int blockIndex = 0; size_t shaderMask = 0;
        };

        static const std::map<GLenum,
IKIGAI::RENDER::ShaderReflection::UniformType> fromGlType = { {GL_FLOAT_MAT4,
IKIGAI::RENDER::ShaderReflection::UniformType::MAT4}, {GL_FLOAT_MAT3,
IKIGAI::RENDER::ShaderReflection::UniformType::MAT3}, {GL_FLOAT_VEC4,
IKIGAI::RENDER::ShaderReflection::UniformType::VEC4}, {GL_FLOAT_VEC3,
IKIGAI::RENDER::ShaderReflection::UniformType::VEC3}, {GL_FLOAT_VEC2,
IKIGAI::RENDER::ShaderReflection::UniformType::VEC2}, {GL_INT,
IKIGAI::RENDER::ShaderReflection::UniformType::INT}, {GL_FLOAT,
IKIGAI::RENDER::ShaderReflection::UniformType::FLOAT}, {GL_BOOL,
IKIGAI::RENDER::ShaderReflection::UniformType::BOOL}, {GL_SAMPLER_2D,
IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_2D}, {GL_SAMPLER_CUBE,
IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_CUBE}, #ifndef USING_GLES
                {GL_SAMPLER_3D,
IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_3D},
                {GL_SAMPLER_2D_ARRAY,
IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_2D_ARRAY},
                {GL_SAMPLER_2D_ARRAY,
IKIGAI::RENDER::ShaderReflection::UniformType::IMAGE_2D}, {GL_SAMPLER_2D_ARRAY,
IKIGAI::RENDER::ShaderReflection::UniformType::IMAGE_2D_ARRAY},
                {GL_SAMPLER_2D_ARRAY,
IKIGAI::RENDER::ShaderReflection::UniformType::IMAGE_3D}, {GL_SAMPLER_2D_ARRAY,
IKIGAI::RENDER::ShaderReflection::UniformType::IMAGE_CUBE}, #endif
        };

        static const std::map<IKIGAI::RENDER::ShaderReflection::UniformType,
unsigned> typeToSize = { {IKIGAI::RENDER::ShaderReflection::UniformType::MAT4,
sizeof(float) * 16}, {IKIGAI::RENDER::ShaderReflection::UniformType::MAT3,
sizeof(float) * 9}, {IKIGAI::RENDER::ShaderReflection::UniformType::VEC4,
sizeof(float) * 4}, {IKIGAI::RENDER::ShaderReflection::UniformType::VEC3,
sizeof(float) * 3}, {IKIGAI::RENDER::ShaderReflection::UniformType::VEC2,
sizeof(float) * 2}, {IKIGAI::RENDER::ShaderReflection::UniformType::INT,
sizeof(uint32_t) * 1}, {IKIGAI::RENDER::ShaderReflection::UniformType::FLOAT,
sizeof(float) * 1}, {IKIGAI::RENDER::ShaderReflection::UniformType::BOOL,
sizeof(uint32_t) * 1},
                {IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_2D,
sizeof(uint32_t) * 1},
                {IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_CUBE,
sizeof(uint32_t) * 1}, #ifndef USING_GLES
                {IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_3D,
sizeof(uint32_t) * 1},
                {IKIGAI::RENDER::ShaderReflection::UniformType::SAMPLER_2D_ARRAY,
sizeof(uint32_t) * 1}, {IKIGAI::RENDER::ShaderReflection::UniformType::IMAGE_2D,
sizeof(uint32_t) * 1},
                {IKIGAI::RENDER::ShaderReflection::UniformType::IMAGE_2D_ARRAY,
sizeof(uint32_t) * 1}, {IKIGAI::RENDER::ShaderReflection::UniformType::IMAGE_3D,
sizeof(uint32_t) * 1},
                {IKIGAI::RENDER::ShaderReflection::UniformType::IMAGE_CUBE,
sizeof(uint32_t) * 1}, #endif
        };

        std::array<IKIGAI::RENDER::ShaderType, 6> shaderTypes = {
                IKIGAI::RENDER::ShaderType::VERTEX,
                IKIGAI::RENDER::ShaderType::FRAGMENT,
                IKIGAI::RENDER::ShaderType::TESSELLATION_CONTROL,
                IKIGAI::RENDER::ShaderType::TESSELLATION_EVALUATION,
                IKIGAI::RENDER::ShaderType::GEOMETRY,
                IKIGAI::RENDER::ShaderType::COMPUTE
        };

        std::vector<UData> data;
        GLint numActiveUniforms = 0;
        glGetProgramiv(static_cast<unsigned>(mId), GL_ACTIVE_UNIFORMS,
&numActiveUniforms);

        std::vector<GLchar> nameData(256);
        for (auto i = 0; i < numActiveUniforms; ++i) {
                GLint arraySize = 0;
                GLenum type = 0;
                GLsizei actualLength = 0;
                glGetActiveUniform(static_cast<unsigned>(mId), i,
static_cast<GLsizei>(nameData.size()), &actualLength, &arraySize, &type,
nameData.data()); std::string name(nameData.data(), actualLength);

                std::array<GLint, 8> blockData{};
#ifndef USING_GLES
                std::array<GLenum, 8> blockProperties{GL_OFFSET, GL_BLOCK_INDEX,
                        GL_REFERENCED_BY_VERTEX_SHADER,
GL_REFERENCED_BY_FRAGMENT_SHADER, GL_REFERENCED_BY_TESS_CONTROL_SHADER,
GL_REFERENCED_BY_TESS_EVALUATION_SHADER, GL_REFERENCED_BY_GEOMETRY_SHADER,
GL_REFERENCED_BY_COMPUTE_SHADER};
                glGetProgramResourceiv(static_cast<unsigned>(mId), GL_UNIFORM,
i, blockProperties.size(), blockProperties.data(), blockData.size(), nullptr,
blockData.data()); #endif if (name.back() == '\0') { name.pop_back();
                }

                data.push_back({i, name, fromGlType.at(type), arraySize,
blockData[0], blockData[1]});

                // if it is UBO
#ifndef USING_GLES
                if (blockData[1] >= 0) {
                        //UBO construct later
                        continue;
                }
#endif

                for (int i = 2; i < blockData.size(); i++) {
                        if (blockData[i]) {
                                data.back().shaderMask |=
static_cast<size_t>(shaderTypes[i - 2]);
                        }
                }
        }
#ifndef USING_GLES
        std::array<GLenum, 10> blockProperties{
                GL_NAME_LENGTH, GL_NUM_ACTIVE_VARIABLES,
                GL_BUFFER_DATA_SIZE, GL_BUFFER_BINDING,
                GL_REFERENCED_BY_VERTEX_SHADER,
GL_REFERENCED_BY_TESS_CONTROL_SHADER, GL_REFERENCED_BY_TESS_EVALUATION_SHADER,
GL_REFERENCED_BY_GEOMETRY_SHADER, GL_REFERENCED_BY_FRAGMENT_SHADER,
GL_REFERENCED_BY_COMPUTE_SHADER
        };
        std::array<GLint, 10> blockData{};
        GLint numUniformBlocks = 0;
        glGetProgramInterfaceiv(static_cast<unsigned>(mId), GL_UNIFORM_BLOCK,
GL_ACTIVE_RESOURCES, &numUniformBlocks); std::set<int> usedIds; for (int blockIx
= 0; blockIx < numUniformBlocks; ++blockIx) {
                glGetProgramResourceiv(static_cast<unsigned>(mId),
GL_UNIFORM_BLOCK, blockIx, blockProperties.size(), blockProperties.data(),
blockData.size(), nullptr, blockData.data());

                //Retrieve name
                std::string blockName(blockData[0], '\0');
                glGetProgramResourceName(static_cast<unsigned>(mId),
GL_UNIFORM_BLOCK, blockIx, blockName.size() + 1, nullptr, blockName.data());

                //Retrieve indices of uniforms that are a member of this block.
                std::vector<GLint> uniformIxs(blockData[1]);
                GLenum member = GL_ACTIVE_VARIABLES;
                glGetProgramResourceiv(static_cast<unsigned>(mId),
GL_UNIFORM_BLOCK, blockIx, 1, &member, uniformIxs.size(), nullptr,
uniformIxs.data());

                //We already retrieved the size.
                auto bufferDataSize = blockData[2];
                auto bind = blockData[3];

                if (blockName.back() == '\0') {
                        blockName.pop_back();
                }

                // std::cout << "UNIFORM BUFFER INFO" << std::endl;
                // std::cout << "Name: " << blockName << std::endl;
                // std::cout << "Size: " << bufferDataSize << std::endl;
                // std::cout << "Bind id: " << bind << std::endl;
                // std::cout << "Members: " << std::endl;
                // for (auto i : uniformIxs)
                //	std::cout << i << " " << std::endl;

                IKIGAI::RENDER::ShaderReflection::Uniform uniform;
                uniform.mName = blockName;
                uniform.mType =
IKIGAI::RENDER::ShaderReflection::UniformType::UNIFORM_BUFFER; uniform.mBind =
bind; uniform.mSize = bufferDataSize - 8; // Wtf, I dont know why struct always
bigger on 8 bytes for (auto i : uniformIxs) { ShaderReflection::UniformMember
member; member.mType = data[i].type; member.mName = data[i].name; member.mOffset
= data[i].offset; member.mSize = (int)typeToSize.at(data[i].type);
                        member.mArraySize = data[i].arraySize;
                        usedIds.insert(i);
                }
                for (int i = 4; i < blockData.size(); i++) {
                        if (blockData[i]) {
                                uniform.mShaderMask |= (size_t)shaderTypes[i -
4];
                        }
                }
                std::sort(uniform.mMembers.begin(), uniform.mMembers.end(),
                        [](const auto& a, const auto& b) {
                                return a.mOffset < b.mOffset;
                        });
                mReflection.mUniforms.push_back(uniform);
                mReflection.mNameToUniforms[uniform.mName] =
mReflection.mUniforms.size() - 1;
        }


        for (int i = 0; i < data.size(); ++i) {
                if (usedIds.contains(i)) {
                        continue;
                }
                IKIGAI::RENDER::ShaderReflection::Uniform uniform;
                uniform.mName = data[i].name;
                uniform.mType = data[i].type;
                uniform.mShaderMask = data[i].shaderMask;
                uniform.mSize = typeToSize.at(uniform.mType);
                mReflection.mUniforms.push_back(uniform);
                mReflection.mNameToUniforms[uniform.mName] =
mReflection.mUniforms.size() - 1;
        }
#endif
}
*/