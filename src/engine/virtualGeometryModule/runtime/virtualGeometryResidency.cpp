#include "virtualGeometryResidency.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

#include "virtualGeometryModule/format/vmeshIO.h"

namespace IKIGAI::VG {
struct VirtualGeometryResidency::Owner {
	std::shared_ptr<const VMeshPagedMetadata> Metadata;
	uint64_t Generation = 0;
};

uint64_t VirtualGeometryResidency::Cut::generation() const {
	return mOwner->Generation;
}
std::span<const uint32_t> VirtualGeometryResidency::Cut::clusters() const {
	return mClusters;
}
std::span<const uint32_t> VirtualGeometryResidency::Cut::pages() const {
	return mPages;
}

VirtualGeometryResidency::VirtualGeometryResidency(
	std::shared_ptr<const VMeshPagedMetadata> metadata, uint64_t generation, VirtualGeometryResidencyBudget budget)
	: mOwner(std::make_shared<Owner>(Owner { std::move(metadata), generation }))
	, mBudget(budget) {
	if (!mOwner->Metadata || !generation) {
		throw std::invalid_argument("VG residency requires validated metadata and a nonzero asset generation");
	}
	const auto& asset = *mOwner->Metadata;
	mPages.resize(asset.Pages.size());
	mPending.reserve(std::min<size_t>(budget.Requests, mPages.size()));
	for (const auto cluster : asset.Roots) {
		const auto pageId = asset.ClusterPages.at(cluster);
		auto& page = mPages.at(pageId);
		if (!page.Pinned) {
			page.Pinned = true;
			mRootPages.push_back(pageId);
		}
	}
	uint64_t rootBytes = 0;
	for (const auto page : mRootPages) {
		const auto bytes = asset.Pages[page].Size;
		if (bytes > budget.PageBytes - rootBytes) {
			return;
		}
		rootBytes += bytes;
	}
	mRootFits = !mRootPages.empty() && budget.Requests > 0;
	if (mRootFits) {
		mReservedBytes = rootBytes;
	}
}

bool VirtualGeometryResidency::rootFits() const {
	return mRootFits;
}
uint64_t VirtualGeometryResidency::reservedBytes() const {
	return mReservedBytes;
}
VirtualGeometryResidency::State VirtualGeometryResidency::state(uint32_t page) const {
	return mPages.at(page).Status;
}

bool VirtualGeometryResidency::request(uint32_t pageId) {
	const auto& page = mPages.at(pageId);
	return mRootFits && (page.Pinned || mNextRoot == mRootPages.size()) && queue(pageId);
}

bool VirtualGeometryResidency::queue(uint32_t pageId) {
	auto& page = mPages[pageId];
	if ((page.Status != State::Missing && page.Status != State::Error) || mActiveRequests == mBudget.Requests) {
		return false;
	}
	const auto bytes = page.Pinned ? 0 : mOwner->Metadata->Pages[pageId].Size;
	if (bytes > mBudget.PageBytes - mReservedBytes) {
		return false;
	}
	if (page.Attempt == std::numeric_limits<uint64_t>::max()) {
		throw std::overflow_error("VG page request attempt counter exhausted");
	}
	mPending.push_back({ mOwner->Generation, page.Attempt + 1, pageId });
	++page.Attempt;
	page.Status = State::Request;
	++mActiveRequests;
	mReservedBytes += bytes;
	return true;
}

std::vector<VirtualGeometryResidency::Ticket> VirtualGeometryResidency::takeRequests() {
	if (mRootFits) {
		while (mNextRoot < mRootPages.size() && mActiveRequests < mBudget.Requests) {
			queue(mRootPages[mNextRoot]);
			++mNextRoot;
		}
	}
	auto requests = mPending;
	for (const auto ticket : requests) {
		mPages[ticket.Page].Status = State::Reading;
	}
	mPending.clear();
	return requests;
}

VirtualGeometryResidency::Page* VirtualGeometryResidency::matching(Ticket ticket, State expected) {
	if (ticket.AssetGeneration != mOwner->Generation || ticket.Page >= mPages.size()) {
		return nullptr;
	}
	auto& page = mPages[ticket.Page];
	return page.Status == expected && page.Attempt == ticket.Attempt ? &page : nullptr;
}

bool VirtualGeometryResidency::finishRead(Ticket ticket, ReadResult result) {
	auto* page = matching(ticket, State::Reading);
	if (!page) {
		return false;
	}
	if (result == ReadResult::Ready) {
		page->Status = State::Upload;
	}
	else {
		--mActiveRequests;
		release(ticket.Page, result == ReadResult::Cancelled ? State::Missing : State::Error);
	}
	return true;
}

bool VirtualGeometryResidency::cancelUpload(Ticket ticket) {
	const auto* page = matching(ticket, State::Upload);
	if (!page || page->UploadSubmission) {
		return false;
	}
	--mActiveRequests;
	release(ticket.Page, State::Missing);
	return true;
}

bool VirtualGeometryResidency::finishUpload(Ticket ticket, bool success, uint64_t submission) {
	auto* page = matching(ticket, State::Upload);
	if (!page || page->UploadSubmission || !submission) {
		return false;
	}
	page->UploadSubmission = submission;
	page->UploadSucceeded = success;
	return true;
}

void VirtualGeometryResidency::collect(uint64_t completedSubmission) {
	mCompletedSubmission = std::max(mCompletedSubmission, completedSubmission);
	for (size_t id = 0; id < mPages.size(); ++id) {
		auto& page = mPages[id];
		if (page.Status == State::Upload && page.UploadSubmission && page.UploadSubmission <= mCompletedSubmission) {
			if (page.UploadSucceeded) {
				page.Lease = std::make_shared<PageLease>();
				page.Status = State::Resident;
			}
			else {
				release(static_cast<uint32_t>(id), State::Error);
			}
			--mActiveRequests;
		}
		else if (page.Status == State::Retire && page.LastReader <= mCompletedSubmission) {
			release(static_cast<uint32_t>(id), State::Missing);
		}
	}
}

void VirtualGeometryResidency::release(uint32_t pageId, State next) {
	auto& page = mPages[pageId];
	if (!page.Pinned) {
		mReservedBytes -= mOwner->Metadata->Pages[pageId].Size;
	}
	page.Status = next;
	page.UploadSubmission = 0;
	page.UploadSucceeded = false;
	page.LastReader = 0;
	page.Lease.reset();
}

VirtualGeometryResidency::CutSnapshot VirtualGeometryResidency::rootCut() const {
	if (!mRootFits) {
		return {};
	}
	auto roots = mOwner->Metadata->Roots;
	std::ranges::sort(roots);
	return makeCut(std::move(roots));
}

VirtualGeometryResidency::CutSnapshot VirtualGeometryResidency::makeCut(std::vector<uint32_t> clusters) const {
	std::vector<uint32_t> pages;
	pages.reserve(clusters.size());
	for (const auto cluster : clusters) {
		const auto pageId = mOwner->Metadata->ClusterPages[cluster];
		if (mPages[pageId].Status != State::Resident) {
			return {};
		}
		pages.push_back(pageId);
	}
	std::ranges::sort(pages);
	pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

	auto cut = std::shared_ptr<Cut>(new Cut);
	cut->mOwner = mOwner;
	cut->mClusters = std::move(clusters);
	cut->mPages = std::move(pages);
	cut->mLeases.reserve(cut->mPages.size());
	for (const auto page : cut->mPages) {
		cut->mLeases.push_back(mPages[page].Lease);
	}
	return cut;
}

VirtualGeometryResidency::CutSnapshot VirtualGeometryResidency::changeCut(
	const CutSnapshot& source, std::span<const uint32_t> transitions, Direction direction) const {
	if (!source || source->mOwner != mOwner || transitions.empty()) {
		return source;
	}
	struct Membership {
		bool Removed = false;
		bool Added = false;
	};
	const auto& asset = *mOwner->Metadata;
	const auto clusters = std::span(asset.TransitionClusters);
	std::map<uint32_t, Membership> changes;
	const auto add = [&](std::span<const uint32_t> side, bool Membership::* role) {
		for (const auto cluster : side) {
			auto& membership = changes[cluster];
			if (membership.*role) {
				return false;
			}
			membership.*role = true;
		}
		return true;
	};
	for (const auto id : transitions) {
		if (id >= asset.Transitions.size()) {
			return source;
		}
		const auto& transition = asset.Transitions[id];
		const auto coarse = clusters.subspan(transition.CoarseFirst, transition.CoarseCount);
		const auto fine = clusters.subspan(transition.FineFirst, transition.FineCount);
		const bool refine = direction == Direction::Refine;
		if (!add(refine ? coarse : fine, &Membership::Removed) || !add(refine ? fine : coarse, &Membership::Added)) {
			return source;
		}
	}
	std::vector<uint32_t> removed;
	std::vector<uint32_t> added;
	for (const auto& [cluster, membership] : changes) {
		const bool inSource = std::ranges::binary_search(source->mClusters, cluster);
		// Internal edges require a unique producer and consumer and cannot already be visible.
		if (inSource != (membership.Removed && !membership.Added)) {
			return source;
		}
		if (membership.Removed != membership.Added) {
			(membership.Removed ? removed : added).push_back(cluster);
		}
	}
	std::vector<uint32_t> kept;
	std::vector<uint32_t> target;
	std::ranges::set_difference(source->mClusters, removed, std::back_inserter(kept));
	std::ranges::set_union(kept, added, std::back_inserter(target));
	const auto cut = makeCut(std::move(target));
	return cut ? cut : source;
}

bool VirtualGeometryResidency::markSubmitted(const CutSnapshot& cut, uint64_t submission) {
	if (!cut || cut->mOwner != mOwner || !submission) {
		return false;
	}
	for (const auto id : cut->mPages) {
		auto& page = mPages[id];
		page.LastReader = std::max(page.LastReader, submission);
	}
	return true;
}

bool VirtualGeometryResidency::retire(uint32_t pageId) {
	auto& page = mPages.at(pageId);
	if (page.Pinned || page.Status != State::Resident || page.Lease.use_count() != 1) {
		return false;
	}
	page.Status = State::Retire;
	return true;
}
}
