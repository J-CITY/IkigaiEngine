#include "virtualMeshPageRequests.h"

#include "resourceModule/fileSystem/fileSystem.h"
#include "taskModule/task.h"
#include "virtualMeshPageSource.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <stop_token>
#include <utility>

namespace IKIGAI::VG {
namespace {
	constexpr uint32_t MaxWorkerCount = 8;
	constexpr uint32_t MaxRequestCount = 4096;
}

struct VirtualMeshPageRequests::Request {
	Ticket Key {};
	uint64_t Reservation = 0;
	std::stop_source Stop {};
	std::atomic<bool> Done = false;
	VirtualMeshPageCompletion Completion {};

	void read(const VirtualMeshPageSource& source) {
		Completion.Ticket = Key;
		const auto stop = Stop.get_token();
		try {
			if (stop.stop_requested()) {
				Completion.Status = VirtualMeshPageReadStatus::Cancelled;
			}
			else {
				Completion.Data = source.readPage(Key.Page, stop);
				Completion.Status = VirtualMeshPageReadStatus::Ready;
			}
		} catch (const RESOURCES::RangeFileError& error) {
			Completion.Status = error.code() == RESOURCES::RangeFileErrorCode::Cancelled ? VirtualMeshPageReadStatus::Cancelled
																						 : VirtualMeshPageReadStatus::IoError;
			Completion.Failure = std::current_exception();
		} catch (const VMeshFormatError&) {
			Completion.Status = VirtualMeshPageReadStatus::InvalidData;
			Completion.Failure = std::current_exception();
		} catch (...) {
			Completion.Status = VirtualMeshPageReadStatus::Failed;
			Completion.Failure = std::current_exception();
		}
		Done.store(true, std::memory_order_release);
	}
};

VirtualMeshPageRequests::VirtualMeshPageRequests(VirtualMeshPageRequestSettings settings)
	: mSettings(settings) {
	if (!settings.WorkerCount || settings.WorkerCount > MaxWorkerCount || !settings.MaxRequests || settings.MaxRequests > MaxRequestCount
		|| !settings.MaxStagingBytes) {
		throw std::invalid_argument("Invalid VG page request worker count, request count, or staging budget");
	}
	mPending.reserve(settings.MaxRequests);
	mExecutor = std::make_unique<TASK::ThreadPoolExecutor>(static_cast<int>(settings.WorkerCount));
}

VirtualMeshPageRequests::~VirtualMeshPageRequests() {
	shutdown();
}

VirtualMeshPageAdmissionStatus VirtualMeshPageRequests::trySubmit(Ticket ticket, std::shared_ptr<const VirtualMeshPageSource> source) {
	if (!mExecutor) {
		return VirtualMeshPageAdmissionStatus::Closed;
	}
	if (!source || !ticket.AssetGeneration || !ticket.Attempt || ticket.Page >= source->metadata().Pages.size()) {
		return VirtualMeshPageAdmissionStatus::InvalidRequest;
	}
	if (std::ranges::any_of(mPending, [&](const auto& request) { return request->Key == ticket; })) {
		return VirtualMeshPageAdmissionStatus::AlreadyPending;
	}
	if (mPending.size() >= mSettings.MaxRequests) {
		return VirtualMeshPageAdmissionStatus::RequestLimit;
	}
	const uint64_t pageBytes = source->metadata().Pages[ticket.Page].Size;
	// A read briefly holds the encoded range and decoded vertex/index arrays.
	const uint64_t remaining = mSettings.MaxStagingBytes - mReservedStagingBytes;
	if (pageBytes > remaining / 2u) {
		return VirtualMeshPageAdmissionStatus::StagingLimit;
	}
	const uint64_t reservation = pageBytes * 2u;
	try {
		auto request = std::make_shared<Request>();
		request->Key = ticket;
		request->Reservation = reservation;
		auto work = [request, source = std::move(source)] { request->read(*source); };
		auto task = std::make_unique<TASK::Task<decltype(work)>>(std::move(work), "VG page read", nullptr);
		mPending.push_back(std::move(request));
		mReservedStagingBytes += reservation;
		try {
			if (mExecutor->addTask(std::move(task), -1)) {
				return VirtualMeshPageAdmissionStatus::Accepted;
			}
		} catch (...) {
			// Scheduling failed before the task could be queued.
		}
		mPending.pop_back();
		mReservedStagingBytes -= reservation;
	} catch (...) {
		return VirtualMeshPageAdmissionStatus::DispatchFailed;
	}
	return VirtualMeshPageAdmissionStatus::DispatchFailed;
}

bool VirtualMeshPageRequests::cancel(Ticket ticket) {
	const auto found = std::ranges::find_if(mPending, [&](const auto& request) { return request->Key == ticket; });
	return found != mPending.end() && (*found)->Stop.request_stop();
}

size_t VirtualMeshPageRequests::cancelGeneration(uint64_t generation) {
	size_t count = 0;
	for (const auto& request : mPending) {
		if (request->Key.AssetGeneration == generation && request->Stop.request_stop()) {
			++count;
		}
	}
	return count;
}

std::optional<VirtualMeshPageCompletion> VirtualMeshPageRequests::tryTake() {
	const auto found = std::ranges::find_if(mPending, [](const auto& request) { return request->Done.load(std::memory_order_acquire); });
	if (found == mPending.end()) {
		return std::nullopt;
	}
	auto request = std::move(*found);
	mPending.erase(found);
	if (request->Stop.stop_requested()) {
		request->Completion = { request->Key, VirtualMeshPageReadStatus::Cancelled, {}, {} };
	}
	mReservedStagingBytes -= request->Reservation;
	return std::move(request->Completion);
}

size_t VirtualMeshPageRequests::pendingCount() const {
	return mPending.size();
}

uint64_t VirtualMeshPageRequests::reservedStagingBytes() const {
	return mReservedStagingBytes;
}

void VirtualMeshPageRequests::shutdown() {
	for (const auto& request : mPending) {
		request->Stop.request_stop();
	}
	mExecutor.reset();
	mPending.clear();
	mReservedStagingBytes = 0;
}
}
