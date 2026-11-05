#include "app.h"
#include "captureModule/screenshot.h"
#include "coreModule/ecs/componentTypes.h"
#include "coreModule/testHooks.h"
#include "coroutineModule/scheduler.h"
#include "ecsModule/world.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/systems/destructionSystem.h"
#endif
#include "editorModule/editorRender.h"
#include "networkModule/networkRuntime.h"
#include "profilerModule/profiler.h"
#include "profilerModule/profilerSpy.h"
#include "renderModule/gameRendererInterface.h"
#include "renderModule/render.h"
#include "resourceModule/fileWatcher.h"
#include "resourceModule/parser/assimpParser.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"
#include "taskModule/taskSystem.h"
#include "uiModule/viewHost.h"
#include "utilsModule/time/time.h"
#include "windowModule/inputManager/inputActions.h"
#include "windowModule/inputManager/inputManager.h"
#include "windowModule/window/window.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "renderModule/backends/driverFactory.h"
#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/gameRenderer.h"
#ifdef USE_SDL
#include <SDL3/SDL.h>
#endif

using namespace IKIGAI::CORE;

namespace {
#ifdef __EMSCRIPTEN__
void emscriptenMainLoop(IKIGAI::CORE::App* application) {
	try {
		application->runMainLoop();
	} catch (const std::exception& e) {
		std::cout << "[web] EXCEPTION in main loop: " << e.what() << std::endl;
		std::cout.flush();
	} catch (...) {
		std::cout << "[web] UNKNOWN EXCEPTION in main loop" << std::endl;
		std::cout.flush();
	}
}
#endif
} // namespace

App::App(const PlatformStartup& platform)
	: core(platform) { }

App::~App() = default;

void App::run() {
#ifdef __EMSCRIPTEN__
	std::cout << "[web] App::run, starting main loop" << std::endl;
	std::cout.flush();
	emscripten_set_main_loop_arg((em_arg_callback_func)::emscriptenMainLoop, this, 0, 1);
#else
	while (runMainLoop()) { }
#endif
}

bool App::runMainLoop() {
	core.network->poll();

	if (!isRunning()) {
		return false;
	}
	auto& timer = TIME::Timer::GetInstance();
	timer.update();
	const auto frameDt = timer.getDeltaTime();
	static bool init = false;
	if (!init) {
		init = true;
	}

	core.window->pollEvent();
	CAPTURE::ScreenshotService::Get().status();
#if defined(USE_SDL) || defined(OCULUS)
	if (core.inputManager && core.inputManager->wasKeyPressedThisFrame(INPUT_SYSTEM::EKey::KEY_F12)) {
		CAPTURE::ScreenshotService::Get().request();
	}
#endif

	{
		auto coroutineProfile
			= IKIGAI::PROFILER::Profiler::IsEnabled() ? std::make_unique<IKIGAI::PROFILER::ProfilerSpy>("CoroutineScheduler::tick") : nullptr;
		core.coroutineScheduler->tick(frameDt, timer.getDeltaTimeUnscaled());
	}
	core.render->waitForPendingPresent();
	preUpdate(frameDt);

	update(frameDt);

	postUpdate(frameDt);

	return true;
}

bool App::isRunning() const {
	return !core.window->isClosed();
}

void App::preUpdate(std::chrono::duration<double> dt) {
	core.window->preUpdate();
#if USE_PHYSICS_3D && defined(USE_EDITOR)
	if (core.sceneManager->hasCurrentScene() && !core.sceneManager->isEditingLocked()) {
		core.sceneManager->getCurrentScene().drawPhysics3DDebug();
	}
#endif
#if USE_PHYSICS_2D && defined(USE_EDITOR)
	if (core.sceneManager->hasCurrentScene() && !core.sceneManager->isEditingLocked()) {
		core.sceneManager->getCurrentScene().drawPhysics2DDebug();
	}
#endif
#if USE_JSON_VIEWS && !defined(OCULUS)
	if (core.viewHost && !core.sceneManager->isEditingLocked()) {
		core.viewHost->draw();
	}
#endif
#ifdef USE_EDITOR
#ifndef OCULUS
	if (core.editorRender && !core.sceneManager->isEditingLocked()) {
		core.editorRender->draw();
	}
#endif
#endif
}

