#pragma once
#include <cstdint>
#include <memory>

namespace IKIGAI::DATA {
struct EntityRef {
	uint64_t id = 0;
	std::weak_ptr<void> handle {};
	bool (*isLive)(const std::shared_ptr<void>&) = nullptr;
	bool alive() const;
	bool operator==(const EntityRef&) const;
};
}
