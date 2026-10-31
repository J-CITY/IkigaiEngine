#include "driverVk.h"

#ifdef VULKAN_BACKEND
#include <iostream>

namespace IKIGAI::RENDER {
namespace {
	constexpr uint32_t TimestampQueryCount = 4096;
}

void DriverVk::createTimestampQueries() {
	const auto families = mPhysicalDevice.getQueueFamilyProperties();
	const uint32_t validBits = mQueueFamilyIndex < families.size() ? families[mQueueFamilyIndex].timestampValidBits : 0;
	const auto limits = mPhysicalDevice.getProperties().limits;
	if (validBits == 0 || limits.timestampPeriod <= 0.0f) {
		std::cout << "Vulkan timestamp queries unavailable on the graphics queue\n";
		return;
	}
	auto info = vk::QueryPoolCreateInfo().setQueryType(vk::QueryType::eTimestamp).setQueryCount(TimestampQueryCount);
	mTimestampPool = mDevice.createQueryPool(info);
	mTimestampCapacity = TimestampQueryCount;
	mTimestampPeriodNanoseconds = limits.timestampPeriod;
	mTimestampMask = validBits >= 64 ? ~uint64_t { 0 } : ((uint64_t { 1 } << validBits) - 1);
	// Queries start in an undefined state; the first begin() resets the pool.
	mTimestampPoolNeedsReset = true;
}

void DriverVk::resetTimestampPoolIfNeeded() {
	if (mTimestampPoolNeedsReset && *mTimestampPool) {
		getCurrentFrame().mCommandBuffer.resetQueryPool(*mTimestampPool, 0, mTimestampCapacity);
		mTimestampPoolNeedsReset = false;
	}
}

uint32_t DriverVk::timestampQueryCapacity() const {
	return mTimestampCapacity;
}

void DriverVk::writeTimestamp(uint32_t index) {
	if (index >= mTimestampCapacity) {
		throw UnsupportedGpuFeature("Timestamp query index out of range");
	}
	// ALL_COMMANDS: the timestamp is written once all previously recorded work completed,
	// so consecutive stage intervals do not overlap. Valid inside dynamic rendering.
	getCurrentFrame().mCommandBuffer.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *mTimestampPool, index);
}

bool DriverVk::readTimestamps(uint32_t first, uint32_t count, std::vector<uint64_t>& ticks) {
	if (count == 0 || first >= mTimestampCapacity || count > mTimestampCapacity - first) {
		return false;
	}
	ticks.resize(count);
	const VkResult result = vkGetQueryPoolResults(
		*mDevice, *mTimestampPool, first, count, count * sizeof(uint64_t), ticks.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
	if (result != VK_SUCCESS) {
		return false;
	}
	for (auto& value : ticks) {
		value &= mTimestampMask;
	}
	// Results were consumed: reset the queries in the current command buffer so the
	// caller may write them again later in this frame. Resets are illegal inside a render pass.
	deactivateRenderPass();
	getCurrentFrame().mCommandBuffer.resetQueryPool(*mTimestampPool, first, count);
	return true;
}
}
#endif
