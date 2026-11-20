#include "virtualGeometryPageFeedback.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "profilerModule/gpuStageProfiler.h"
#include "renderModule/render.h"
#include "resourceModule/shaderManager.h"

namespace IKIGAI::VG {
namespace {
	constexpr uint32_t WorkGroupSize = 64;
	constexpr uint64_t RetiredTicketAge = 2 * VirtualGeometryPageFeedback::RingSize;

	struct alignas(16) GpuCoarse {
		std::array<float, 4> Bounds {};
		float Error = 0.0f;
		uint32_t Candidate = 0;
		uint32_t Reserved0 = 0;
		uint32_t Reserved1 = 0;
	};
	static_assert(sizeof(GpuCoarse) == 32);

	struct alignas(16) GpuView {
		std::array<std::array<float, 4>, 3> WorldRows {};
		std::array<float, 4> CameraScale {};
		std::array<float, 4> Projection {};
		std::array<uint32_t, 4> Counts {};
	};
	static_assert(sizeof(GpuView) == 96);

	constexpr size_t SlotBytes
		= VirtualGeometryPageFeedback::MaxCoarseInputs * sizeof(GpuCoarse) + VirtualGeometryPageFeedback::MaxReadbackBytes + sizeof(GpuView);

	enum class ReadbackState { Queued, Recording, Recorded, Failed, Discarded };
	struct FeedbackJob {
		std::shared_ptr<const RENDER::FrameSubmissionReceipt> Receipt;
		std::atomic<ReadbackState> State = ReadbackState::Queued;
		uint64_t ReadbackTicket = 0;
		VirtualGeometryPageFeedback::Batch Result;
	};

