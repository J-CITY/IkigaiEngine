#include "serviceManager.h"

namespace IKIGAI::UTILS {
std::mutex ServiceManager::MUTEX;
std::map<std::type_index, ServiceType> ServiceManager::SERVICES;
}
