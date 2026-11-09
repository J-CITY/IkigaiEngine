#pragma once

#ifdef DX12_BACKEND
#include <atomic>
#include <d3d12.h>
#include <mutex>
#include <string>
#include <wrl/client.h>

namespace IKIGAI::RENDER {
class DeviceHealthDx12 {
public:
	explicit DeviceHealthDx12(ID3D12Device* device);
	static void enableDred();
	bool isDeviceLost() const noexcept;
	bool hasFailure() const noexcept;

	void requireHealthy();
	void check(HRESULT result, const char* operation, bool completionUnknown = false);
	void execute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);
	UINT64 completedValue(ID3D12Fence* fence, const char* operation);
	void waitForFence(ID3D12Fence* fence, UINT64 value, const char* operation, HANDLE event = nullptr, bool pumpMessages = false);

private:
	[[noreturn]] void fail(HRESULT result, const char* operation, bool completionUnknown = false);

	Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
	mutable std::mutex mMutex;
	std::atomic_bool mDeviceLost { false };
	std::atomic_bool mFailed { false };
	std::string mFailure;
};
}
#endif
