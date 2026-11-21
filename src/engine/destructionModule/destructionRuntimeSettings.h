#pragma once

#include <cstddef>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>

namespace IKIGAI::DESTRUCTION {
struct RuntimeSettings {
	unsigned MaximumPendingPerObject = 32;
	unsigned MaximumResults = 64;
	unsigned MaximumJobs = 2;
	unsigned MaximumOperationsPerFrame = 4;
	unsigned MaximumBodies = 2048;
	std::size_t MaximumWorkingBytes = 64 * 1024 * 1024;
	std::size_t MaximumGpuBytes = 64 * 1024 * 1024;
	std::size_t MaximumUploadBytesPerFrame = 1024 * 1024;
	double MaximumMainThreadMilliseconds = 2;
	float ImpactCooldown = .2f;
	void validate() const;
	template <class C> static auto serde(C& context, RuntimeSettings& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&RuntimeSettings::MaximumPendingPerObject, "MaximumPendingPerObject", default_ { 32u })
			.field(&RuntimeSettings::MaximumResults, "MaximumResults", default_ { 64u })
			.field(&RuntimeSettings::MaximumJobs, "MaximumJobs", default_ { 2u })
			.field(&RuntimeSettings::MaximumOperationsPerFrame, "MaximumOperationsPerFrame", default_ { 4u })
			.field(&RuntimeSettings::MaximumBodies, "MaximumBodies", default_ { 2048u })
			.field(&RuntimeSettings::MaximumWorkingBytes, "MaximumWorkingBytes", default_ { std::size_t(64 * 1024 * 1024) })
			.field(&RuntimeSettings::MaximumGpuBytes, "MaximumGpuBytes", default_ { std::size_t(64 * 1024 * 1024) })
			.field(&RuntimeSettings::MaximumUploadBytesPerFrame, "MaximumUploadBytesPerFrame", default_ { std::size_t(1024 * 1024) })
			.field(&RuntimeSettings::MaximumMainThreadMilliseconds, "MaximumMainThreadMilliseconds", default_ { 2.0 })
			.field(&RuntimeSettings::ImpactCooldown, "ImpactCooldown", default_ { .2f });
	}
};
}
