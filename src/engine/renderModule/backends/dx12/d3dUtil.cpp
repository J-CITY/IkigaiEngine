#include "d3dUtil.h"

#ifdef DX12_BACKEND
#include "d3dx12/d3dx12.h"
#include <comdef.h>
#include <fstream>
#include "driverDx12.h"
#include "renderModule/backends/interface/driverInterface.h"
#include <ranges>

using Microsoft::WRL::ComPtr;
using namespace IKIGAI;
using namespace IKIGAI::RENDER;
#include <dxcapi.h>
#include <d3d12shader.h> 

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
    ErrorCode(hr),
    FunctionName(functionName),
    Filename(filename),
    LineNumber(lineNumber) {
}

std::wstring DxException::ToString() const {
    // Get the string description of the error code.
    _com_error err(ErrorCode);
    std::wstring msg;
#ifdef UNICODE
    msg = err.ErrorMessage();
#else
    const char* ansi = err.ErrorMessage();
    const int wlen = MultiByteToWideChar(CP_ACP, 0, ansi, -1, nullptr, 0);
    if (wlen > 0) {
        msg.resize(static_cast<size_t>(wlen - 1));
        MultiByteToWideChar(CP_ACP, 0, ansi, -1, msg.data(), wlen);
    }
#endif

    return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}

ComPtr<IDxcCompiler3> compiler{};
ComPtr<IDxcUtils> utils{};
ComPtr<IDxcIncludeHandler> includeHandler{};

PixelFormat MaskToFormat(D3D_REGISTER_COMPONENT_TYPE compType, const uint32_t Mask) {
    if (compType == D3D_REGISTER_COMPONENT_SINT32 || compType == D3D_REGISTER_COMPONENT_SINT32) {
        switch (Mask) {
        case 1: // 0001: Only the first component is used (e.g., x or R).
            return PixelFormat::R_INT;
        case 3: // 0011: First and second components are used (e.g., xy or RG).
            return PixelFormat::RG_INT;
        case 7: // 0111: First, second, and third components are used (e.g., xyz or RGB).
            return PixelFormat::RGB_INT;
        case 15: // 1111: All four components are used (e.g., xyzw or RGBA).
            return PixelFormat::RGBA_INT;
            // Add more cases here if you're handling other types of data (e.g., integers or 16-bit floats).
        }
    }
    switch (Mask) {
    case 1: // 0001: Only the first component is used (e.g., x or R).
        return PixelFormat::R_FLOAT;
    case 3: // 0011: First and second components are used (e.g., xy or RG).
        return PixelFormat::RG_FLOAT;
    case 7: // 0111: First, second, and third components are used (e.g., xyz or RGB).
        return PixelFormat::RGB_FLOAT;
    case 15: // 1111: All four components are used (e.g., xyzw or RGBA).
        return PixelFormat::RGBA_FLOAT;
        // Add more cases here if you're handling other types of data (e.g., integers or 16-bit floats).
    }
    //TODO: add int params

    return PixelFormat::RGBA_INT;
}

ComPtr<ID3DBlob> d3dUtil::CompileShader(ShaderReflection& reflection, const std::wstring& filename, const D3D_SHADER_MACRO* defines, const std::wstring& entrypoint, const std::wstring& target, ShaderType shaderType)
{

    if (!utils) {
        ThrowIfFailed(::DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)));
        ThrowIfFailed(::DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)));
        ThrowIfFailed(utils->CreateDefaultIncludeHandler(&includeHandler));
    }

    std::vector<LPCWSTR> compilationArguments = {
        L"-HV",
        L"2021",
        L"-E",
        entrypoint.data(),
        L"-T",
        target.c_str(),
        DXC_ARG_PACK_MATRIX_ROW_MAJOR,
        DXC_ARG_WARNINGS_ARE_ERRORS,
        DXC_ARG_ALL_RESOURCES_BOUND,
        L"-I",
        filename.c_str(),
    };
    
#if defined(DEBUG) || defined(_DEBUG)
    compilationArguments.push_back(DXC_ARG_DEBUG);
    compilationArguments.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
