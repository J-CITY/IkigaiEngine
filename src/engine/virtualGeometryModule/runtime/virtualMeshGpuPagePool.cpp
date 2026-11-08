#include "virtualMeshGpuPagePool.h"

#include "renderModule/backends/interface/indexBufferInterface.h"
#include "renderModule/backends/interface/vertexBufferInterface.h"
#include "renderModule/frame/frameContext.h"
#include "renderModule/render.h"
#include "virtualGeometryModule/format/vmeshIO.h"

#ifdef VULKAN_BACKEND
#include "renderModule/backends/vk/indexBufferVk.h"
#include "renderModule/backends/vk/vertexBufferVk.h"
#endif
#ifdef DX12_BACKEND
#include "renderModule/backends/dx12/indexBufferDx12.h"
#include "renderModule/backends/dx12/vertexBufferDx12.h"
#endif

#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <utility>

namespace IKIGAI::VG {
namespace {
	constexpr uint64_t MaxPoolBytes = 512u * 1024u * 1024u;
	constexpr uint32_t MaxSlotCount = 4096;
	std::atomic<uint64_t> nextPoolId = 1;

	uint64_t AllocatePoolId() {
		auto id = nextPoolId.load(std::memory_order_relaxed);
		do {
			if (id == std::numeric_limits<uint64_t>::max()) {
				throw std::overflow_error("VG page pool identities exhausted");
			}
		} while (!nextPoolId.compare_exchange_weak(id, id + 1, std::memory_order_relaxed));
		return id;
	}

	enum class UploadOutcome { Pending, Succeeded, Failed };

	struct UploadCommand {
		std::shared_ptr<const VMeshPageData> Page {};
		std::atomic<UploadOutcome> Outcome = UploadOutcome::Pending;
	};
}

struct VirtualMeshGpuPagePool::Slot {
	State Status = State::Free;
	uint64_t Generation = 0;
	std::shared_ptr<RENDER::VertexBufferInterface> Vertices {};
	std::shared_ptr<RENDER::IndexBufferInterface> Indices {};
	SlotLease Published {};
	std::shared_ptr<UploadCommand> Upload {};
	std::shared_ptr<const RENDER::FrameSubmissionReceipt> UploadReceipt {};
	RENDER::GpuCompletionToken UploadToken {};
	std::shared_ptr<const RENDER::FrameSubmissionReceipt> ReadReceipt {};
	RENDER::GpuCompletionToken LastRead {};
	uint64_t UploadBytes = 0;
	bool RecordingFailed = false;

