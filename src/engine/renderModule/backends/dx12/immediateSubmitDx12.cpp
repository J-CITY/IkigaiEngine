#include "immediateSubmitDx12.h"
#ifdef DX12_BACKEND
#include "d3dUtil.h"
#include "d3dx12/DirectXHelpers.h"
#include "deviceHealthDx12.h"

namespace IKIGAI::RENDER {
namespace {
	thread_local bool tRecordingImmediate = false;

	struct RecordingScope {
		RecordingScope() {
			tRecordingImmediate = true;
		}
		~RecordingScope() {
			tRecordingImmediate = false;
		}
	};
}

ImmediateSubmitDx12::ImmediateSubmitDx12(ID3D12Device* device, std::shared_ptr<DeviceHealthDx12> health)
	: mDevice(device)
	, mHealth(std::move(health)) {
	D3D12_COMMAND_QUEUE_DESC queueDesc {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.NodeMask = 1;
	ThrowIfFailed(mDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(mQueue.GetAddressOf())));
	ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(mAllocator.GetAddressOf())));
	ThrowIfFailed(
		mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mAllocator.Get(), nullptr, IID_PPV_ARGS(mCommandList.GetAddressOf())));
	ThrowIfFailed(mCommandList->Close());
	ThrowIfFailed(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mFence.GetAddressOf())));
	DirectX::SetDebugObjectName(mFence.Get(), L"ResourceUploadBatch");
	mEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
	if (!mEvent) {
		throw std::runtime_error("Cannot create DX12 immediate submit event");
	}
}

ImmediateSubmitDx12::~ImmediateSubmitDx12() {
	if (mEvent) {
		CloseHandle(mEvent);
	}
}

void ImmediateSubmitDx12::submit(const std::function<void(ID3D12GraphicsCommandList*)>& record) {
	mHealth->requireHealthy();
	if (tRecordingImmediate) {
		submitTransient(record);
		return;
	}
	std::lock_guard lock(mMutex);
	RecordingScope recording;
	mHealth->check(mAllocator->Reset(), "Reset immediate command allocator");
	mHealth->check(mCommandList->Reset(mAllocator.Get(), nullptr), "Reset immediate command list");
	try {
		record(mCommandList.Get());
	} catch (...) {
		mCommandList->Close();
		throw;
	}
	mHealth->check(mCommandList->Close(), "Close immediate command list");
	ID3D12CommandList* lists[] = { mCommandList.Get() };
	mQueue->ExecuteCommandLists(1, lists);
	const auto value = ++mFenceValue;
	mHealth->check(mQueue->Signal(mFence.Get(), value), "Signal immediate submission fence");
	mHealth->waitForFence(mFence.Get(), value, "Wait for immediate submission fence", mEvent);
}

void ImmediateSubmitDx12::submitTransient(const std::function<void(ID3D12GraphicsCommandList*)>& record) {
	D3D12_COMMAND_QUEUE_DESC queueDesc {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.NodeMask = 1;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
	ThrowIfFailed(mDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(queue.GetAddressOf())));
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
	ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.GetAddressOf())));
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
	ThrowIfFailed(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(commandList.GetAddressOf())));
	record(commandList.Get());
	ThrowIfFailed(commandList->Close());
	ID3D12CommandList* lists[] = { commandList.Get() };
	queue->ExecuteCommandLists(1, lists);
	Microsoft::WRL::ComPtr<ID3D12Fence> fence;
	ThrowIfFailed(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())));
	mHealth->check(queue->Signal(fence.Get(), 1), "Signal transient submission fence");
	mHealth->waitForFence(fence.Get(), 1, "Wait for transient submission fence");
}
}
#endif
