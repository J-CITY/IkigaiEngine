#pragma once
#include <memory>

#include "ecsModule/systemBatchExecutor.h"

namespace IKIGAI::TASK {
class TaskSystem;
}

namespace IKIGAI::ECS {
SystemBatchExecutor makeTaskSystemBatchExecutor(std::weak_ptr<TASK::TaskSystem> tasks);
}
