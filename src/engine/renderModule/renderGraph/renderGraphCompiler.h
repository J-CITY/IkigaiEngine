#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace IKIGAI::RENDER::GRAPH {
using ResourceId = size_t;
using PassId = size_t;

enum class Access { SAMPLED, UNIFORM, INDIRECT, STORAGE_READ, STORAGE_WRITE, ATTACHMENT_READ, ATTACHMENT_WRITE, TRANSFER_READ, TRANSFER_WRITE };

struct Resource {
	std::string Name;
	bool Imported = false;
	bool Exported = false;
};

struct Use {
	ResourceId Resource = 0;
	uint32_t Version = 0;
	Access Mode = Access::SAMPLED;
};

struct Pass {
	std::string Name;
	std::vector<Use> Uses;
	std::vector<PassId> After;
	bool SideEffect = false;
};

struct Barrier {
	ResourceId Resource = 0;
	bool Uav = false;
};

struct ScheduledPass {
	PassId Id = 0;
	std::vector<Barrier> Barriers;
};

struct Plan {
	std::vector<ScheduledPass> Schedule;
	std::vector<PassId> Culled;
};

[[nodiscard]] bool IsWrite(Access access);
[[nodiscard]] Plan Compile(std::span<const Resource> resources, std::span<const Pass> passes);
}
