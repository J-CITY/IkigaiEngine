#include "deviceHealthDx12.h"

#ifdef DX12_BACKEND
#include "profilerModule/trace.h"
#include "utilsModule/crashReport.h"
#include "windowMessageWait.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	using Microsoft::WRL::ComPtr;

	constexpr DWORD FenceTimeoutMs = 30000;

	struct ScopedFenceEvent {
		HANDLE Handle = nullptr;
		~ScopedFenceEvent() {
			if (Handle) {
				CloseHandle(Handle);
			}
		}
	};

	void appendDred(std::ostream& output, ID3D12Device* device) {
		ComPtr<ID3D12DeviceRemovedExtendedData> dred;
		if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
			return;
		}
		D3D12_DRED_PAGE_FAULT_OUTPUT pageFault {};
		if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pageFault))) {
			output << "\n  DRED page-fault address=0x" << std::hex << pageFault.PageFaultVA << std::dec;
		}
		D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs {};
		if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs))) {
			auto* node = breadcrumbs.pHeadAutoBreadcrumbNode;
			for (unsigned scanned = 0, reported = 0; node && scanned < 1024 && reported < 16; node = node->pNext, ++scanned) {
				if (!node->pLastBreadcrumbValue || *node->pLastBreadcrumbValue >= node->BreadcrumbCount) {
					continue;
				}
				const auto completed = *node->pLastBreadcrumbValue;
				output << "\n  DRED queue=" << (node->pCommandQueueDebugNameA ? node->pCommandQueueDebugNameA : "(unnamed)")
					   << " list=" << (node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "(unnamed)") << " breadcrumbs=" << completed
					   << '/' << node->BreadcrumbCount;
				if (node->pCommandHistory) {
					output << " nextOp=" << node->pCommandHistory[completed];
				}
				++reported;
			}
		}
	}
}

DeviceHealthDx12::DeviceHealthDx12(ID3D12Device* device)
	: mDevice(device) { }

void DeviceHealthDx12::enableDred() {
	ComPtr<ID3D12DeviceRemovedExtendedDataSettings> settings;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&settings)))) {
		settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
	}
}

bool DeviceHealthDx12::isDeviceLost() const noexcept {
	return mDeviceLost.load();
}

bool DeviceHealthDx12::hasFailure() const noexcept {
	return mFailed.load();
}

void DeviceHealthDx12::requireHealthy() {
	if (mFailed.load()) {
		std::lock_guard lock(mMutex);
		throw std::runtime_error(mFailure);
	}
}

void DeviceHealthDx12::check(HRESULT result, const char* operation, bool completionUnknown) {
	if (FAILED(result)) {
		fail(result, operation, completionUnknown);
	}
}

void DeviceHealthDx12::execute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
	check(mDevice->GetDeviceRemovedReason(), "Before ExecuteCommandLists");
	{
		std::lock_guard lock(mMutex);
		if (mFailed.load()) {
			throw std::runtime_error(mFailure);
		}
		IKIGAI_PROFILE_SCOPE("DX12 Execute Command Lists");
		queue->ExecuteCommandLists(count, lists);
	}
	check(mDevice->GetDeviceRemovedReason(), "ExecuteCommandLists");
}

UINT64 DeviceHealthDx12::completedValue(ID3D12Fence* fence, const char* operation) {
	const auto completed = fence->GetCompletedValue();
	if (completed == (std::numeric_limits<UINT64>::max)()) {
		fail(DXGI_ERROR_DEVICE_REMOVED, operation);
	}
	return completed;
}

void DeviceHealthDx12::waitForFence(ID3D12Fence* fence, UINT64 value, const char* operation, HANDLE event, bool pumpMessages) {
	if (completedValue(fence, operation) >= value) {
		return;
	}
	IKIGAI_PROFILE_SCOPE("DX12 Wait for GPU Fence");
	IKIGAI_PROFILE_ZONE_TEXT(operation, std::strlen(operation));
	ScopedFenceEvent localEvent;
	if (!event) {
		localEvent.Handle = CreateEventExW(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
		if (!localEvent.Handle) {
			fail(HRESULT_FROM_WIN32(GetLastError()), operation, true);
		}
		event = localEvent.Handle;
	}
	const auto registration = fence->SetEventOnCompletion(value, event);
	if (FAILED(registration)) {
		fail(registration, operation, true);
	}

	const auto deadline = GetTickCount64() + FenceTimeoutMs;
	while (completedValue(fence, operation) < value) {
		const auto now = GetTickCount64();
		if (now >= deadline) {
			fail(HRESULT_FROM_WIN32(WAIT_TIMEOUT), operation, true);
		}
		const auto slice = static_cast<DWORD>((std::min)(deadline - now, 100ULL));
		const auto result = pumpMessages ? WaitWithWindowMessages(event, slice) : WaitForSingleObject(event, slice);
		if (result == WAIT_FAILED) {
			fail(HRESULT_FROM_WIN32(GetLastError()), operation, true);
		}
		if (result != WAIT_OBJECT_0 && result != WAIT_TIMEOUT && !(pumpMessages && result == WAIT_OBJECT_0 + 1)) {
			fail(E_UNEXPECTED, operation, true);
		}
	}
}

void DeviceHealthDx12::fail(HRESULT result, const char* operation, bool completionUnknown) {
	std::lock_guard lock(mMutex);
	const auto reason = mDevice->GetDeviceRemovedReason();
	const bool removed = FAILED(reason) || result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET
		|| result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
	const bool firstFailure = !mFailed.exchange(true);
	const bool firstRemoval = removed && !mDeviceLost.exchange(true);
	if (firstFailure || firstRemoval || (completionUnknown && !removed)) {
		const char* failureType = "operation failed";
		if (removed) {
			failureType = "device lost";
		}
		else if (result == HRESULT_FROM_WIN32(WAIT_TIMEOUT)) {
			failureType = "fence wait timed out";
		}
		std::ostringstream report;
		report << "DX12 " << failureType << " in " << operation << " (HRESULT=0x" << std::hex << static_cast<unsigned long>(result)
			   << ", removed reason=0x" << static_cast<unsigned long>(reason) << ')';
		if (removed) {
			appendDred(report, mDevice.Get());
		}
		const auto message = report.str();
		if (firstFailure) {
			mFailure = message;
		}
		std::cerr << message << '\n';
		// Unwinding could release resources still owned by the GPU.
		if (completionUnknown && !mDeviceLost.load()) {
			UTILS::CRASH::terminate(message.c_str());
		}
	}
	throw std::runtime_error(mFailure);
}
}

#endif
