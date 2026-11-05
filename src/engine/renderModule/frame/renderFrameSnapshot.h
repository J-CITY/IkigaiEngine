#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace IKIGAI::RENDER {
// CPU matrices use the engine storage order, independent of ECS lifetime and backend UBO layout.
struct RenderViewSnapshot {
	uint64_t ViewId = 0;
	std::array<float, 16> View {};
	std::array<float, 16> Projection {};
	std::array<float, 16> PreviousViewProjection {};
	std::array<float, 3> Position {};
	std::array<uint32_t, 2> RenderSize {};
	std::array<uint32_t, 2> DisplaySize {};
	std::array<float, 2> Jitter {};
	float Near = 0.0f;
	float Far = 0.0f;
};

class RenderFrameSnapshot final {
public:
	RenderFrameSnapshot(uint64_t frameId, std::vector<RenderViewSnapshot> views);
	[[nodiscard]] uint64_t frameId() const noexcept;
	[[nodiscard]] const std::vector<RenderViewSnapshot>& views() const noexcept;

private:
	uint64_t mFrameId = 0;
	std::vector<RenderViewSnapshot> mViews {};
};
}
