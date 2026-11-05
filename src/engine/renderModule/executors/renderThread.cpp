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

RenderThread::RenderThread()
	: mThread([this] { run(); }) { }

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
	if (mBusy || mStopping) {
		throw std::logic_error("Render thread slot is unavailable");
	}
	mJob.emplace(Job { std::move(work), {} });
	auto ticket = mJob->Completion.get_future().share();
	mBusy = true;
	mWake.notify_one();
	return ticket;
}

void RenderThread::run() {
	for (;;) {
		Job job;
		{
			std::unique_lock lock(mMutex);
			mWake.wait(lock, [this] { return mJob.has_value() || mStopping; });
			if (!mJob) {
				return;
			}
			job = std::move(*mJob);
			mJob.reset();
		}
		std::exception_ptr failure;
		try {
			job.Work();
		} catch (...) {
			failure = std::current_exception();
		}
		job.Work = {};
		std::lock_guard lock(mMutex);
		mBusy = false;
		if (failure) {
			job.Completion.set_exception(failure);
		}
		else {
			job.Completion.set_value();
		}
	}
}
}
