#pragma once
#ifdef DX12_BACKEND
#include <d3d12.h>
#include <functional>
#include <memory>
#include <mutex>
#include <wrl/client.h>

namespace IKIGAI::RENDER {
class DeviceHealthDx12;
// Records, submits and waits for a one-off command list on a dedicated direct queue.
// The queue, allocator, list, fence and event are created once and reused, instead of per call.
class ImmediateSubmitDx12 {
public:
	ImmediateSubmitDx12(ID3D12Device* device, std::shared_ptr<DeviceHealthDx12> health);
	~ImmediateSubmitDx12();
	ImmediateSubmitDx12(const ImmediateSubmitDx12&) = delete;
	ImmediateSubmitDx12& operator=(const ImmediateSubmitDx12&) = delete;

	void submit(const std::function<void(ID3D12GraphicsCommandList*)>& record);

private:
	// Nested submissions from inside a recording callback use temporary objects.
	void submitTransient(const std::function<void(ID3D12GraphicsCommandList*)>& record);

	Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
	std::shared_ptr<DeviceHealthDx12> mHealth;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;
	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
	UINT64 mFenceValue = 0;
	HANDLE mEvent = nullptr;
	std::mutex mMutex;
};
}
#endif
