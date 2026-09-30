#pragma once
#include <memory>
#ifdef DX12_BACKEND
#include <d3d12.h>
#include <map>
#include <wrl/client.h>

#include "renderModule/backends/interface/reflectionStructs.h"
#include "renderModule/backends/interface/shaderInterface.h"

namespace IKIGAI::RENDER {
class ShaderDx12 : public ShaderInterface {
public:
  const Microsoft::WRL::ComPtr<ID3D12RootSignature> &getRootSignature() const {
    return mRootSignature;
  }

  ShaderDx12(std::map<ShaderType, std::string> shaderCode);

  static std::shared_ptr<ShaderDx12>
  CreateFromPath(std::map<ShaderType, std::string> path);
  // private:
  ShaderDx12();

  void bind() override {};
  void unbind() override {};
  void recompile(const ShaderResource &res) override {}

  // std::unordered_map<uint32_t, ShaderReflection::Descriptor>
  // mRequiredDescriptorBindings; std::unordered_map<ShaderStage,
  // std::unordered_map<uint32_t/*set*/,
  // std::unordered_set<uint32_t>/*bindings*/>> mRequiredDescriptorSets;
  // std::unordered_map<uint32_t, uint32_t> mBindingToRootIndexMap;

  std::map<ShaderType, Microsoft::WRL::ComPtr<ID3DBlob>> mBlobs;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
  int mPushConstantRootIndex = -1;

  void buildRootSignature();
};
} // namespace IKIGAI::RENDER
#endif
