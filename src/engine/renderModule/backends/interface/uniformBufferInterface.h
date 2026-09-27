#pragma once
#include <cstddef>

namespace IKIGAI::RENDER
{
	class UniformBufferInterface {
	protected:
		size_t mSizeByte = 0;
	public:
		UniformBufferInterface(size_t sz) : mSizeByte(sz) {};
		virtual ~UniformBufferInterface() = default;
		virtual void setData(const void* data, size_t sz, size_t offset = 0) = 0;

		template<class T>
		void setData(const T& data) {
			setData((void*) & data, sizeof(T));
		}
	};

	//TODO: push constant detect automaticly by reflection
	class PushConstantInterface {
	public:
		virtual ~PushConstantInterface() = default;
	};
}
