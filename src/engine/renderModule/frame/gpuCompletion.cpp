#include "gpuCompletion.h"
#include <limits>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	std::atomic<uint64_t> nextDomain { 1 };
}
GpuCompletionSource::GpuCompletionSource()
	: mDomain(nextDomain.fetch_add(1, std::memory_order_relaxed)) { }

GpuCompletionToken GpuCompletionSource::submitted() {
	const auto previous = mSubmitted.load(std::memory_order_relaxed);
	if (previous == std::numeric_limits<uint64_t>::max()) {
		throw std::overflow_error("GPU completion timeline exhausted");
	}
	mSubmitted.store(previous + 1, std::memory_order_release);
	return { mDomain, previous + 1 };
}

GpuCompletionToken GpuCompletionSource::lastSubmitted() const noexcept {
	return { mDomain, mSubmitted.load(std::memory_order_acquire) };
}

void GpuCompletionSource::validate(GpuCompletionToken token) const {
	if (token.Domain != mDomain || token.Value > mSubmitted.load(std::memory_order_acquire)) {
		throw std::invalid_argument("Foreign or unsubmitted GPU completion token");
	}
}

bool GpuCompletionSource::isComplete(GpuCompletionToken token) const {
	validate(token);
	return mCompleted.load(std::memory_order_acquire) >= token.Value;
}

void GpuCompletionSource::completed(GpuCompletionToken token) {
	validate(token);
	auto previous = mCompleted.load(std::memory_order_relaxed);
	while (previous < token.Value && !mCompleted.compare_exchange_weak(previous, token.Value, std::memory_order_release, std::memory_order_relaxed)) {
	}
}
}
