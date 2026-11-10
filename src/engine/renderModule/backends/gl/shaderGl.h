#pragma once
#ifdef OPENGL_BACKEND
#include "profilerModule/gpuMemoryProfiler.h"
#include "../interface/reflectionStructs.h"
#include "../interface/shaderInterface.h"
#include "coreModule/glmWrapper.hpp"
#include "mathModule/math.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include <coreModule/graphicsWrapper.hpp>
#include <unordered_map>

namespace IKIGAI::RENDER {
class ShaderGl : public IKIGAI::RENDER::ShaderInterface {
public:
  static std::shared_ptr<ShaderGl>
  CreateFromSource(const std::map<ShaderType, std::string> &source);
  static std::shared_ptr<ShaderGl>
  CreateFromPath(const std::map<ShaderType, std::string> &path);
  static std::shared_ptr<ShaderGl> Create(const ShaderResource &resource);

  ShaderGl() = default;

  ShaderGl(const ShaderResource &res);
  // ShaderGl(const ShaderResource& res, const std::vector<char>& source);

  ~ShaderGl() override;

  void recompile(const ShaderResource &res) override;
  bool adoptReloadedShader(ShaderInterface& replacement) noexcept override;

  void bind() override;
  void unbind() override;

  void setBool(const std::string &name, bool value) const;
  void setInt(const std::string &name, int value) const;
  void setFloat(const std::string &name, float value) const;
  void setVec2(const std::string &name, const glm::vec2 &value) const;
  void setVec2(const std::string &name, float x, float y) const;
  void setVec2(const std::string &name,
               const IKIGAI::MATH::Vector2f &vec) const;
  void setVec3(const std::string &name, const glm::vec3 &value) const;
  void setVec3(const std::string &name, float x, float y, float z) const;
  void setVec3(const std::string &name,
               const IKIGAI::MATH::Vector3f &vec) const;
  void setVec4(const std::string &name, const glm::vec4 &value) const;
  void setVec4(const std::string &name, float x, float y, float z, float w);
  void setVec4(const std::string &name,
               const IKIGAI::MATH::Vector4f &vec) const;
  void setMat2(const std::string &name, const glm::mat2 &mat) const;
  void setMat3(const std::string &name, const glm::mat3 &mat) const;
  void setMat3(const std::string &name,
               const IKIGAI::MATH::Matrix3f &mat) const;
  void setMat4(const std::string &name, const glm::mat4 &mat) const;
  void setMat4(const std::string &name,
               const IKIGAI::MATH::Matrix4f &mat) const;
  IKIGAI::MATH::Vector2f getUniformVec2(const std::string &name) const;
  IKIGAI::MATH::Vector3f getUniformVec3(const std::string &name) const;
  IKIGAI::MATH::Vector4f getUniformVec4(const std::string &name) const;
  IKIGAI::MATH::Matrix3f getUniformMat3(const std::string &name) const;
  IKIGAI::MATH::Matrix4f getUniformMat4(const std::string &name) const;
  int getUniformInt(const std::string &name) const;
  float getUniformFloat(const std::string &name) const;
  int getUniformLocation(const std::string &name) const;

private:
#if IKIGAI_ENABLE_MEMORY_TRACKING
  mutable PROFILER::GpuAllocationToken mGpuProgram;
#endif
  static void CheckCompileErrors(GLuint shader, const std::string &type);

  // void readReflection();

  void compile(const std::map<ShaderType, std::string> &source);
  void create(const ShaderResource &res);
  void clear() const;
  // void loadBinaryShader(const std::vector<char>& buffer);

  mutable std::unordered_map<std::string, int> uniformLocationCache;
};
} // namespace IKIGAI::RENDER

#endif
