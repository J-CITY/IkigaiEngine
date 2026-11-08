#include "virtualGeometryStreaming.h"
#include "renderModule/render.h"
#include "virtualMeshAsset.h"
#include "virtualMeshPageSource.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_set>

namespace IKIGAI::VG {
struct VirtualGeometryStreaming::Context {
	struct Page {
		VirtualGeometryResidency::Ticket Read {};
		VirtualMeshGpuPagePool::Ticket Gpu {};
		bool UploadSubmitted = false;
		uint64_t LastUsed = 0;
	};
	struct ReadingCut {
		VirtualGeometryResidency::CutSnapshot Cut {};
		std::shared_ptr<const RENDER::FrameSubmissionReceipt> Receipt {};
	};
	struct ViewCut {
		VirtualGeometryResidency::CutSnapshot Cut {};
		std::optional<uint64_t> PendingFeedback {};
		uint64_t LastSeen = 0;
	};
	std::weak_ptr<VirtualMeshAsset> Asset {};
	std::shared_ptr<const VirtualMeshPageSource> Source {};
	VirtualGeometryResidency Residency;
	std::vector<Page> Pages {};
	std::vector<ReadingCut> ReadingCuts {};
	std::map<std::pair<uint64_t, uint64_t>, ViewCut> ViewCuts {};
	uint64_t Generation = 0;
	uint64_t LastSeen = 0;
	std::vector<bool> RootPages {};
	uint32_t RootSlots = 0;
	bool Failed = false;
	Context(const std::shared_ptr<VirtualMeshAsset>& asset, uint64_t generation, uint64_t bytes, uint32_t requests)
		: Asset(asset)
		, Source(asset->pageSource())
		, Residency(std::shared_ptr<const VMeshPagedMetadata>(Source, &Source->metadata()), generation, { bytes, requests })
		, Pages(Source->metadata().Pages.size())
		, Generation(generation) {
		RootPages.resize(Pages.size(), false);
		for (const auto root : Source->metadata().Roots) {
			const auto page = Source->metadata().ClusterPages[root];
			if (!RootPages[page]) {
				RootPages[page] = true;
				++RootSlots;
			}
		}
	}
};

VirtualGeometryStreaming::VirtualGeometryStreaming(RENDER::Renderer& renderer, VirtualGeometryStreamingSettings settings)
	: mSettings([&] {
		if (!settings.MaxFeedbackAgeFrames || settings.MaxFeedbackAgeFrames > 64 || !settings.MaxReadBytesPerFrame
			|| !settings.MaxUploadBytesPerFrame) {
			throw std::invalid_argument("Invalid VG feedback age or per-frame I/O budget");
		}
		return settings;
	}())
	, mPool(renderer, mSettings.Pool)
	, mRequests(mSettings.Requests) { }
VirtualGeometryStreaming::~VirtualGeometryStreaming() = default;
uint64_t VirtualGeometryStreaming::gpuBytes() const {
	return mPool.physicalBytes();
}
uint64_t VirtualGeometryStreaming::stagingBytes() const {
	return mRequests.reservedStagingBytes() + mPool.pendingUploadBytes() + mReadyBytes;
}

VirtualGeometryStreamingStatistics VirtualGeometryStreaming::statistics() const {
	auto stats = mStatistics;
	stats.Feedback = mFeedback.statistics();
	return stats;
}

void VirtualGeometryStreaming::beginFrame(RENDER::Renderer& renderer, uint64_t frame) {
	mFrame = frame;
	mReadBytesThisFrame = 0;
	mUploadBytesThisFrame = 0;
	mStatistics.StaleFeedback += mFeedbackBatches.size();
	if (mSettings.GpuFeedback) {
		mFeedbackBatches = mFeedback.poll(renderer, frame);
	}
	auto& completion = renderer.getDriver()->completion();
	mPool.collect();
	const auto failRoots = [&](Context& state) {
		if (state.Failed) {
			return;
		}
		state.Failed = true;
		state.ViewCuts.clear();
		mRootSlots -= state.RootSlots;
		state.RootSlots = 0;
		mRequests.cancelGeneration(state.Generation);
		for (auto& page : state.Pages) {
			if (page.Gpu.Generation) {
				mPool.retire(page.Gpu);
				page = {};
			}
		}
	};
	for (auto& [key, context] : mContexts) {
		auto& state = *context;
		std::erase_if(state.ReadingCuts, [&](const auto& read) {
			const auto token = read.Receipt->token();
			if (!token.Value) {
				return false;
			}
			state.Residency.markSubmitted(read.Cut, token.Value);
			if (!completion.isComplete(token)) {
				return false;
			}
			mCompleted = std::max(mCompleted, token.Value);
			return true;
		});
		for (uint32_t id = 0; id < state.Pages.size(); ++id) {
			auto& page = state.Pages[id];
			if (!page.Gpu.Generation) {
				continue;
			}
			const auto token = mPool.uploadToken(page.Gpu);
			const auto gpuState = mPool.state(page.Gpu);
			if (!page.UploadSubmitted && token.Value
				&& (gpuState == VirtualMeshGpuPagePool::State::Ready || gpuState == VirtualMeshGpuPagePool::State::Failed)) {
				const bool success = gpuState == VirtualMeshGpuPagePool::State::Ready;
				state.Residency.finishUpload(page.Read, success, token.Value);
				page.UploadSubmitted = true;
			}
			if (token.Value && completion.isComplete(token)) {
				mCompleted = std::max(mCompleted, token.Value);
			}
		}
		state.Residency.collect(mCompleted);
		for (uint32_t page = 0; page < state.Pages.size(); ++page) {
			if (state.RootPages[page] && state.Residency.state(page) == VirtualGeometryResidency::State::Error) {
				failRoots(state);
				break;
			}
		}
		for (auto& page : state.Pages) {
			if (page.Gpu.Generation && mPool.state(page.Gpu) == VirtualMeshGpuPagePool::State::Failed && page.UploadSubmitted) {
				mPool.retire(page.Gpu);
				page = {};
			}
		}
		std::erase_if(state.ViewCuts, [&](const auto& entry) {
			const auto seen = entry.second.LastSeen;
			return mFrame > seen && mFrame - seen > 2;
		});
		// Active instance cuts retain their pages. Completed prior-frame draw cuts no longer pin pages.
		for (uint32_t id = 0; id < state.Pages.size(); ++id) {
			auto& page = state.Pages[id];
			if (page.Gpu.Generation && page.LastUsed + 2 < mFrame && state.Residency.retire(id)) {
				++mStatistics.Evictions;
				mPool.retire(page.Gpu);
				page = {};
			}
		}
		state.Residency.collect(mCompleted);
	}

	for (auto it = mContexts.begin(); it != mContexts.end();) {
		auto& state = *it->second;
		if (state.Asset.expired() || state.LastSeen + 2 < mFrame) {
			mRequests.cancelGeneration(state.Generation);
			for (const auto& page : state.Pages) {
				if (page.Gpu.Generation) {
					mPool.retire(page.Gpu);
				}
			}
			mRootSlots -= state.RootSlots;
			it = mContexts.erase(it);
		}
		else {
			++it;
		}
	}
	const auto findContext = [&](uint64_t generation) -> Context* {
		for (auto& [key, candidate] : mContexts) {
			if (candidate->Generation == generation) {
				return candidate.get();
			}
		}
		return nullptr;
	};
	std::erase_if(mFeedbackBatches, [&](const auto& batch) {
		auto* state = findContext(batch.Stamp.AssetGeneration);
		const auto key = std::pair { batch.Stamp.InstanceId, batch.Stamp.ViewId };
		if (!state || state->Failed || !state->ViewCuts.contains(key)) {
			++mStatistics.StaleFeedback;
			return true;
		}
		return false;
	});
	mRootPressure = std::ranges::any_of(mContexts, [](const auto& pair) { return !pair.second->Failed && !pair.second->Residency.rootCut(); });
	while (auto result = mRequests.tryTake()) {
		if (result->Status == VirtualMeshPageReadStatus::Ready) {
			mStatistics.CompletedReadBytes += result->Data.Vertices.size() * sizeof(VMeshVertex) + result->Data.Indices.size() * sizeof(uint32_t);
		}
		else if (result->Status != VirtualMeshPageReadStatus::Cancelled) {
			++mStatistics.ReadFailures;
		}
		auto* state = findContext(result->Ticket.AssetGeneration);
		if (!state) {
			continue;
		}
		const auto status = result->Status == VirtualMeshPageReadStatus::Ready ? VirtualGeometryResidency::ReadResult::Ready
			: result->Status == VirtualMeshPageReadStatus::Cancelled		   ? VirtualGeometryResidency::ReadResult::Cancelled
																			   : VirtualGeometryResidency::ReadResult::Failed;
		if (!state->Residency.finishRead(result->Ticket, status)) {
			continue;
		}
		if (status != VirtualGeometryResidency::ReadResult::Ready) {
			if (status == VirtualGeometryResidency::ReadResult::Failed) {
				std::cerr << "VG_STREAM_PAGE_FAILED generation=" << state->Generation << " page=" << result->Ticket.Page << std::endl;
				if (state->RootPages[result->Ticket.Page] && !state->Failed) {
					failRoots(*state);
				}
			}
			continue;
		}
		auto payload = std::make_shared<const VMeshPageData>(std::move(result->Data));
		const uint64_t bytes = payload->Vertices.capacity() * sizeof(VMeshVertex) + payload->Indices.capacity() * sizeof(uint32_t);
		mReadyBytes += bytes;
		mReady.push_back({ result->Ticket, std::move(payload), bytes });
	}
	// Roots are globally admitted by slot count. Drop optional ready work while
	// establishing a new root cut so it cannot consume the roots' staging budget.
	for (auto it = mReady.begin(); it != mReady.end();) {
		auto* state = findContext(it->Ticket.AssetGeneration);
		if (!state || state->Failed || (mRootPressure && !state->RootPages[it->Ticket.Page])) {
			if (state) {
				state->Residency.cancelUpload(it->Ticket);
			}
			mReadyBytes -= it->Bytes;
			it = mReady.erase(it);
			continue;
		}
		const auto bytes = state->Source->metadata().Pages[it->Ticket.Page].Size;
		if (bytes > mSettings.MaxUploadBytesPerFrame - mUploadBytesThisFrame) {
			++it;
			continue;
		}
		const auto admission = mPool.recordUpload(renderer, it->Data);
		if (admission.Key.Generation) {
			mUploadBytesThisFrame += bytes;
			mStatistics.UploadBytes += bytes;
			mStatistics.UploadBytesHighWater = std::max(mStatistics.UploadBytesHighWater, mUploadBytesThisFrame);
		}
		if (admission.Status == VirtualMeshGpuPagePool::UploadStatus::NoSlot) {
			++it;
			continue;
		}
		if (admission.Status == VirtualMeshGpuPagePool::UploadStatus::Recorded) {
			auto& page = state->Pages[it->Ticket.Page];
			page.Read = it->Ticket;
			page.Gpu = admission.Key;
			page.UploadSubmitted = false;
			page.LastUsed = mFrame;
		}
		else {
			if (admission.Key.Generation) {
				mPool.retire(admission.Key);
			}
			state->Residency.cancelUpload(it->Ticket);
			failRoots(*state);
		}
		mReadyBytes -= it->Bytes;
		it = mReady.erase(it);
	}

	mPool.collect();
	mStatistics.StagingBytesHighWater = std::max(mStatistics.StagingBytesHighWater, stagingBytes());
}

std::shared_ptr<const VirtualGeometryStreamCut> VirtualGeometryStreaming::select(RENDER::Renderer& renderer,
	const std::shared_ptr<VirtualMeshAsset>& asset, const VirtualGeometryInstanceTransform& transform, const VirtualGeometryView& view,
	float threshold, uint64_t instanceId, uint64_t viewId, PROFILER::GpuStageProfiler* profiler) {
	if (!asset || !asset->streamsPages() || !asset->pageSource()) {
		return {};
	}
	const auto fitsPage = [&](uint64_t bytes) {
		return bytes <= mSettings.Pool.MaxPageBytes && bytes <= mSettings.Requests.MaxStagingBytes / 2 && bytes <= mSettings.MaxReadBytesPerFrame
			&& bytes <= mSettings.MaxUploadBytesPerFrame;
	};
	auto& stored = mContexts[asset.get()];
	if (!stored) {
		std::vector<bool> roots(asset->pageSource()->metadata().Pages.size(), false);
		uint32_t slots = 0;
		for (const auto root : asset->pageSource()->metadata().Roots) {
			const auto page = asset->pageSource()->metadata().ClusterPages[root];
			const auto bytes = asset->pageSource()->metadata().Pages[page].Size;
			if (!fitsPage(bytes)) {
				mContexts.erase(asset.get());
				return {};
			}
			if (!roots[page]) {
				roots[page] = true;
				++slots;
			}
		}
		if (slots > mSettings.Pool.SlotCount - mRootSlots) {
			mContexts.erase(asset.get());
			return {};
		}
		mRootPressure = true;
		// Drop only persistent optional pins. Recorded readers still own their leases.
		for (auto& [key, context] : mContexts) {
			if (context) {
				context->ViewCuts.clear();
			}
		}
		if (mNextGeneration == std::numeric_limits<uint64_t>::max()) {
			throw std::overflow_error("VG asset generation exhausted");
		}
		stored = std::make_unique<Context>(
			asset, mNextGeneration++, uint64_t(mSettings.Pool.SlotCount) * mSettings.Pool.MaxPageBytes, mSettings.Requests.MaxRequests);
		mRootSlots += stored->RootSlots;
	}
	auto& state = *stored;
	if (state.Asset.lock() != asset) {
		return {};
	}
	state.LastSeen = mFrame;
	if (state.Failed) {
		return {};
	}
	const auto& metadata = state.Source->metadata();
	for (const auto root : metadata.Roots) {
		const auto page = metadata.ClusterPages[root];
		if (metadata.Pages[page].Size > mSettings.Pool.MaxPageBytes) {
			return {};
		}
		if (state.Residency.state(page) == VirtualGeometryResidency::State::Missing) {
			state.Residency.request(page);
		}
	}
	VirtualGeometryResidency::CutSnapshot cut;
	Context::ViewCut* activeView = nullptr;
	if (!mRootPressure) {
		activeView = &state.ViewCuts[std::pair { instanceId, viewId }];
		activeView->LastSeen = mFrame;
		if (activeView->Cut) {
			const auto selected = activeView->Cut->clusters();
			const auto members = std::span(metadata.TransitionClusters);
			const bool needsCoarsening = std::ranges::any_of(metadata.Transitions, [&](const VMeshTransition& transition) {
				const auto coarse = members.subspan(transition.CoarseFirst, transition.CoarseCount);
				const auto fine = members.subspan(transition.FineFirst, transition.FineCount);
				// Only an entire fine side can be replaced in a legal DAG cut.
				return std::ranges::all_of(coarse, [&](uint32_t id) {
					const auto& cluster = metadata.Clusters[id];
					return ProjectedErrorPixels(cluster.Error, cluster.LodBounds, transform, view) <= threshold;
				}) && std::ranges::all_of(fine, [&](uint32_t id) { return std::ranges::binary_search(selected, id); });
			});
			if (needsCoarsening) {
				activeView->Cut.reset();
			}
			else {
				cut = activeView->Cut;
			}
		}
	}
	if (!cut) {
		cut = state.Residency.rootCut();
	}
	std::unordered_set<uint32_t> gpuPages;
	if (cut && activeView) {
		using Feedback = VirtualGeometryPageFeedback;
		std::vector<Feedback::Candidate> frontier;
		size_t coarseInputs = 0;
		bool overflow = false;
		bool cpuFallback = !mSettings.GpuFeedback;
		std::vector<Feedback::Score> scores;
		if (mSettings.GpuFeedback && activeView->PendingFeedback) {
			const auto submitted = *activeView->PendingFeedback;
			const auto batch = std::ranges::find_if(mFeedbackBatches, [&](const auto& value) {
				const auto& stamp = value.Stamp;
				return stamp.AssetGeneration == state.Generation && stamp.InstanceId == instanceId && stamp.ViewId == viewId
					&& stamp.Frame == submitted;
			});
			const bool aged = submitted > mFrame || mFrame - submitted > mSettings.MaxFeedbackAgeFrames;
			if (batch != mFeedbackBatches.end()) {
				if (aged || batch->Dropped) {
					mStatistics.StaleFeedback += aged ? 1 : 0;
					mStatistics.DroppedFeedback += batch->Dropped ? 1 : 0;
					cpuFallback = true;
				}
				else {
					scores = std::move(batch->Scores);
				}
				activeView->PendingFeedback.reset();
				mFeedbackBatches.erase(batch);
			}
			else if (aged) {
				++mStatistics.StaleFeedback;
				cpuFallback = true;
				activeView->PendingFeedback.reset();
			}
		}

		const bool waitingForFeedback = !cpuFallback && activeView->PendingFeedback.has_value();
		const auto members = std::span(metadata.TransitionClusters);
		const auto fineSide = [&](uint32_t id) {
			const auto& transition = metadata.Transitions[id];
			return members.subspan(transition.FineFirst, transition.FineCount);
		};
		const auto coarseSide = [&](uint32_t id) {
			const auto& transition = metadata.Transitions[id];
			return members.subspan(transition.CoarseFirst, transition.CoarseCount);
		};
		const auto coarseActive = [&](std::span<const uint32_t> coarse) {
			return std::ranges::all_of(coarse, [&](uint32_t cluster) { return std::ranges::binary_search(cut->clusters(), cluster); });
		};
		const auto priority = [&](std::span<const uint32_t> coarse) {
			float result = 0.0f;
			for (const auto clusterId : coarse) {
				const auto& cluster = metadata.Clusters[clusterId];
				const auto excess = ProjectedErrorPixels(cluster.Error, cluster.LodBounds, transform, view) - threshold;
				if (excess > result) {
					result = std::min(excess, std::numeric_limits<float>::max());
				}
			}
			return result;
		};
		const auto scoreOrder = [](const Feedback::Score& a, const Feedback::Score& b) {
			return a.Priority != b.Priority ? a.Priority > b.Priority : a.Transition < b.Transition;
		};
		std::vector<Feedback::Score> cpuScores;
		const auto requestFine = [&](uint32_t id, bool gpu) {
			for (const auto cluster : fineSide(id)) {
				const auto page = metadata.ClusterPages[cluster];
				const auto status = state.Residency.state(page);
				if (status == VirtualGeometryResidency::State::Request) {
					++mStatistics.DeduplicatedRequests;
				}
				if (status == VirtualGeometryResidency::State::Missing) {
					++mStatistics.PageMisses;
					if (state.Residency.request(page) && gpu) {
						gpuPages.insert(page);
					}
				}
			}
		};
		struct FineState {
			bool Feasible = true;
			bool Missing = false;
			bool Resident = true;
		};
		const auto fineState = [&](uint32_t id) {
			// The current cut and fine replacement must coexist during publication.
			std::vector<uint32_t> needed;
			for (const auto page : cut->pages()) {
				if (!state.RootPages[page]) {
					needed.push_back(page);
				}
			}
			FineState result;
			for (const auto cluster : fineSide(id)) {
				const auto page = metadata.ClusterPages[cluster];
				const auto status = state.Residency.state(page);
				result.Feasible = result.Feasible && fitsPage(metadata.Pages[page].Size) && status != VirtualGeometryResidency::State::Error;
				result.Missing = result.Missing || status == VirtualGeometryResidency::State::Missing;
				result.Resident = result.Resident && status == VirtualGeometryResidency::State::Resident;
				if (!state.RootPages[page]) {
					needed.push_back(page);
				}
			}
			std::ranges::sort(needed);
			needed.erase(std::unique(needed.begin(), needed.end()), needed.end());
			result.Feasible = result.Feasible && needed.size() <= mSettings.Pool.SlotCount - mRootSlots;
			return result;
		};
		const auto visitFrontier = [&](bool onGpu) {
			for (size_t index = metadata.Transitions.size(); index > 0; --index) {
				const auto id = static_cast<uint32_t>(index - 1);
				const auto coarse = coarseSide(id);
				if (!coarseActive(coarse)) {
					continue;
				}
				const float cpuPriority = onGpu ? 0.0f : priority(coarse);
				if (!onGpu && !(cpuPriority > 0.0f)) {
					continue;
				}
				// A healthy pending batch owns optional misses. Resident groups can still refine immediately.
				if (waitingForFeedback && std::ranges::any_of(fineSide(id), [&](uint32_t cluster) {
						return state.Residency.state(metadata.ClusterPages[cluster]) != VirtualGeometryResidency::State::Resident;
					})) {
					continue;
				}
				const auto [feasible, missing, resident] = fineState(id);
				if (!feasible) {
					continue;
				}
				if (resident && (!onGpu || priority(coarse) > 0.0f)) {
					cut = state.Residency.changeCut(cut, std::span(&id, 1), VirtualGeometryResidency::Direction::Refine);
				}
				else if (missing && !onGpu) {
					cpuScores.push_back({ id, cpuPriority });
				}
				else if (missing) {
					if (frontier.size() == Feedback::MaxCandidates || coarse.size() > Feedback::MaxCoarseInputs - coarseInputs) {
						overflow = true;
						continue;
					}
					frontier.push_back({ id, coarse });
					coarseInputs += coarse.size();
				}
			}
		};
		visitFrontier(!cpuFallback);
		if (!cpuFallback && !waitingForFeedback) {
			// Candidate enumeration is descending by transition ID. Apply positive scores in priority order.
			std::ranges::sort(scores, scoreOrder);
			for (const auto& score : scores) {
				if (!(score.Priority > 0.0f)) {
					continue;
				}
				const auto candidate
					= std::ranges::lower_bound(frontier, score.Transition, std::ranges::greater {}, &Feedback::Candidate::Transition);
				if (candidate != frontier.end() && candidate->Transition == score.Transition && coarseActive(candidate->CoarseClusters)
					&& priority(candidate->CoarseClusters) > 0.0f && fineState(score.Transition).Feasible) {
					requestFine(score.Transition, true);
				}
			}
			std::erase_if(frontier, [&](const auto& candidate) {
				return std::ranges::none_of(fineSide(candidate.Transition), [&](uint32_t cluster) {
					return state.Residency.state(metadata.ClusterPages[cluster]) == VirtualGeometryResidency::State::Missing;
				});
			});
			if (overflow) {
				++mStatistics.DroppedFeedback;
				cpuFallback = true;
			}
			else if (!frontier.empty() && !activeView->PendingFeedback) {
				const Feedback::Identity stamp { mFrame, state.Generation, viewId, instanceId };
				const auto submitted = mFeedback.submit(renderer, stamp, transform, view, threshold, metadata.Clusters, frontier, profiler);
				if (submitted == Feedback::SubmitStatus::Accepted) {
					activeView->PendingFeedback = mFrame;
				}
				else {
					cpuFallback = true;
				}
			}
			if (cpuFallback) {
				visitFrontier(false);
			}
		}
		std::ranges::sort(cpuScores, scoreOrder);
		for (const auto& score : cpuScores) {
			if (coarseActive(coarseSide(score.Transition)) && fineState(score.Transition).Feasible) {
				requestFine(score.Transition, false);
			}
		}
		if (cpuFallback && mSettings.GpuFeedback) {
			++mStatistics.CpuFeedbackFallbacks;
		}
	}
	for (const auto ticket : state.Residency.takeRequests()) {
		const auto bytes = metadata.Pages[ticket.Page].Size;
		const auto used = stagingBytes();
		if (mReady.size() + mRequests.pendingCount() >= mSettings.Requests.MaxRequests || used > mSettings.Requests.MaxStagingBytes
			|| bytes > (mSettings.Requests.MaxStagingBytes - used) / 2 || bytes > mSettings.MaxReadBytesPerFrame - mReadBytesThisFrame
			|| mRequests.trySubmit(ticket, state.Source) != VirtualMeshPageAdmissionStatus::Accepted) {
			state.Residency.finishRead(ticket, VirtualGeometryResidency::ReadResult::Cancelled);
			continue;
		}
		++mStatistics.ReadRequests;
		mStatistics.GpuRequests += gpuPages.contains(ticket.Page) ? 1 : 0;
		mStatistics.ReadBytes += bytes;
		mReadBytesThisFrame += bytes;
		mStatistics.ReadBytesHighWater = std::max(mStatistics.ReadBytesHighWater, mReadBytesThisFrame);
		mStatistics.StagingBytesHighWater = std::max(mStatistics.StagingBytesHighWater, stagingBytes());
	}
	if (!cut) {
		return {};
	}
	auto selection = std::make_shared<VirtualGeometryStreamCut>();
	selection->Cut = cut;
	for (const auto id : cut->pages()) {
		auto lease = mPool.acquire(state.Pages[id].Gpu);
		if (!lease || !mPool.markRead(renderer, lease)) {
			return {};
		}
		selection->Pages.emplace(id, std::move(lease));
		state.Pages[id].LastUsed = mFrame;
	}
	state.ReadingCuts.push_back({ cut, renderer.submissionReceipt() });
	if (activeView) {
		activeView->Cut = cut;
	}
	return selection;
}
}
