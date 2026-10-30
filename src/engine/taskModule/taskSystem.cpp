#include "taskSystem.h"

using namespace IKIGAI;
using namespace IKIGAI::TASK;

TaskSystemStatus ObjectStatus = TaskSystemStatus::TERMINANED;

TaskSystemStatus TaskSystem::getStatus() {
	return ObjectStatus;
}

bool TaskSystem::setup() {
	mBackgroundExecutor = std::make_unique<ThreadPoolExecutor>(2);
	mThreadPoolExecutor = std::make_unique<ThreadPoolExecutor>();
	ObjectStatus = TaskSystemStatus::START;
	return mThreadPoolExecutor->setup();
}

bool TaskSystem::initialize() {
	if (ObjectStatus == TaskSystemStatus::START) {
		ObjectStatus = TaskSystemStatus::ACTIVE;
		return mThreadPoolExecutor->initialize();
	}
	return false;
}

bool TaskSystem::update() {
	if (ObjectStatus == TaskSystemStatus::ACTIVE) {
		return mThreadPoolExecutor->update();
	}
	else {
		ObjectStatus = TaskSystemStatus::SUSPEND;
		return false;
	}
}

bool TaskSystem::terminate() {
	ObjectStatus = TaskSystemStatus::TERMINANED;
	mBackgroundExecutor.reset();
	return mThreadPoolExecutor->terminate();
}

void TaskSystem::waitSync() {
	mThreadPoolExecutor->waitSync();
}
void TaskSystem::dispatch(const char* name, std::function<void()> work) {
	mBackgroundExecutor->addTask(std::make_unique<Task<std::function<void()>>>(std::move(work), name, nullptr), -1);
}

size_t TaskSystem::getThreadCounts() {
	return mThreadPoolExecutor->getThreadCounts();
}

std::shared_ptr<ITask> TaskSystem::addTask(std::unique_ptr<ITask>&& task, int threadID) {
	return mThreadPoolExecutor->addTask(std::move(task), threadID);
}

std::shared_ptr<ITask> TaskSystem::addTaskFront(std::unique_ptr<ITask>&& task, int threadID) {
	return mThreadPoolExecutor->addTaskFront(std::move(task), threadID);
}

std::shared_ptr<ITask> TaskSystem::addTaskInNewThread(std::unique_ptr<ITask>&& task) {
	return mNewThreadExecutor->addTask(std::move(task));
}
