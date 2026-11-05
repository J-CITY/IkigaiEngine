#pragma once
#include <atomic>
#include <cstdint>

namespace IKIGAI::RENDER {
struct GpuCompletionToken {
	uint64_t Domain = 0;
	uint64_t Value = 0;
	bool operator==(const GpuCompletionToken&) const = default;
};

// One ordered submission queue. Values from different domains cannot be compared.
// Native completion callbacks may publish completion; recording remains owner-thread only.
class GpuCompletionSource final {
public:
	GpuCompletionSource();
	GpuCompletionToken submitted();
	void completed(GpuCompletionToken token);
	[[nodiscard]] GpuCompletionToken lastSubmitted() const noexcept;
	[[nodiscard]] bool isComplete(GpuCompletionToken token) const;
	void validate(GpuCompletionToken token) const;

private:
	uint64_t mDomain = 0;
	std::atomic<uint64_t> mSubmitted { 0 };
	std::atomic<uint64_t> mCompleted { 0 };
};
}