#endif

    // Load the shader source file to a blob.
    ComPtr<IDxcBlobEncoding> sourceBlob{nullptr};
    ThrowIfFailed(utils->LoadFile(filename.data(), nullptr, &sourceBlob));

    const DxcBuffer sourceBuffer = {
        .Ptr = sourceBlob->GetBufferPointer(),
        .Size = sourceBlob->GetBufferSize(),
        .Encoding = 0u,
    };

    // Compile the shader.
    ComPtr<IDxcResult> compiledShaderBuffer{};
    const HRESULT hr = compiler->Compile(&sourceBuffer, compilationArguments.data(),
        static_cast<uint32_t>(compilationArguments.size()), includeHandler.Get(),
        IID_PPV_ARGS(&compiledShaderBuffer));
    if (FAILED(hr)) {
        //fatalError(std::format("Failed to compile shader with path : {}", wStringToString(shaderPath)));
    }

    // Get compilation errors (if any).
    ComPtr<IDxcBlobUtf8> errors{};
    ThrowIfFailed(compiledShaderBuffer->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr));
    if (errors && errors->GetStringLength() > 0) {
        const LPCSTR errorMessage = errors->GetStringPointer();
        std::wcout << errorMessage << std::endl;
        //fatalError(std::format("Shader path : {}, Error : {}", wStringToString(shaderPath), errorMessage));
    }

    ComPtr<ID3DBlob> compiledShaderBlob{nullptr};
    compiledShaderBuffer->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&compiledShaderBlob), nullptr);


    // Get shader reflection data.
    ComPtr<IDxcBlob> reflectionBlob{};
    ThrowIfFailed(compiledShaderBuffer->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflectionBlob), nullptr));

    const DxcBuffer reflectionBuffer
    {
        .Ptr = reflectionBlob->GetBufferPointer(),
        .Size = reflectionBlob->GetBufferSize(),
        .Encoding = 0,
    };

    ComPtr<ID3D12ShaderReflection> shaderReflection{};
    utils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&shaderReflection));
    D3D12_SHADER_DESC shaderDesc{};
    shaderReflection->GetDesc(&shaderDesc);

    static std::map<PixelFormat, size_t> FormatToSize {
        { PixelFormat::R_FLOAT, 4 },
        { PixelFormat::RG_FLOAT, 8 },
        { PixelFormat::RGB_FLOAT, 12 },
        { PixelFormat::RGBA_FLOAT, 16 },
        { PixelFormat::R_INT, 4 },
        { PixelFormat::RG_INT, 8 },
        { PixelFormat::RGB_INT, 12 },
        { PixelFormat::RGBA_INT, 16 },
    };

    if (shaderType == ShaderType::VERTEX) {
        //read input params
        size_t offset = 0;
    	for (const uint32_t parameterIndex : std::views::iota(0u, shaderDesc.InputParameters)) {
            D3D12_SIGNATURE_PARAMETER_DESC signatureParameterDesc{};
            shaderReflection->GetInputParameterDesc(parameterIndex, &signatureParameterDesc);
            const auto format = MaskToFormat(signatureParameterDesc.ComponentType, signatureParameterDesc.Mask);
            ShaderReflection::InputParam input;
            input.mName = signatureParameterDesc.SemanticName;
            input.mIndex = signatureParameterDesc.SemanticIndex;
            input.mLocation = signatureParameterDesc.Register;
            input.mFormat = format;
            input.mOffset = offset;
            input.mSize = FormatToSize[format];
        	reflection.mInputParams.push_back(input);
            offset += FormatToSize[format];
        }
    }
    if (shaderType == ShaderType::FRAGMENT) {
        //read input params
        for (const uint32_t parameterIndex : std::views::iota(0u, shaderDesc.OutputParameters)) {
            D3D12_SIGNATURE_PARAMETER_DESC signatureParameterDesc{};
            shaderReflection->GetOutputParameterDesc(parameterIndex, &signatureParameterDesc);
            reflection.mOutputParam.push_back({
                MaskToFormat(signatureParameterDesc.ComponentType, signatureParameterDesc.Mask)
            });
        }
    }

    //uniforms
    for (const uint32_t i : std::views::iota(0u, shaderDesc.BoundResources)) {
        D3D12_SHADER_INPUT_BIND_DESC shaderInputBindDesc{};
        ThrowIfFailed(shaderReflection->GetResourceBindingDesc(i, &shaderInputBindDesc));

        if (shaderInputBindDesc.Type == D3D_SIT_CBUFFER) {
            if (reflection.mNameToUniforms.contains(shaderInputBindDesc.Name)) {
                reflection.mUniforms[reflection.mNameToUniforms.at(shaderInputBindDesc.Name)].mShaderMask |= (size_t)shaderType;
                continue;
            }
            ShaderReflection::Uniform uniform;
            uniform.mName = shaderInputBindDesc.Name;
            if (uniform.mName == "Constants" || uniform.mName == "engine_Model" || uniform.mName == "$Globals") {
                uniform.mType = ShaderReflection::UniformType::PUSH_CONSTANT;
            } else {
                uniform.mType = ShaderReflection::UniformType::UNIFORM_BUFFER;
            }
            uniform.mRootId = reflection.mUniforms.size();
            uniform.mBind = shaderInputBindDesc.BindPoint;
            uniform.mSet = shaderInputBindDesc.Space;
            uniform.mShaderMask |= (size_t)shaderType;
            reflection.mNameToUniforms[shaderInputBindDesc.Name] = reflection.mUniforms.size();

            //rootParameterIndexMap[stringToWString(shaderInputBindDesc.Name)] = static_cast<uint32_t>(rootParameters.size());
            ID3D12ShaderReflectionConstantBuffer* shaderReflectionConstantBuffer = shaderReflection->GetConstantBufferByIndex(i);
            D3D12_SHADER_BUFFER_DESC constantBufferDesc{};
            shaderReflectionConstantBuffer->GetDesc(&constantBufferDesc);

            uniform.mSize = constantBufferDesc.Size;
            for (auto memberId : std::views::iota(0u, constantBufferDesc.Variables)) {
                auto* member = shaderReflectionConstantBuffer->GetVariableByIndex(memberId);
                ShaderReflection::UniformMember memberDesc;

                D3D12_SHADER_VARIABLE_DESC memberDescDx{};
                memberDesc.mName = member->GetDesc(&memberDescDx);
                D3D12_SHADER_TYPE_DESC typeDescDx{};
            	member->GetType()->GetDesc(&typeDescDx);

                memberDesc.mName = memberDescDx.Name;
                memberDesc.mSize = memberDescDx.Size / (typeDescDx.Elements > 0 ? typeDescDx.Elements : 1);
                memberDesc.mOffset = memberDescDx.StartOffset;
                memberDesc.mArraySize = typeDescDx.Elements;
                switch (typeDescDx.Class) {
                case D3D_SVC_SCALAR: {
                    switch (typeDescDx.Type) {
                    case D3D_SVT_BOOL: memberDesc.mType = ShaderReflection::UniformType::BOOL; break;
                    case D3D_SVT_INT: ShaderReflection::UniformType::INT; break;
                    case D3D_SVT_FLOAT: ShaderReflection::UniformType::FLOAT;  break;
                    }
	                }break;
                case D3D_SVC_VECTOR: {
                    switch (typeDescDx.Columns) {
                    case 2: memberDesc.mType = ShaderReflection::UniformType::VEC2; break;
                    case 3: memberDesc.mType = ShaderReflection::UniformType::VEC3; break;
                    case 4: memberDesc.mType = ShaderReflection::UniformType::VEC4;  break;
                    }
                }break;
                case D3D_SVC_MATRIX_COLUMNS:
                case D3D_SVC_MATRIX_ROWS: {
                    switch (typeDescDx.Rows) {
                    case 3: memberDesc.mType = ShaderReflection::UniformType::MAT3; break;
                    case 4: memberDesc.mType = ShaderReflection::UniformType::MAT4;  break;
                    }
                }break;
                default: ;
                }
                //typeDescDx.Type

                uniform.mMembers.push_back(memberDesc);
            }

            reflection.mUniforms.push_back(uniform);
        }
        if (shaderInputBindDesc.Type == D3D_SIT_TEXTURE) {
            if (reflection.mNameToUniforms.contains(shaderInputBindDesc.Name)) {
                reflection.mUniforms[reflection.mNameToUniforms.at(shaderInputBindDesc.Name)].mShaderMask |= (size_t)shaderType;
                continue;
            }

            ShaderReflection::Uniform uniform;
            uniform.mName = shaderInputBindDesc.Name;
            uniform.mType = ShaderReflection::UniformType::SAMPLER_2D;
            uniform.mRootId = reflection.mUniforms.size();
            uniform.mBind = shaderInputBindDesc.BindPoint;
            uniform.mSet = shaderInputBindDesc.Space;

            const CD3DX12_DESCRIPTOR_RANGE1 srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                1u,
                shaderInputBindDesc.BindPoint,
                shaderInputBindDesc.Space,
                D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

            //descriptorRanges.push_back(srvRange);

            //const D3D12_ROOT_PARAMETER1 rootParameter
            //{
            //    .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            //    .DescriptorTable =
            //    {
            //        .NumDescriptorRanges = 1u,
            //        .pDescriptorRanges = &descriptorRanges.back(),
            //},
            //.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
            //};

            reflection.mUniforms.push_back(uniform);
        }
        if (shaderInputBindDesc.Type == D3D_SIT_STRUCTURED) {
            if (reflection.mNameToUniforms.contains(shaderInputBindDesc.Name)) {
                reflection.mUniforms[reflection.mNameToUniforms.at(shaderInputBindDesc.Name)].mShaderMask |= (size_t)shaderType;
                continue;
            }

            ShaderReflection::Uniform uniform;
            uniform.mName = shaderInputBindDesc.Name;
            uniform.mType = ShaderReflection::UniformType::STORAGE_BUFFER;
            uniform.mRootId = reflection.mUniforms.size();
            uniform.mBind = shaderInputBindDesc.BindPoint;
            uniform.mSet = shaderInputBindDesc.Space;
            uniform.mShaderMask |= (size_t)shaderType;
            reflection.mNameToUniforms[shaderInputBindDesc.Name] = reflection.mUniforms.size();


            reflection.mUniforms.push_back(uniform);
        }
    }

    //shader.shaderBlob = compiledShaderBlob;

    //ComPtr<IDxcBlob> rootSignatureBlob{nullptr};
    //if (extractRootSignature) {
    //    compiledShaderBuffer->GetOutput(DXC_OUT_ROOT_SIGNATURE, IID_PPV_ARGS(&rootSignatureBlob), nullptr);
    //    shader.rootSignatureBlob = rootSignatureBlob;
    //}

	return compiledShaderBlob;
}

