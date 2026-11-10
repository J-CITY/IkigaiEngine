#pragma once

#ifdef VULKAN_BACKEND
#include "driverVk.h"

namespace IKIGAI::RENDER {
struct UniformSnapshotVk {
	VmaBuffer Buffer;
	bool Recorded = false;
};

class UniformBufferVk : public UniformBufferInterface {
public:
	UniformBufferVk(void* data, size_t size);
	~UniformBufferVk() override;

	template <class T>
	UniformBufferVk(const T& data)
		: UniformBufferVk((void*)&data, sizeof(T)) { }

	[[nodiscard]] const VmaBuffer& getBuffer() const;
	void setData(const void* data, size_t size, size_t offset = 0) override;

private:
	struct Snapshot {
		std::shared_ptr<UniformSnapshotVk> Native;
		std::vector<uint8_t> Bytes;
	};

	void snapshot();

	DriverVk& mDriver;
	std::vector<Snapshot> mSnapshots;
	std::vector<uint8_t> mBytes;
	size_t mSnapshot = 0;
	VmaBuffer mEmpty;
};
}
#endif