void App::update(std::chrono::duration<double> dt) {
	auto* frameDriver = RENDER::DriverInterface::Get();
	if (frameDriver) {
		frameDriver->latencyFrameBegin();
	}

#ifdef USE_FILE_WATCHER
	static std::chrono::duration<double> fwwait = std::chrono::milliseconds(0);
	fwwait += TIME::Timer::GetInstance().getDeltaTimeUnscaled();
	if (fwwait > std::chrono::milliseconds(RESOURCES::FileWatcher::DELAY_UPDATE)) {
		fwwait = std::chrono::milliseconds(0);
		RESOURCES::FileWatcher::getInstance()->requestUpdate();
	}
	if (!core.sceneManager->isEditingLocked() && !core.sceneManager->isPreparingLoadingScene()) {
		RESOURCES::FileWatcher::getInstance()->applyUpdate();
	}
#endif

	const bool simulate = !core.sceneManager->isEditingLocked() && core.sceneManager->hasCurrentScene();
	const auto fixedDt = TIME::Timer::GetInstance().getFixedDeltaTime();
	if (simulate) {
		m_fixedTimeAccumulator += dt;
	}
	else {
		m_fixedTimeAccumulator = std::chrono::duration<double>::zero();
	}
	constexpr int kMaxFixedStepsPerFrame = 8;
	int fixedSteps = 0;
#if USE_PHYSICS_3D
	if (simulate) {
		ECS::DestructionSystem::beginFrame(core.sceneManager->getCurrentScene());
	}
#endif
	while (m_fixedTimeAccumulator >= fixedDt && fixedSteps < kMaxFixedStepsPerFrame) {
		m_fixedTimeAccumulator -= fixedDt;
		++fixedSteps;

		if (core.sceneManager->hasCurrentScene()) {
			auto& currentScene = core.sceneManager->getCurrentScene();

			currentScene.fixedUpdate(fixedDt);

			auto& world = RESOURCES::ServiceManager::Get<ECS::World>();
			auto systemManager = world.getSystemManager();

			systemManager->runFixedUpdate(fixedDt);

#if USE_PHYSICS_3D
			currentScene.stepPhysics3D(fixedDt, false);
#endif
#if USE_PHYSICS_2D
			currentScene.stepPhysics2D(fixedDt);
#endif
			currentScene.finishFixedUpdate(fixedDt);
		}
	}
	if (m_fixedTimeAccumulator >= fixedDt) {
		// Drop excess whole steps under overload, but preserve the fractional remainder.
		m_fixedTimeAccumulator = std::chrono::duration<double>(std::fmod(m_fixedTimeAccumulator.count(), fixedDt.count()));
	}

#if defined(USE_SDL) || defined(OCULUS)
	if (simulate && RESOURCES::ServiceManager::Check<INPUT_SYSTEM::InputManager>() && RESOURCES::ServiceManager::Check<INPUT_SYSTEM::InputActions>()
		&& RESOURCES::ServiceManager::Check<ECS::World>()) {
		auto& input = RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputManager>();
		input.beginInputFrame(dt);
		auto& actions = RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputActions>();
		actions.prepareMaps(RESOURCES::ServiceManager::Get<ECS::World>());
		actions.resolve(dt);
	}
#endif

	bool presentationAllowed = true;
	if (simulate) {

		auto& currentScene = core.sceneManager->getCurrentScene();

		auto& world = RESOURCES::ServiceManager::Get<ECS::World>();
		auto systemManager = world.getSystemManager();

		systemManager->runUpdate(dt);

		currentScene.update(dt);
#if USE_PHYSICS_3D
		currentScene.presentDestructibles(float(m_fixedTimeAccumulator.count() / fixedDt.count()));
#endif

		systemManager->runLateUpdate(dt);
	}
	else {
		core.sceneManager->updateLoadingScene(TIME::Timer::GetInstance().getDeltaTimeUnscaled());
	}
#ifndef OCULUS

	if (frameDriver) {
		frameDriver->latencySimulationEnd();
		frameDriver->latencySubmitBegin();
	}
	core.window->update();
#ifdef USE_SDL
	presentationAllowed = !(SDL_GetWindowFlags(core.window->getSDLWindow()) & SDL_WINDOW_MINIMIZED);
	if (!presentationAllowed) {
		if (frameDriver && frameDriver->mFrameGeneration && frameDriver->mFrameGenerationStatus.Active) {
			frameDriver->mFrameGeneration->suspend();
			frameDriver->mFrameGeneration->waitForPresents();
			frameDriver->mFrameGenerationStatus = frameDriver->mFrameGeneration->status();
		}
		if (auto* game = dynamic_cast<RENDER::GameRenderer*>(core.renderer.get())) {
			game->resetTemporalHistory();
		}
	}
#endif

	if (presentationAllowed) {
		core.render->beginFrame();
		core.renderer->renderScene();
	}

#endif
#ifndef __EMSCRIPTEN__
	RESOURCES::ServiceManager::Get<TASK::TaskSystem>().waitSync();
#endif

#ifndef OCULUS
	if (presentationAllowed && RENDER::DriverInterface::settings.backend != RENDER::RenderSettings::Backend::OPENGL) {
#if defined(_WIN32) && defined(USE_SDL)
		if (TESTHOOK::focusPresentation) {
			TESTHOOK::focusPresentation(
				SDL_GetPointerProperty(SDL_GetWindowProperties(core.window->getSDLWindow()), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
		}
#endif
		core.render->submitAsync();
	}
#endif

	// EDITOR
#ifdef USE_EDITOR
#ifndef OCULUS
#endif
#endif

#ifndef OCULUS
#ifndef VULKAN_BACKEND
	// core.window->update();
#endif
#else
	core.render->beginFrame();
	core.window->update([this](XrCompositionLayerProjectionView& layerView, render_target_t& rtarget, XrPosef& stagePose, uint32_t viewID) {
		static_cast<RENDER::GameRenderer*>(core.renderer.get())->renderSceneOculus(layerView, rtarget, stagePose, viewID);
		core.render->flush();
	});
	core.render->submit();
#endif

	if (presentationAllowed && core.sceneManager->hasCurrentScene() && !core.sceneManager->isTransitioning()) {
		core.sceneManager->getCurrentScene().commitRenderTransforms();
	}
	core.sceneManager->update();
}

void App::postUpdate(std::chrono::duration<double> dt) {

	core.window->draw();
#ifndef OCULUS
	if (RENDER::DriverInterface::settings.backend == RENDER::RenderSettings::Backend::OPENGL && core.render->hasActiveFrame()) {
		// The GL window renders ImGui here. Include it in the serial completion boundary.
		core.render->submit();
	}
#endif
	core.sceneManager->notifyPresented();

#if defined(USE_SDL) || defined(OCULUS)
	if (RESOURCES::ServiceManager::Check<INPUT_SYSTEM::InputManager>()) {
		RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputManager>().endFrame();
	}
#endif
}

IKIGAI::UTILS::Ref<Core> App::getCore() {
	return core;
}