	// Keep the latest known read on the single ordered queue while allowing
	// at most one unresolved receipt per slot.
	void resolveRead(const RENDER::GpuCompletionSource& completion) {
		if (!ReadReceipt) {
			return;
		}
		const auto token = ReadReceipt->token();
		if (!token.Domain || !token.Value) {
			return;
		}
		completion.validate(token);
		if (token.Value > LastRead.Value) {
			LastRead = token;
		}
		ReadReceipt.reset();
	}
};

VirtualMeshGpuPagePool::Ticket VirtualMeshGpuPagePool::Lease::ticket() const {
	return mTicket;
}

const std::shared_ptr<RENDER::VertexBufferInterface>& VirtualMeshGpuPagePool::Lease::vertices() const {
	return mVertices;
}

const std::shared_ptr<RENDER::IndexBufferInterface>& VirtualMeshGpuPagePool::Lease::indices() const {
	return mIndices;
}

VirtualMeshGpuPagePool::VirtualMeshGpuPagePool(RENDER::Renderer& renderer, VirtualMeshGpuPagePoolSettings settings)
	: mRenderer(renderer)
	, mSettings(settings) {
	if (!settings.SlotCount || settings.SlotCount > MaxSlotCount || settings.MaxPageBytes < sizeof(VMeshVertex)
		|| settings.MaxPageBytes > VMeshMaxPageBytes || physicalBytes() > MaxPoolBytes) {
		throw std::invalid_argument("Invalid VG GPU page slot count or capacity");
	}
	if (!renderer.hasActiveFrame()) {
		throw std::logic_error("VG GPU page pool needs an active startup frame");
	}
	auto* driver = renderer.getDriver();
	if (!driver || !driver->hasOpenRecording()) {
		throw std::logic_error("VG GPU page pool needs native startup recording");
	}
	mCompletion = &driver->completion();
	mPoolId = AllocatePoolId();
	mSlots.resize(settings.SlotCount);
	const size_t vertexCapacity = settings.MaxPageBytes / sizeof(VMeshVertex);
	const size_t indexCapacity = settings.MaxPageBytes / sizeof(uint32_t);
	switch (RENDER::DriverInterface::settings.backend) {
#ifdef VULKAN_BACKEND
	case RENDER::RenderSettings::Backend::VULKAN:
		for (auto& slot : mSlots) {
			slot.Vertices = std::make_shared<RENDER::VertexBufferVk>(nullptr, vertexCapacity, sizeof(VMeshVertex));
			slot.Indices = std::make_shared<RENDER::IndexBufferVk>(nullptr, indexCapacity, sizeof(uint32_t));
		}
		break;
#endif
#ifdef DX12_BACKEND
	case RENDER::RenderSettings::Backend::DIRECTX12: {
		// The existing DX12 index constructor always records an initial copy.
		std::vector<uint32_t> initialIndices(indexCapacity, 0u);
		for (auto& slot : mSlots) {
			slot.Vertices = std::make_shared<RENDER::VertexBufferDx12>(nullptr, vertexCapacity, sizeof(VMeshVertex));
			slot.Indices = std::make_shared<RENDER::IndexBufferDx12>(initialIndices);
		}
		break;
	}
#endif
	default:
		throw std::runtime_error("VG GPU page pool requires Vulkan or DirectX 12");
	}
}

VirtualMeshGpuPagePool::~VirtualMeshGpuPagePool() = default;

VirtualMeshGpuPagePool::UploadAdmission VirtualMeshGpuPagePool::recordUpload(RENDER::Renderer& renderer, std::shared_ptr<const VMeshPageData> page) {
	mOwner.requireCurrentThread();
	if (&renderer != &mRenderer || !renderer.hasActiveFrame()) {
		return { UploadStatus::InvalidFrame, {} };
	}
	if (!page || page->Vertices.empty() || page->Indices.empty() || page->Indices.size() % 3u != 0u) {
		return { UploadStatus::InvalidPage, {} };
	}
	if (page->Vertices.capacity() > mSettings.MaxPageBytes / sizeof(VMeshVertex)) {
		return { UploadStatus::Oversize, {} };
	}
	const auto vertexBytes = page->Vertices.capacity() * sizeof(VMeshVertex);
	if (page->Indices.capacity() > (mSettings.MaxPageBytes - vertexBytes) / sizeof(uint32_t)) {
		return { UploadStatus::Oversize, {} };
	}
	const auto candidate = std::ranges::find_if(
		mSlots, [](const Slot& slot) { return slot.Status == State::Free && slot.Generation < std::numeric_limits<uint64_t>::max(); });
	if (candidate == mSlots.end()) {
		return { UploadStatus::NoSlot, {} };
	}
	auto& slot = *candidate;
	const auto uploadReceipt = renderer.submissionReceipt();
	auto command = std::make_shared<UploadCommand>();
	command->Page = std::move(page);
	const auto key = Ticket { mPoolId, ++slot.Generation, static_cast<uint32_t>(candidate - mSlots.begin()) };
	slot.Status = State::Uploading;
	slot.Upload = command;
	slot.UploadReceipt = uploadReceipt;
	slot.UploadBytes = vertexBytes + command->Page->Indices.capacity() * sizeof(uint32_t);
	mPendingUploadBytes += slot.UploadBytes;
	try {
		renderer.execute([command, vertices = slot.Vertices, indices = slot.Indices] {
			try {
				vertices->setData(command->Page->Vertices);
				indices->setData(command->Page->Indices);
				command->Outcome.store(UploadOutcome::Succeeded, std::memory_order_release);
			} catch (...) {
				command->Outcome.store(UploadOutcome::Failed, std::memory_order_release);
			}
		});
	} catch (...) {
		slot.RecordingFailed = true;
		return { UploadStatus::RecordingFailed, key };
	}
	return { UploadStatus::Recorded, key };
}

const VirtualMeshGpuPagePool::Slot* VirtualMeshGpuPagePool::matching(Ticket ticket) const {
	if (ticket.Pool != mPoolId || !ticket.Generation || ticket.Slot >= mSlots.size()) {
		return nullptr;
	}
	const auto& slot = mSlots[ticket.Slot];
	return slot.Generation == ticket.Generation && slot.Status != State::Free ? &slot : nullptr;
}

VirtualMeshGpuPagePool::Slot* VirtualMeshGpuPagePool::matching(Ticket ticket) {
	return const_cast<Slot*>(std::as_const(*this).matching(ticket));
}

VirtualMeshGpuPagePool::State VirtualMeshGpuPagePool::state(Ticket ticket) const {
	mOwner.requireCurrentThread();
	const auto* slot = matching(ticket);
	return slot ? slot->Status : State::Invalid;
}

RENDER::GpuCompletionToken VirtualMeshGpuPagePool::uploadToken(Ticket ticket) const {
	mOwner.requireCurrentThread();
	const auto* slot = matching(ticket);
	if (!slot) {
		return {};
	}
	const auto token = slot->UploadReceipt ? slot->UploadReceipt->token() : slot->UploadToken;
	if (token.Value) {
		mCompletion->validate(token);
	}
	return token;
}

VirtualMeshGpuPagePool::SlotLease VirtualMeshGpuPagePool::acquire(Ticket ticket) const {
	mOwner.requireCurrentThread();
	const auto* slot = matching(ticket);
	return slot && slot->Status == State::Ready ? slot->Published : nullptr;
}

bool VirtualMeshGpuPagePool::markRead(RENDER::Renderer& renderer, const SlotLease& lease) {
	mOwner.requireCurrentThread();
	if (&renderer != &mRenderer || !lease || !renderer.hasActiveFrame()) {
		return false;
	}
	auto* slot = matching(lease->ticket());
	if (!slot || slot->Status != State::Ready || slot->Published != lease) {
		return false;
	}
	const auto receipt = renderer.submissionReceipt();
	if (slot->ReadReceipt == receipt) {
		return true;
	}
	slot->resolveRead(*mCompletion);
	if (slot->ReadReceipt) {
		return false;
	}
	slot->ReadReceipt = receipt;
	return true;
}

bool VirtualMeshGpuPagePool::retire(Ticket ticket) {
	mOwner.requireCurrentThread();
	auto* slot = matching(ticket);
	if (!slot || slot->Status == State::Retiring) {
		return false;
	}
	slot->Status = State::Retiring;
	return true;
}

void VirtualMeshGpuPagePool::collect() {
	mOwner.requireCurrentThread();
	for (size_t index = 0; index < mSlots.size(); ++index) {
		auto& slot = mSlots[index];
		if (slot.UploadReceipt) {
			const auto token = slot.UploadReceipt->token();
			if (token.Domain && token.Value && mCompletion->isComplete(token)) {
				const bool uploaded = !slot.RecordingFailed && slot.Upload->Outcome.load(std::memory_order_acquire) == UploadOutcome::Succeeded;
				if (uploaded && slot.Status != State::Retiring) {
					auto lease = std::shared_ptr<Lease>(new Lease);
					lease->mTicket = { mPoolId, slot.Generation, static_cast<uint32_t>(index) };
					lease->mVertices = slot.Vertices;
					lease->mIndices = slot.Indices;
					slot.Published = std::move(lease);
				}
				slot.UploadToken = token;
				slot.Upload->Page.reset();
				slot.Upload.reset();
				slot.UploadReceipt.reset();
				mPendingUploadBytes -= slot.UploadBytes;
				slot.UploadBytes = 0;
				if (slot.Status != State::Retiring) {
					slot.Status = uploaded ? State::Ready : State::Failed;
				}
			}
		}
		slot.resolveRead(*mCompletion);
		if (slot.Status == State::Retiring && !slot.UploadReceipt && !slot.ReadReceipt
			&& (!slot.LastRead.Value || mCompletion->isComplete(slot.LastRead)) && (!slot.Published || slot.Published.use_count() == 1)) {
			slot.Published.reset();
			slot.UploadToken = {};
			slot.LastRead = {};
			slot.RecordingFailed = false;
			slot.Status = State::Free;
		}
	}
}

uint64_t VirtualMeshGpuPagePool::physicalBytes() const {
	mOwner.requireCurrentThread();
	return 2u * uint64_t(mSettings.SlotCount) * mSettings.MaxPageBytes;
}

uint64_t VirtualMeshGpuPagePool::pendingUploadBytes() const {
	mOwner.requireCurrentThread();
	return mPendingUploadBytes;
}
}
