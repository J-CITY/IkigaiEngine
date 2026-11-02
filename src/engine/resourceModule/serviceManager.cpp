#include "serviceManager.h"

using namespace IKIGAI;
using namespace IKIGAI::RESOURCES;

std::mutex ServiceManager::MUTEX;
std::map<std::type_index, ServiceType> ServiceManager::SERVICES;
