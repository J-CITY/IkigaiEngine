#include "renderThread.h"
#include "coroutineModule/awaitables.h"
#include <chrono>
#include <stdexcept>

namespace IKIGAI::RENDER {
COROUTINE::Task<void> AwaitRender(RenderTicket ticket) {
	while (ticket.valid() && ticket.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
		co_await COROUTINE::NextFrame();
	}
	if (ticket.valid()) {
		ticket.get();
	}
}

RenderThread::RenderThread(size_t capacity)
	: mCapacity(capacity ? capacity : throw std::invalid_argument("Render thread capacity must be positive"))
	, mThread([this] { run(); }) { }

RenderThread::~RenderThread() {
	{
		std::lock_guard lock(mMutex);
		mStopping = true;
	}
	mWake.notify_one();
	mThread.join();
}

RenderTicket RenderThread::post(std::function<void()> work) {
	mProducer.requireCurrentThread();
	std::lock_guard lock(mMutex);
	if (mOutstanding == mCapacity || mStopping) {
		throw std::logic_error("Render thread slot is unavailable");
	}
	Job job { std::move(work), {} };
	auto ticket = job.Completion.get_future().share();
	mJobs.push_back(std::move(job));
	++mOutstanding;
	mWake.notify_one();
	return ticket;
}

void RenderThread::run() {
	for (;;) {
		Job job;
		{
			std::unique_lock lock(mMutex);
			mWake.wait(lock, [this] { return !mJobs.empty() || mStopping; });
			if (mJobs.empty()) {
				return;
			}
			job = std::move(mJobs.front());
			mJobs.pop_front();
		}
		std::exception_ptr failure;
		try {
			job.Work();
		} catch (...) {
			failure = std::current_exception();
		}
		job.Work = {};
		std::lock_guard lock(mMutex);
		--mOutstanding;
		if (failure) {
			job.Completion.set_exception(failure);
		}
		else {
			job.Completion.set_value();
		}
	}
}
}
