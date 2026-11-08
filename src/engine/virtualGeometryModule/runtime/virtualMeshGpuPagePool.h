#pragma once

#include "renderModule/frame/gpuCompletion.h"
#include "renderModule/frame/renderOwner.h"

#include <cstdint>
#include <memory>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>
#include <vector>

namespace IKIGAI::RENDER {
class IndexBufferInterface;
class Renderer;
class VertexBufferInterface;
}

namespace IKIGAI::VG {
struct VMeshPageData;

struct VirtualMeshGpuPagePoolSettings {
	static constexpr uint32_t DefaultSlotCount = 64;
	static constexpr uint32_t DefaultMaxPageBytes = 64u * 1024u;
	uint32_t SlotCount = DefaultSlotCount;
	uint32_t MaxPageBytes = DefaultMaxPageBytes;

	template <class C> static void serde(C& context, VirtualMeshGpuPagePoolSettings& value) {
		using Self = VirtualMeshGpuPagePoolSettings;
		using serde::attribute::default_;
		serde::serde_struct(context, value)
			.field(&Self::SlotCount, "SlotCount", default_ { DefaultSlotCount })
			.field(&Self::MaxPageBytes, "MaxPageBytes", default_ { DefaultMaxPageBytes });
	}
};

// Owner-thread only. Initialize in an explicit startup frame after Renderer::begin().
// Drain recorded work and release leases before destroying the pool. The same
// renderer and its driver must outlive the pool and every lease.
class VirtualMeshGpuPagePool final {
public:
	struct Ticket {
		uint64_t Pool = 0;
		uint64_t Generation = 0;
		uint32_t Slot = 0;
		bool operator==(const Ticket&) const = default;
	};

	enum class State { Invalid, Free, Uploading, Ready, Failed, Retiring };
	enum class UploadStatus { Recorded, NoSlot, InvalidPage, Oversize, InvalidFrame, RecordingFailed };
	struct UploadAdmission {
		UploadStatus Status = UploadStatus::InvalidFrame;
		Ticket Key {};
	};

	class Lease final {
	public:
		[[nodiscard]] Ticket ticket() const;
		[[nodiscard]] const std::shared_ptr<RENDER::VertexBufferInterface>& vertices() const;
		[[nodiscard]] const std::shared_ptr<RENDER::IndexBufferInterface>& indices() const;

	private:
		friend class VirtualMeshGpuPagePool;
		Lease() = default;
		Ticket mTicket {};
		std::shared_ptr<RENDER::VertexBufferInterface> mVertices {};
		std::shared_ptr<RENDER::IndexBufferInterface> mIndices {};
	};
	using SlotLease = std::shared_ptr<const Lease>;

	explicit VirtualMeshGpuPagePool(RENDER::Renderer& renderer, VirtualMeshGpuPagePoolSettings settings = {});
	~VirtualMeshGpuPagePool();
	VirtualMeshGpuPagePool(const VirtualMeshGpuPagePool&) = delete;
	VirtualMeshGpuPagePool& operator=(const VirtualMeshGpuPagePool&) = delete;

	// Record a validated page before passes. A RecordingFailed result with a ticket must still be
	// retired because part of the command may already have reached native recording.
	[[nodiscard]] UploadAdmission recordUpload(RENDER::Renderer& renderer, std::shared_ptr<const VMeshPageData> page);
	[[nodiscard]] State state(Ticket ticket) const;
	[[nodiscard]] RENDER::GpuCompletionToken uploadToken(Ticket ticket) const;
	[[nodiscard]] SlotLease acquire(Ticket ticket) const;

	// Call before recording a read and hold the lease across that recording.
	// If this fails, the caller must skip the read and use its fallback.
	bool markRead(RENDER::Renderer& renderer, const SlotLease& lease);
	bool retire(Ticket ticket);
	void collect();

	// Conservative buffer-capacity charge, excluding native allocator overhead.
	[[nodiscard]] uint64_t physicalBytes() const;
	// CPU payloads retained by pending upload commands, bounded by slot capacity.
	[[nodiscard]] uint64_t pendingUploadBytes() const;

private:
	struct Slot;
	[[nodiscard]] const Slot* matching(Ticket ticket) const;
	[[nodiscard]] Slot* matching(Ticket ticket);

	RENDER::RenderOwner mOwner {};
	RENDER::Renderer& mRenderer;
	const RENDER::GpuCompletionSource* mCompletion = nullptr;
	VirtualMeshGpuPagePoolSettings mSettings {};
	uint64_t mPoolId = 0;
	std::vector<Slot> mSlots;
	uint64_t mPendingUploadBytes = 0;
};
}