	GpuView MakeView(const VirtualGeometryInstanceTransform& transform, const VirtualGeometryView& view, float threshold, uint32_t inputCount) {
		GpuView gpu;
		for (size_t row = 0; row < gpu.WorldRows.size(); ++row) {
			std::copy_n(transform.World.getData() + row * 4, 4, gpu.WorldRows[row].begin());
		}
		gpu.CameraScale = { view.CameraPosition.x, view.CameraPosition.y, view.CameraPosition.z, transform.MaxScale };
		gpu.Projection = { view.ProjectionScale, view.Orthographic ? 0.0f : view.ZNear, threshold, view.Orthographic ? 1.0f : 0.0f };
		gpu.Counts[0] = inputCount;
		return gpu;
	}
}

struct VirtualGeometryPageFeedback::Slot {
	std::shared_ptr<RENDER::StorageBufferInterface> Inputs;
	std::shared_ptr<RENDER::StorageBufferInterface> Priorities;
	std::shared_ptr<RENDER::UniformBufferInterface> View;
	std::shared_ptr<FeedbackJob> Pending;
};

VirtualGeometryPageFeedback::VirtualGeometryPageFeedback() = default;
VirtualGeometryPageFeedback::~VirtualGeometryPageFeedback() = default;

bool VirtualGeometryPageFeedback::ensureShader(RENDER::Renderer& renderer) {
	const auto features = renderer.gpuFeatures();
	if (!features.Compute || !features.BufferUsageFlags || !features.BufferTransfer || !features.BufferReadback) {
		return false;
	}
	if (!mShaderAttempted) {
		mShaderAttempted = true;
		try {
			mShader = RESOURCES::ShaderLoader::Create("/shaders/virtualGeometry/vgPageFeedback.shader");
		} catch (const std::exception& error) {
			std::cerr << "VG_PAGE_FEEDBACK_FALLBACK " << error.what() << '\n';
		}
	}
	return bool(mShader);
}

VirtualGeometryPageFeedback::SubmitStatus VirtualGeometryPageFeedback::submit(RENDER::Renderer& renderer, Identity stamp,
	const VirtualGeometryInstanceTransform& transform, const VirtualGeometryView& view, float threshold, std::span<const VMeshCluster> clusters,
	std::span<const Candidate> candidates, PROFILER::GpuStageProfiler* profiler) {
	if (candidates.empty() || candidates.size() > MaxCandidates) {
		++mStatistics.Overflow;
		return SubmitStatus::Overflow;
	}
	size_t inputCount = 0;
	for (const auto& candidate : candidates) {
		if (candidate.CoarseClusters.empty() || candidate.CoarseClusters.size() > MaxCoarseInputs - inputCount) {
			++mStatistics.Overflow;
			return SubmitStatus::Overflow;
		}
		inputCount += candidate.CoarseClusters.size();
	}
	const auto gpuView = MakeView(transform, view, threshold, static_cast<uint32_t>(inputCount));
	const auto finite = [](const auto& values) { return std::ranges::all_of(values, [](float value) { return std::isfinite(value); }); };
	if (!stamp.AssetGeneration || !finite(gpuView.CameraScale) || !finite(gpuView.Projection) || !std::ranges::all_of(gpuView.WorldRows, finite)
		|| transform.MaxScale < 0.0f || view.ProjectionScale < 0.0f || (!view.Orthographic && view.ZNear <= 0.0f) || threshold < 0.0f) {
		return SubmitStatus::Unsupported;
	}
	if (!ensureShader(renderer)) {
		return SubmitStatus::Unsupported;
	}
	const auto groups = static_cast<uint32_t>((inputCount + WorkGroupSize - 1) / WorkGroupSize);
	if (groups > renderer.gpuFeatures().MaxComputeWorkGroupCountX) {
		++mStatistics.Overflow;
		return SubmitStatus::Overflow;
	}
	const auto available = std::ranges::find_if(mSlots, [](const auto& slot) { return !slot || !slot->Pending; });
	if (available == mSlots.end()) {
		++mStatistics.Busy;
		return SubmitStatus::Busy;
	}
	auto job = std::make_shared<FeedbackJob>();
	job->Receipt = renderer.submissionReceipt();
	job->Result.Stamp = stamp;
	job->Result.Scores.reserve(candidates.size());
	std::vector<GpuCoarse> inputs;
	inputs.reserve(inputCount);
	for (const auto& candidate : candidates) {
		const auto candidateIndex = static_cast<uint32_t>(job->Result.Scores.size());
		job->Result.Scores.push_back({ candidate.Transition, 0.0f });
		for (const auto id : candidate.CoarseClusters) {
			if (id >= clusters.size()) {
				throw std::invalid_argument("VG page feedback cluster ID is out of range");
			}
			const auto& cluster = clusters[id];
			const auto& sphere = cluster.LodBounds;
			inputs.push_back({ { sphere.Center[0], sphere.Center[1], sphere.Center[2], sphere.Radius }, cluster.Error, candidateIndex });
		}
	}

	if (!*available) {
		auto slot = std::make_unique<Slot>();
		slot->Inputs = renderer.createStorageBuffer(
			nullptr, MaxCoarseInputs, sizeof(GpuCoarse), RENDER::BufferUsage::STORAGE | RENDER::BufferUsage::TRANSFER_DST);
		slot->Priorities = renderer.createStorageBuffer(nullptr, MaxCandidates, sizeof(uint32_t),
			RENDER::BufferUsage::STORAGE | RENDER::BufferUsage::TRANSFER_SRC | RENDER::BufferUsage::TRANSFER_DST);
		slot->View = renderer.createUniformBuffer(gpuView);
		*available = std::move(slot);
		mStatistics.GpuBytes += SlotBytes;
	}
	auto& slot = **available;
	const size_t readbackBytes = candidates.size() * sizeof(uint32_t);
	auto profile = profiler ? profiler->measure("VgPageFeedback") : PROFILER::GpuStageProfiler::Scope {};
	renderer.updateStorageBufferRange(slot.Inputs, inputs.data(), inputs.size() * sizeof(GpuCoarse), 0);
	renderer.updateUniformBuffer(slot.View, gpuView);
	renderer.fillBuffer(slot.Priorities, 0, readbackBytes, 0);
	renderer.setShader(mShader);
	renderer.setUniformBuffer("engine_VgFeedbackView", slot.View);
	renderer.setStorageBuffer("engine_VgFeedbackCoarse", slot.Inputs);
	renderer.setStorageBuffer("engine_VgFeedbackPriorities", slot.Priorities);
	renderer.dispatch(groups, 1, 1);
	renderer.execute([&renderer, buffer = slot.Priorities, readbackBytes, job] {
		auto expected = ReadbackState::Queued;
		if (!job->State.compare_exchange_strong(expected, ReadbackState::Recording, std::memory_order_acq_rel)) {
			return;
		}
		try {
			job->ReadbackTicket = renderer.getDriver()->requestBufferReadback(buffer, 0, readbackBytes);
		} catch (const std::exception& error) {
			std::cerr << "VG_PAGE_FEEDBACK_READBACK " << error.what() << '\n';
		}
		job->State.store(job->ReadbackTicket ? ReadbackState::Recorded : ReadbackState::Failed, std::memory_order_release);
	});
	slot.Pending = std::move(job);
	++mStatistics.Submitted;
	++mStatistics.Outstanding;
	mStatistics.ReadbackBytes += readbackBytes;
	return SubmitStatus::Accepted;
}

std::vector<VirtualGeometryPageFeedback::Batch> VirtualGeometryPageFeedback::poll(RENDER::Renderer& renderer, uint64_t currentFrame) {
	std::vector<Batch> results;
	if (!mStatistics.Outstanding) {
		return results;
	}
	results.reserve(mStatistics.Outstanding);
	auto& driver = *renderer.getDriver();
	for (auto& slot : mSlots) {
		if (!slot || !slot->Pending) {
			continue;
		}
		auto& job = *slot->Pending;
		const auto completion = job.Receipt->token();
		bool discardSlot = false;
		if (!completion.Value) {
			auto expected = ReadbackState::Queued;
			// A later renderer frame means this frame is no longer open. Only an
			// unexecuted copy can be abandoned, and we allocate fresh slot buffers.
			discardSlot = currentFrame > job.Result.Stamp.Frame
				&& job.State.compare_exchange_strong(expected, ReadbackState::Discarded, std::memory_order_acq_rel);
			if (!discardSlot) {
				continue;
			}
		}
		else if (!driver.completion().isComplete(completion)) {
			continue;
		}
		const uint64_t ticket = job.State.load(std::memory_order_acquire) == ReadbackState::Recorded ? job.ReadbackTicket : 0;
		std::vector<uint8_t> bytes;
		const size_t expectedBytes = job.Result.Scores.size() * sizeof(uint32_t);
		bool collected = false;
		if (ticket) {
			try {
				collected = driver.collectBufferReadback(ticket, bytes);
				// VK staging can still await slot retirement. The same false return also
				// denotes a ticket the backend evicted, so bound retries after completion.
				if (!collected && (currentFrame < job.Result.Stamp.Frame || currentFrame - job.Result.Stamp.Frame < RetiredTicketAge)) {
					continue;
				}
			} catch (const std::exception& error) {
				std::cerr << "VG_PAGE_FEEDBACK_COLLECT " << error.what() << '\n';
			}
		}
		job.Result.Dropped = !collected || bytes.size() != expectedBytes;
		if (!job.Result.Dropped) {
			for (size_t index = 0; index < job.Result.Scores.size(); ++index) {
				uint32_t bits = 0;
				std::memcpy(&bits, bytes.data() + index * sizeof(bits), sizeof(bits));
				const float score = std::bit_cast<float>(bits);
				if (!std::isfinite(score) || score < 0.0f) {
					job.Result.Dropped = true;
					break;
				}
				job.Result.Scores[index].Priority = score;
			}
		}
		if (job.Result.Dropped) {
			++mStatistics.Dropped;
		}
		else {
			++mStatistics.Completed;
		}
		results.push_back(std::move(job.Result));
		--mStatistics.Outstanding;
		mStatistics.ReadbackBytes -= expectedBytes;
		if (discardSlot) {
			mStatistics.GpuBytes -= SlotBytes;
			slot.reset();
		}
		else {
			slot->Pending.reset();
		}
	}
	return results;
}

const VirtualGeometryPageFeedback::Statistics& VirtualGeometryPageFeedback::statistics() const {
	return mStatistics;
}
}
