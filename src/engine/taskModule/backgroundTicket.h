#pragma once

#include <exception>
#include <future>

namespace IKIGAI::TASK {

enum class BackgroundOutcome { Rejected, Succeeded, Failed, Cancelled };
enum class BackgroundRejection { None, Unavailable, Stopping, InvalidWork, SchedulingFailed, QueueFull };

constexpr const char* backgroundRejectionMessage(BackgroundRejection rejection) {
	switch (rejection) {
	case BackgroundRejection::None:
		return "no rejection";
	case BackgroundRejection::Unavailable:
		return "background executor is unavailable";
	case BackgroundRejection::Stopping:
		return "background executor is stopping";
	case BackgroundRejection::InvalidWork:
		return "background work is empty";
	case BackgroundRejection::SchedulingFailed:
		return "background work could not be scheduled";
	case BackgroundRejection::QueueFull:
		return "I/O executor is at capacity";
	}
	return "unknown background rejection";
}

struct BackgroundResult {
	BackgroundOutcome Outcome = BackgroundOutcome::Rejected;
	BackgroundRejection Rejection = BackgroundRejection::Unavailable;
	std::exception_ptr Exception = {};
};

// Copies share a result. Dropping a ticket neither waits nor cancels the work.
class BackgroundTicket {
public:
	[[nodiscard]] bool accepted() const;
	[[nodiscard]] bool ready() const;
	// Throws std::logic_error if an executor callback tries to block on pending background work.
	[[nodiscard]] BackgroundResult get() const;

private:
	friend class TaskSystem;

	explicit BackgroundTicket(BackgroundResult rejection);
	explicit BackgroundTicket(std::shared_future<BackgroundResult> completion);

	BackgroundResult mRejection = {};
	std::shared_future<BackgroundResult> mCompletion = {};
};

}
