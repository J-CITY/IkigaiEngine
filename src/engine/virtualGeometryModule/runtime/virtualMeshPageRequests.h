#pragma once

#include "virtualGeometryModule/format/vmeshIO.h"
#include "virtualGeometryResidency.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>
#include <vector>

namespace IKIGAI::TASK {
class ThreadPoolExecutor;
}

namespace IKIGAI::VG {
class VirtualMeshPageSource;

struct VirtualMeshPageRequestSettings {
	static constexpr uint32_t DefaultWorkerCount = 2;
	static constexpr uint32_t DefaultMaxRequests = 64;
	static constexpr uint64_t DefaultMaxStagingBytes = 16u * 1024u * 1024u;

	uint32_t WorkerCount = DefaultWorkerCount;
	uint32_t MaxRequests = DefaultMaxRequests;
	uint64_t MaxStagingBytes = DefaultMaxStagingBytes;

	template <class C> static void serde(C& context, VirtualMeshPageRequestSettings& value) {
		using Self = VirtualMeshPageRequestSettings;
		using serde::attribute::default_;
		serde::serde_struct(context, value)
			.field(&Self::WorkerCount, "WorkerCount", default_ { DefaultWorkerCount })
			.field(&Self::MaxRequests, "MaxRequests", default_ { DefaultMaxRequests })
			.field(&Self::MaxStagingBytes, "MaxStagingBytes", default_ { DefaultMaxStagingBytes });
	}
};

enum class VirtualMeshPageAdmissionStatus { Accepted, AlreadyPending, RequestLimit, StagingLimit, InvalidRequest, Closed, DispatchFailed };
enum class VirtualMeshPageReadStatus { Ready, Cancelled, IoError, InvalidData, Failed };

struct VirtualMeshPageCompletion {
	VirtualGeometryResidency::Ticket Ticket {};
	VirtualMeshPageReadStatus Status = VirtualMeshPageReadStatus::Failed;
	VMeshPageData Data {};
	std::exception_ptr Failure {};
};

// One owner thread submits, cancels and polls. A ticket is scoped to its residency
// controller's unique asset generation. Workers only read immutable page sources.
class VirtualMeshPageRequests final {
public:
	using Ticket = VirtualGeometryResidency::Ticket;

	explicit VirtualMeshPageRequests(VirtualMeshPageRequestSettings settings = {});
	~VirtualMeshPageRequests();
	VirtualMeshPageRequests(const VirtualMeshPageRequests&) = delete;
	VirtualMeshPageRequests& operator=(const VirtualMeshPageRequests&) = delete;

	[[nodiscard]] VirtualMeshPageAdmissionStatus trySubmit(Ticket ticket, std::shared_ptr<const VirtualMeshPageSource> source);
	bool cancel(Ticket ticket);
	size_t cancelGeneration(uint64_t generation);

	// Takes an already-completed result without waiting. The queue releases its
	// reservation here. The caller accounts for decoded data retained after take.
	[[nodiscard]] std::optional<VirtualMeshPageCompletion> tryTake();
	[[nodiscard]] size_t pendingCount() const;
	[[nodiscard]] uint64_t reservedStagingBytes() const;

	// Stops admission, cancels queued/active reads, joins workers and drops results.
	// Can wait for a native read already in progress.
	void shutdown();

private:
	struct Request;

	VirtualMeshPageRequestSettings mSettings {};
	std::unique_ptr<TASK::ThreadPoolExecutor> mExecutor {};
	std::vector<std::shared_ptr<Request>> mPending {};
	uint64_t mReservedStagingBytes = 0;
};
}
