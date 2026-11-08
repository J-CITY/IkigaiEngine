#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>
#include <span>
#include <vector>

namespace IKIGAI::VG {
struct VMeshPagedMetadata;
struct VirtualGeometryResidencyBudget {
	static constexpr uint64_t DefaultPageBytes = 64u * 1024u * 1024u;
	static constexpr uint32_t DefaultRequests = 16;
	uint64_t PageBytes = DefaultPageBytes;
	uint32_t Requests = DefaultRequests;
	template <class C> static void serde(C& context, VirtualGeometryResidencyBudget& value) {
		using serde::attribute::default_;
		serde::serde_struct(context, value)
			.field(&VirtualGeometryResidencyBudget::PageBytes, "PageBytes", default_ { DefaultPageBytes })
			.field(&VirtualGeometryResidencyBudget::Requests, "Requests", default_ { DefaultRequests });
	}
};

// One owner-thread controller per validated asset generation. All tokens come from one GPU completion domain.
class VirtualGeometryResidency final {
	struct Owner;
	struct PageLease { };

public:
	enum class State { Missing, Request, Reading, Upload, Resident, Retire, Error };
	enum class ReadResult { Ready, Failed, Cancelled };
	enum class Direction { Refine, Coarsen };

	struct Ticket {
		uint64_t AssetGeneration = 0;
		uint64_t Attempt = 0;
		uint32_t Page = 0;
		bool operator==(const Ticket&) const = default;
	};

	class Cut final {
	public:
		[[nodiscard]] uint64_t generation() const;
		[[nodiscard]] std::span<const uint32_t> clusters() const;
		[[nodiscard]] std::span<const uint32_t> pages() const;

	private:
		friend class VirtualGeometryResidency;
		Cut() = default;
		std::shared_ptr<const Owner> mOwner;
		std::vector<uint32_t> mClusters;
		std::vector<uint32_t> mPages;
		std::vector<std::shared_ptr<const PageLease>> mLeases;
	};

	using CutSnapshot = std::shared_ptr<const Cut>;
	VirtualGeometryResidency(std::shared_ptr<const VMeshPagedMetadata> metadata, uint64_t generation, VirtualGeometryResidencyBudget budget);
	VirtualGeometryResidency(const VirtualGeometryResidency&) = delete;
	VirtualGeometryResidency& operator=(const VirtualGeometryResidency&) = delete;

	// Root pages get the first request slots and reserve their cooked payload bytes up front.
	[[nodiscard]] bool rootFits() const;
	[[nodiscard]] uint64_t reservedBytes() const;
	[[nodiscard]] State state(uint32_t page) const;
	bool request(uint32_t page);
	[[nodiscard]] std::vector<Ticket> takeRequests();
	bool finishRead(Ticket ticket, ReadResult result);
	// Call only before a native upload has been submitted.
	bool cancelUpload(Ticket ticket);
	bool finishUpload(Ticket ticket, bool success, uint64_t submission);
	void collect(uint64_t completedSubmission);

	[[nodiscard]] CutSnapshot rootCut() const;
	// Return the source snapshot on failure. Submit coupled transitions together, in any order.
	[[nodiscard]] CutSnapshot changeCut(const CutSnapshot& source, std::span<const uint32_t> transitions, Direction direction) const;
	bool markSubmitted(const CutSnapshot& cut, uint64_t submission);
	// A held cut prevents eviction. Once dropped, its last GPU reader still holds the budget
	// until completion. The caller keeps the old controller through drain.
	bool retire(uint32_t page);

private:
	struct Page {
		State Status = State::Missing;
		bool Pinned = false;
		bool UploadSucceeded = false;
		uint64_t Attempt = 0;
		uint64_t UploadSubmission = 0;
		uint64_t LastReader = 0;
		std::shared_ptr<PageLease> Lease;
	};

	[[nodiscard]] Page* matching(Ticket ticket, State expected);
	bool queue(uint32_t page);
	void release(uint32_t page, State next);
	[[nodiscard]] CutSnapshot makeCut(std::vector<uint32_t> clusters) const;

	std::shared_ptr<const Owner> mOwner;
	VirtualGeometryResidencyBudget mBudget;
	std::vector<Page> mPages;
	std::vector<uint32_t> mRootPages;
	std::vector<Ticket> mPending;
	size_t mNextRoot = 0;
	uint64_t mReservedBytes = 0;
	uint64_t mCompletedSubmission = 0;
	uint32_t mActiveRequests = 0;
	bool mRootFits = false;
};
}
