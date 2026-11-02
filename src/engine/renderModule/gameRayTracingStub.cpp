#include "gameRenderer.h"

#include <memory>
#include <stdexcept>

#ifdef VULKAN_BACKEND
#include "backends/vk/driverVk.h"
#endif
#ifdef DX12_BACKEND
#include "backends/dx12/driverDx12.h"
#endif

namespace IKIGAI::RENDER {
namespace {
	[[noreturn]] void rayTracingNotCompiled() {
		throw UnsupportedRayTracing("Ray tracing was not compiled into this binary");
	}
}

std::shared_ptr<RayTracingProgram> LoadRayTracingProgram(Renderer&, const std::string&) {
	rayTracingNotCompiled();
}

void GameRenderer::prepareRayTracingScene(const std::vector<RenderChunk>&) {
	rayTracingNotCompiled();
}

void GameRenderer::runRayTracingStage(PipelineStage&) {
	rayTracingNotCompiled();
}

#ifdef VULKAN_BACKEND
void DriverVk::configureRayTracing(std::vector<const char*>&) { }

std::shared_ptr<VertexBufferInterface> DriverVk::createRayTracingVertexBuffer(const void*, size_t, size_t) {
	rayTracingNotCompiled();
}
std::shared_ptr<AccelerationStructure> DriverVk::createBottomLevelAS(const std::vector<RayTracingGeometry>&, bool) {
	rayTracingNotCompiled();
}
void DriverVk::refitBottomLevelAS(const std::shared_ptr<AccelerationStructure>&) {
	rayTracingNotCompiled();
}
void DriverVk::refitTopLevelAS(const std::shared_ptr<AccelerationStructure>&, const std::vector<RayTracingInstance>&) {
	rayTracingNotCompiled();
}
std::shared_ptr<AccelerationStructure> DriverVk::createTopLevelAS(const std::vector<RayTracingInstance>&, bool) {
	rayTracingNotCompiled();
}
std::shared_ptr<RayTracingProgram> DriverVk::createRayTracingProgram(const RayTracingProgramResource&) {
	rayTracingNotCompiled();
}
std::shared_ptr<RayTracingProgram> DriverVk::createComputeProgram(const RayTracingProgramResource&) {
	rayTracingNotCompiled();
}
std::shared_ptr<RayTracingProgram> DriverVk::createRayTracingProgramImpl(const RayTracingProgramResource&, bool) {
	rayTracingNotCompiled();
}
void DriverVk::traceRays(const RayTracingDispatch&) {
	rayTracingNotCompiled();
}
void DriverVk::dispatchRayQueries(const RayTracingDispatch&) {
	rayTracingNotCompiled();
}
void DriverVk::dispatchComputeProgram(const RayTracingDispatch&) {
	rayTracingNotCompiled();
}
void DriverVk::executeRayProgram(const RayTracingDispatch&, bool) {
	rayTracingNotCompiled();
}
#endif

#ifdef DX12_BACKEND
void DriverDx12::configureRayTracing() { }

std::shared_ptr<VertexBufferInterface> DriverDx12::createRayTracingVertexBuffer(const void*, size_t, size_t) {
	rayTracingNotCompiled();
}
std::shared_ptr<AccelerationStructure> DriverDx12::createBottomLevelAS(const std::vector<RayTracingGeometry>&, bool) {
	rayTracingNotCompiled();
}
std::shared_ptr<AccelerationStructure> DriverDx12::createTopLevelAS(const std::vector<RayTracingInstance>&, bool) {
	rayTracingNotCompiled();
}
void DriverDx12::refitBottomLevelAS(const std::shared_ptr<AccelerationStructure>&) {
	rayTracingNotCompiled();
}
void DriverDx12::refitTopLevelAS(const std::shared_ptr<AccelerationStructure>&, const std::vector<RayTracingInstance>&) {
	rayTracingNotCompiled();
}
std::shared_ptr<RayTracingProgram> DriverDx12::createRayTracingProgram(const RayTracingProgramResource&) {
	rayTracingNotCompiled();
}
std::shared_ptr<RayTracingProgram> DriverDx12::createComputeProgram(const RayTracingProgramResource&) {
	rayTracingNotCompiled();
}
std::shared_ptr<RayTracingProgram> DriverDx12::createRayProgram(const RayTracingProgramResource&, bool) {
	rayTracingNotCompiled();
}
void DriverDx12::traceRays(const RayTracingDispatch&) {
	rayTracingNotCompiled();
}
void DriverDx12::dispatchRayQueries(const RayTracingDispatch&) {
	rayTracingNotCompiled();
}
void DriverDx12::dispatchComputeProgram(const RayTracingDispatch&) {
	rayTracingNotCompiled();
}
void DriverDx12::executeRayProgram(const RayTracingDispatch&, bool) {
	rayTracingNotCompiled();
}
#endif
}
