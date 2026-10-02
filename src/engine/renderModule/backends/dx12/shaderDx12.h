#pragma once
#include <memory>
#ifdef DX12_BACKEND
#include <d3d12.h>
#include <map>
#include <wrl/client.h>

#include "renderModule/backends/interface/reflectionStructs.h"
#include "renderModule/backends/interface/shaderInterface.h"
#include "renderModule/backends/interface/resourceStruct.h"

namespace IKIGAI::RENDER {
class ShaderDx12 : public ShaderInterface {
public:
  const Microsoft::WRL::ComPtr<ID3D12RootSignature> &getRootSignature() const {
    return mRootSignature;
  }

  ShaderDx12(std::map<ShaderType, std::string> shaderCode);
  explicit ShaderDx12(const ShaderResource& res);

  static std::shared_ptr<ShaderDx12> Create(const ShaderResource& res);
  static std::shared_ptr<ShaderDx12>
  CreateFromPath(std::map<ShaderType, std::string> path);

  ShaderDx12();

  void bind() override {};
  void unbind() override {};
  void recompile(const ShaderResource &res) override;

  std::map<ShaderType, Microsoft::WRL::ComPtr<ID3DBlob>> mBlobs;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
  int mPushConstantRootIndex = -1;
  uint32_t mPushConstantDwords = 0;

  void buildRootSignature();

private:
  void create(const ShaderResource& res);
};
} // namespace IKIGAI::RENDER
#endif
