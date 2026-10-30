#include "entityRef.h"
namespace IKIGAI::DATA {
bool EntityRef::alive() const {
	auto object = handle.lock();
	return id != 0 && object && (!isLive || isLive(object));
}
bool EntityRef::operator==(const EntityRef& other) const {
	return id == other.id && isLive == other.isLive && !handle.owner_before(other.handle) && !other.handle.owner_before(handle);
}
}
