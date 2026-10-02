#pragma once
#ifdef DX12_BACKEND
#include "renderModule/backends/interface/reflectionStructs.h"
#include <windows.h>
#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <string>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <functional>
#include <utility>

#define D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED
#include "D3D12MemAlloc.h"

class DxException {
public:
    DxException() = default;
    DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

    std::wstring ToString()const;

    HRESULT ErrorCode = S_OK;
    std::wstring FunctionName;
    std::wstring Filename;
    int LineNumber = -1;
};

inline std::wstring AnsiToWString(const std::string& str) {
    WCHAR buffer[512];
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
    return std::wstring(buffer);
}

namespace IKIGAI::RENDER
{
	struct ShaderReflection;
	class DriverDx12;

	class Dx12Resource {
	public:
		Dx12Resource() = default;
		Dx12Resource(Microsoft::WRL::ComPtr<ID3D12Resource> resource,
			Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation = nullptr)
			: mResource(std::move(resource)), mAllocation(std::move(allocation)) {}

		ID3D12Resource* Get() const { return mResource.Get(); }
		ID3D12Resource* operator->() const { return mResource.Get(); }
		explicit operator bool() const { return mResource != nullptr; }

		void Reset() {
			mResource.Reset();
			mAllocation.Reset();
		}

		Dx12Resource& operator=(std::nullptr_t) {
			Reset();
			return *this;
		}

	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> mResource;
		Microsoft::WRL::ComPtr<D3D12MA::Allocation> mAllocation;
	};

	class d3dUtil {
    public:
        static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(ShaderReflection& reflection, const std::wstring& filename, const D3D_SHADER_MACRO* defines, const std::wstring& entrypoint, const std::wstring& target, ShaderType shaderType);
        static Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFromHlsl(const std::string& hlslSource, const std::wstring& entrypoint, const std::wstring& target);

        static void OneTimeSubmit(std::function<void(ID3D12GraphicsCommandList*)> func);
        static Dx12Resource CreateBuffer(uint64_t size, D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT);
        static Dx12Resource CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_HEAP_TYPE heapType,
            D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue = nullptr,
            D3D12MA::ALLOCATION_FLAGS flags = D3D12MA::ALLOCATION_FLAG_NONE);

        static DriverDx12* mDriver;
        static DriverDx12* GetDriver();
    };
}

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    std::wstring wfn = AnsiToWString(__FILE__);                       \
    if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); } \
}
#endif

#ifndef ReleaseCom
#define ReleaseCom(x) { if(x){ x->Release(); x = 0; } }
#endif

#endif