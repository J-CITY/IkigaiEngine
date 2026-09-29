#include "shaderDx12.h"

#ifdef DX12_BACKEND
#include "../../gameRendererDx12.h"
using namespace IKIGAI::RENDER;


std::shared_ptr<ShaderDx12> ShaderDx12::CreateFromPath(std::map<ShaderType, std::string> path) {
	auto shader = std::make_shared<ShaderDx12>();

	const auto& vertexPath = path[ShaderType::VERTEX];
	const auto& fragmentPath = path[ShaderType::FRAGMENT];
	shader->mBlobs[ShaderType::VERTEX] = d3dUtil::CompileShader(shader->mReflection, std::wstring(vertexPath.begin(), vertexPath.end()), nullptr, L"main", L"vs_6_6", ShaderType::VERTEX);
	shader->mBlobs[ShaderType::FRAGMENT] = d3dUtil::CompileShader(shader->mReflection, std::wstring(fragmentPath.begin(), fragmentPath.end()), nullptr, L"main", L"ps_6_6", ShaderType::FRAGMENT);

	shader->buildRootSignature();
	//shader->buildPSO();

	return shader;
}

ShaderDx12::ShaderDx12() {
	static size_t ID = 0;
	mId = ID;
	++ID;
}

//void ShaderDx12::buildPSO() {
//	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
//	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
//
//	opaquePsoDesc.pRootSignature = mRootSignature.Get();
//	opaquePsoDesc.VS = {
//				reinterpret_cast<BYTE*>(mVertexShader->GetBufferPointer()),
//				mVertexShader->GetBufferSize()
//	};
//	opaquePsoDesc.PS = {
//		reinterpret_cast<BYTE*>(mFragmentShader->GetBufferPointer()),
//		mFragmentShader->GetBufferSize()
//	};
//	if (mInputLayout.size() > 0) {
//		opaquePsoDesc.InputLayout = { mInputLayout.data(),(UINT)mInputLayout.size() };
//	}
//	if (GameRendererDx12::mApp->mDriver->mUseDepth) {
//		opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
//	}
//	else {
//		opaquePsoDesc.DepthStencilState.DepthEnable = false;
//		opaquePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
//		opaquePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
//	}
//	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
//	opaquePsoDesc.SampleMask = UINT_MAX;
//	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
//	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
//	opaquePsoDesc.NumRenderTargets = mFragmentReflection->colorDescriptor->members.empty() ? 1 : mFragmentReflection->colorDescriptor->members.size();
//	for (int i = 0; i < opaquePsoDesc.NumRenderTargets; i++) {
//		opaquePsoDesc.RTVFormats[i] = DXGI_FORMAT_R16G16B16A16_FLOAT;
//	}
//	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
//	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
//	ThrowIfFailed(GameRendererDx12::mApp->mDriver->mDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSO)));
//}

void ShaderDx12::buildRootSignature() {
	std::vector<CD3DX12_DESCRIPTOR_RANGE> texTables;
	int texSize = 0;
	for (auto& uniform : mReflection.mUniforms) {
		if (uniform.mType == ShaderReflection::UniformType::SAMPLER_2D ||
			uniform.mType == ShaderReflection::UniformType::SAMPLER_3D ||
			uniform.mType == ShaderReflection::UniformType::SAMPLER_CUBE ||
			uniform.mType == ShaderReflection::UniformType::SAMPLER_2D_ARRAY) {
			texSize++;
		}
	}
	texTables.reserve(texSize);

	std::vector<CD3DX12_ROOT_PARAMETER> slotRootParameters;

	for (auto& uniform : mReflection.mUniforms) {
		CD3DX12_ROOT_PARAMETER elem;
		if (uniform.mType == ShaderReflection::UniformType::UNIFORM_BUFFER) {
			elem.InitAsConstantBufferView(uniform.mBind);
			slotRootParameters.push_back(elem);
		}
		else if (uniform.mType == ShaderReflection::UniformType::STORAGE_BUFFER) {
			elem.InitAsShaderResourceView(uniform.mBind, uniform.mSet);
			slotRootParameters.push_back(elem);
		}
		else if (uniform.mType == ShaderReflection::UniformType::SAMPLER_2D ||
			uniform.mType == ShaderReflection::UniformType::SAMPLER_3D ||
			uniform.mType == ShaderReflection::UniformType::SAMPLER_CUBE ||
			uniform.mType == ShaderReflection::UniformType::SAMPLER_2D_ARRAY) {
			CD3DX12_DESCRIPTOR_RANGE texTable;
			texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, uniform.mBind, uniform.mSet);
			texTables.push_back(texTable);
			elem.InitAsDescriptorTable(1, &texTables.back(), D3D12_SHADER_VISIBILITY_PIXEL);
			slotRootParameters.push_back(elem);
		}
		else if (uniform.mType == ShaderReflection::UniformType::PUSH_CONSTANT) {
			elem.InitAsConstants(uniform.mSize / 4, uniform.mBind, uniform.mSet);
			mPushConstantRootIndex = slotRootParameters.size();
			slotRootParameters.push_back(elem);
		}
	}

	auto staticSamplers = DriverDx12::GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(slotRootParameters.size(), slotRootParameters.data(),
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr) {
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(d3dUtil::GetDriver()->getDevice()->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));

}

#endif