ComPtr<ID3DBlob> d3dUtil::CompileShaderFromHlsl(const std::string& hlslSource, const std::wstring& entrypoint, const std::wstring& target) {
    if (!utils) {
        ThrowIfFailed(::DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)));
        ThrowIfFailed(::DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)));
        ThrowIfFailed(utils->CreateDefaultIncludeHandler(&includeHandler));
    }

    std::vector<LPCWSTR> compilationArguments = {
        L"-HV",
        L"2021",
        L"-E",
        entrypoint.data(),
        L"-T",
        target.c_str(),
        DXC_ARG_PACK_MATRIX_ROW_MAJOR,
        DXC_ARG_WARNINGS_ARE_ERRORS,
        DXC_ARG_ALL_RESOURCES_BOUND,
    };

#if defined(DEBUG) || defined(_DEBUG)
    compilationArguments.push_back(DXC_ARG_DEBUG);
    compilationArguments.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
#endif

    const DxcBuffer sourceBuffer = {
        .Ptr = hlslSource.data(),
        .Size = hlslSource.size(),
        .Encoding = DXC_CP_UTF8,
    };

    ComPtr<IDxcResult> compiledShaderBuffer{};
    const HRESULT hr = compiler->Compile(&sourceBuffer, compilationArguments.data(),
        static_cast<uint32_t>(compilationArguments.size()), includeHandler.Get(),
        IID_PPV_ARGS(&compiledShaderBuffer));
    if (FAILED(hr)) {
        ThrowIfFailed(hr);
    }

    ComPtr<IDxcBlobUtf8> errors{};
    ThrowIfFailed(compiledShaderBuffer->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr));
    if (errors && errors->GetStringLength() > 0) {
        const LPCSTR errorMessage = errors->GetStringPointer();
        std::wcout << errorMessage << std::endl;
    }

    ComPtr<ID3DBlob> compiledShaderBlob{nullptr};
    ThrowIfFailed(compiledShaderBuffer->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&compiledShaderBlob), nullptr));
    return compiledShaderBlob;
}

