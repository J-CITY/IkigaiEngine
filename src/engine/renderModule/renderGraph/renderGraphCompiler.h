#pragma once
#include <cstddef>
#include <optional>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace IKIGAI::RENDER::GRAPH {
using ResourceId = size_t;
using PassId = size_t;

enum class Access { SAMPLED, UNIFORM, INDIRECT, STORAGE_READ, STORAGE_WRITE, ATTACHMENT_READ, ATTACHMENT_WRITE, TRANSFER_READ, TRANSFER_WRITE };
enum class Queue { GRAPHICS, ASYNC_COMPUTE };

struct Resource {
	std::string Name;
	bool Imported = false;
	bool Exported = false;
	bool Transient = false;
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
	Queue PreferredQueue = Queue::GRAPHICS;
	std::vector<ResourceId> ClearedColorAttachments;
};

struct Barrier {
	struct SourceAccess {
		PassId Pass = 0;
		Access Mode = Access::SAMPLED;
	};

	ResourceId Resource = 0;
	bool Uav = false;
	std::optional<SourceAccess> Before;
	Access After = Access::SAMPLED;
};

struct ScheduledPass {
	PassId Id = 0;
	std::vector<Barrier> Barriers;
	Queue ExecutionQueue = Queue::GRAPHICS;
	bool WaitForCompute = false;
};

struct Plan {
	std::vector<std::string> ResourceNames;
	std::vector<ScheduledPass> Schedule;
	std::vector<PassId> Culled;
	struct TransientLifetime {
		std::string Name;
		size_t First = 0;
		size_t Last = 0;
	};
	std::vector<TransientLifetime> TransientLifetimes;
};

[[nodiscard]] bool IsWrite(Access access);
[[nodiscard]] Plan Compile(std::span<const Resource> resources, std::span<const Pass> passes);
}
