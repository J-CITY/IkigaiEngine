#include "driverDx12.h"
#ifdef DX12_BACKEND
#include "textureDx12.h"
#include <iostream>
#ifdef IKIGAI_USE_STREAMLINE
#include "../streamlineRuntime.h"
#include "deviceHealthDx12.h"
namespace IKIGAI::RENDER {
void DriverDx12::initializeStreamline() {
	try {
		mDlssRuntime = AcquireStreamline(sl::RenderAPI::eD3D12);
	} catch (const std::exception& e) {
		std::cerr << "STREAMLINE_UNAVAILABLE " << e.what() << '\n';
	}
}
void DriverDx12::bindStreamlineDevice() {
	if (!mDlssRuntime) {
		return;
	}
	auto runtime = std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime);
	StreamlineRuntime::check(runtime->setD3DDevice(mDevice.Get()), "slSetD3DDevice");
	auto luid = mDevice->GetAdapterLuid();
	sl::AdapterInfo info {};
	info.deviceLUID = reinterpret_cast<uint8_t*>(&luid);
	info.deviceLUIDSizeInBytes = sizeof(luid);
	runtime->deviceReady(info);
}
StreamlineCallbacks DriverDx12::streamlineCallbacks() {
	return { [this](const std::shared_ptr<TextureInterface>& texture, bool write) {
				auto t = std::dynamic_pointer_cast<TextureDx12>(texture);
				if (!t) {
					throw UnsupportedFrameGeneration("Streamline requires DX12 textures");
				}
				const auto state = write ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
										 : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
				t->setState(mCommandList.Get(), state);
				mRayFrameResources.push_back(t);
				sl::Resource r { sl::ResourceType::eTex2d, t->getResource().Get(), uint32_t(state) };
				r.width = texture->getWidth();
				r.height = texture->getHeight();
				r.nativeFormat = t->getResource()->GetDesc().Format;
				r.flags = 0;
				r.mipLevels = texture->getMipCount();
				r.arrayLayers = 1;
				return r;
			},
		[this]() {
			invalidateCommandState();
			return static_cast<void*>(mCommandList.Get());
		},
		[this]() {
			auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
			mCommandList->ResourceBarrier(1, &barrier);
		},
		[this](bool enable) { streamlinePresenter(enable); },
		[this](void* nativeFence, uint64_t value) {
			if (!nativeFence || !value) {
				return;
			}
			auto* fence = static_cast<ID3D12Fence*>(nativeFence);
			mDeviceHealth->waitForFence(fence, value, "Wait for Streamline fence", mFenceEvent, true);
		},
		[this]() {
			if (mShuttingDown || hasGpuFailure()) {
				return;
			}
			end();
			wait();
			mDeviceHealth->check(mSwapchains.at(mCurrentWindowID).mSwapChain->Present(0, 0), "Streamline Present");
			mFrameId = mSwapchains.at(mCurrentWindowID).mSwapChain->GetCurrentBackBufferIndex();
			wait();
			begin();
		},
		[this]() {
			if (!settings.pipelined()) {
				return;
			}
			const auto token = completion().lastSubmitted();
			if (completion().isComplete(token)) {
				return;
			}
			for (const auto& slot : mRecordingSlots) {
				if (slot.Completion == token) {
					mDeviceHealth->waitForFence(mFence.Get(), slot.FenceValue, "Wait for Streamline submissions", mFenceEvent, true);
					collectCompletedFrames();
					return;
				}
			}
			throw std::logic_error("Streamline submission fence not found");
		},
		[](void* nativeFence) -> std::shared_ptr<void> {
			auto* fence = static_cast<ID3D12Fence*>(nativeFence);
			fence->AddRef();
			return { fence, [](void* retained) { static_cast<ID3D12Fence*>(retained)->Release(); } };
		} };
}
void DriverDx12::streamlinePresenter(bool enable) {
	if (!mDlssRuntime) {
		throw UnsupportedFrameGeneration("Streamline runtime unavailable");
	}
	auto runtime = std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime);
	auto& ctx = mSwapchains.at(mCurrentWindowID);
	const auto requested
		= mFrameGeneration && mFrameGeneration->status().Technology == FrameGenerationType::DLSS ? mFrameGenerationSettings.multiplier : 0;
	if (enable && ctx.streamline && ctx.slMultiplier == requested) {
		return;
	}
	if (mShuttingDown) {
		return;
	}
	end();
	wait();
	runtime->clearTags();
	for (auto& fb : ctx.mDefaultFb) {
		fb.reset();
	}
	mCurrentState.mFrameBuffer.reset();
	clearForDestroy();
	ctx.mSwapChain.Reset();
	createSwapChain(mCurrentWindowID, ctx.width, ctx.height, enable);
	// SR-only presentation uses the common lifecycle hooks in DriverDx12;
	// it does not need to keep the FG swapchain proxy alive.
	ctx.streamline = enable;
	ctx.slMultiplier = enable ? requested : 0;
	mFrameId = ctx.mSwapChain->GetCurrentBackBufferIndex();
	createDefaultFrameBuffer(mCurrentWindowID, ctx.width, ctx.height);
	begin();
}
void DriverDx12::setTemporalCamera(const FrameGenerationInputs& f) {
	if (mDlssRuntime) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->camera(f);
	}
}
void DriverDx12::latencyFrameBegin() {
	DriverInterface::latencyFrameBegin();
	if (mDlssRuntime) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->beginFrame(uint32_t(mRealFrameId));
	}
}
void DriverDx12::latencySimulationEnd() {
	if (mDlssRuntime) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->marker(sl::PCLMarker::eSimulationEnd);
	}
}
void DriverDx12::latencySubmitBegin() {
	if (mDlssRuntime) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->marker(sl::PCLMarker::eRenderSubmitStart);
	}
}
void DriverDx12::latencySubmitEnd() {
	if (mDlssRuntime) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->marker(sl::PCLMarker::eRenderSubmitEnd);
	}
}
}
#else
namespace IKIGAI::RENDER {
void DriverDx12::initializeStreamline() { }
void DriverDx12::bindStreamlineDevice() { }
void DriverDx12::setTemporalCamera(const FrameGenerationInputs&) { }
void DriverDx12::latencyFrameBegin() {
	DriverInterface::latencyFrameBegin();
}
void DriverDx12::latencySimulationEnd() { }
void DriverDx12::latencySubmitBegin() { }
void DriverDx12::latencySubmitEnd() { }
}
#endif
#endif