void d3dUtil::OneTimeSubmit(std::function<void(ID3D12GraphicsCommandList*)> func) {
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.NodeMask = 1;

    ComPtr<ID3D12CommandQueue> cmd_queue;
    GetDriver()->getDevice()->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(cmd_queue.GetAddressOf()));

    ComPtr<ID3D12CommandAllocator> cmd_alloc;
    GetDriver()->getDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(cmd_alloc.GetAddressOf()));

    ComPtr<ID3D12GraphicsCommandList> cmd_list;
    GetDriver()->getDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc.Get(), NULL,
        IID_PPV_ARGS(cmd_list.GetAddressOf()));

    cmd_list->Close();

    GetDriver()->beginCommandList(cmd_alloc.Get(), cmd_list.Get());
    func(cmd_list.Get());
    GetDriver()->endCommandList(cmd_queue.Get(), cmd_list.Get(), true);
}

Dx12Resource d3dUtil::CreateBuffer(uint64_t size, D3D12_HEAP_TYPE heapType) {
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
    const D3D12_RESOURCE_STATES initialState = (heapType == D3D12_HEAP_TYPE_UPLOAD)
        ? D3D12_RESOURCE_STATE_GENERIC_READ
        : D3D12_RESOURCE_STATE_COMMON;
    return CreateResource(desc, heapType, initialState);
}

Dx12Resource d3dUtil::CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, D3D12MA::ALLOCATION_FLAGS flags) {
    D3D12MA::ALLOCATION_DESC allocDesc{};
    allocDesc.HeapType = heapType;
    allocDesc.Flags = flags;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation;
    ThrowIfFailed(GetDriver()->getAllocator()->CreateResource(
        &allocDesc,
        &desc,
        initialState,
        clearValue,
        allocation.GetAddressOf(),
        IID_PPV_ARGS(&resource)));
    return Dx12Resource(std::move(resource), std::move(allocation));
}

DriverDx12* d3dUtil::mDriver = nullptr;

DriverDx12* d3dUtil::GetDriver() {
    if (mDriver) {
        return mDriver;
    }
    return static_cast<DriverDx12*>(DriverInterface::Get());
}


#endif
