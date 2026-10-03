#include "app.h"

#include <iostream>
#include <stdexcept>

#include "coreModule/ecs/componentManager.h"
#include "editorModule/EditorRender.h"
#include "physicsModule/PhysicWorld.h"
#include "renderModule/gameRendererInterface.h"
#include "renderModule/render.h"
#include "resourceModule/fileWatcher.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/parser/assimpParser.h"
#include "sceneModule/sceneManager.h"
#include "taskModule/taskSystem.h"
#include "utilsModule/time/time.h"
#include "windowModule/window/window.h"
#include "windowModule/inputManager/inputActions.h"
#include "windowModule/inputManager/inputManager.h"
#include "ecsModule/world.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "renderModule/gameRenderer.h"
#include "renderModule/backends/interface/driverInterface.h"

using namespace IKIGAI::CORE;

//IKIGAI::PROFILER::Profiler profiler;

//TODO: move gamepad and my imgui widgets

namespace
{
	int gWebLogFrames = 8;

	void WebLog(const char* msg)
	{
#ifdef __EMSCRIPTEN__
		if (gWebLogFrames <= 0) {
			return;
		}
		std::cout << "[web] " << msg << std::endl;
		std::cout.flush();
#endif
	}

#ifdef __EMSCRIPTEN__
	void emscriptenMainLoop(IKIGAI::CORE::App* application)
	{
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


App::App(
#ifdef OCULUS
		android_app* app
#endif
		): core(
#ifdef DX12_BACKEND
	hInstance
#endif
#ifdef OCULUS
	app
#endif
) {
}

App::~App() = default;

void App::run() {
#ifdef __EMSCRIPTEN__
	std::cout << "[web] App::run, starting main loop" << std::endl;
	std::cout.flush();
	emscripten_set_main_loop_arg((em_arg_callback_func)::emscriptenMainLoop, this, 0, 1);
#else
	while (runMainLoop()) {

	}
#endif
}

//#include "../../../core/assets.hpp"
//#include "../../../core/graphics-wrapper.hpp"
//#include "../../../core/log.hpp"
//#include "../../../core/perspective-camera.hpp"
//#include "../../../core/sdl-wrapper.hpp"
//#include "../../../application/opengl/opengl-mesh.hpp"
//#include "../../../application/opengl/opengl-pipeline.hpp"
//#include "../../../application/opengl/opengl-texture.hpp"

#ifdef USE_SDL
//ast::PerspectiveCamera createCamera()
//{
//	std::pair<uint32_t, uint32_t> displaySize{ ast::sdl::getDisplaySize() };
//
//	return ast::PerspectiveCamera(static_cast<float>(displaySize.first), static_cast<float>(displaySize.second));
//}

//glm::mat4 createMeshTransform()
//{
//	glm::mat4 identity{ 1.0f };
//	glm::vec3 position{ 0.0f, 0.0f, 0.0f };
//	glm::vec3 rotationAxis{ 0.0f, 1.0f, 0.0f };
//	glm::vec3 scale{ 1.0f, 1.0f, 1.0f };
//	float rotationDegrees{ 45.0f };
//
//	return glm::translate(identity, position) *
//		glm::rotate(identity, glm::radians(rotationDegrees), rotationAxis) *
//		glm::scale(identity, scale);
//}
#endif
//std::shared_ptr<IKIGAI::RENDER::MaterialGl> material;
//std::shared_ptr<IKIGAI::RENDER::ModelGl> model;
//std::shared_ptr<IKIGAI::RENDER::TextureGl> tex;

bool App::runMainLoop() {
	static int webFrame = 0;
	const int frame = webFrame++;
#ifdef __EMSCRIPTEN__
	auto step = [frame](const char* name) {
		if (frame < 8) {
			std::cout << "[web] frame " << frame << " " << name << std::endl;
			std::cout.flush();
		}
	};
#else
	auto step = [](const char*) {};
#endif

	step("enter");
	if (!isRunning()) {
		step("not running");
		return false;
	}
	static bool init = false;
	if (!init)
	{
//#ifdef WIN32
//		glewInit();
//#endif
//#ifdef USE_SDL
//		auto sz = core.window->getDrawableSize();
//		//ast::log(logTag, "Created OpenGL context with viewport size: " + std::to_string(viewportWidth) + " x " + std::to_string(viewportHeight));
//
//		glClearDepthf(1.0f);
//		glEnable(GL_DEPTH_TEST);
//		glDepthFunc(GL_LEQUAL);
//		glEnable(GL_CULL_FACE);
//		glViewport(0, 0, sz.first, sz.second);
//#endif

		//material = std::make_shared<RENDER::MaterialGl>();
		//
		//std::string objFilename = "assets/models/crate.obj";
		//std::string texFilename = "assets/textures/crate.png";
		//std::string vshaderFilename = "assets/shaders/opengl/default.vert";
		//std::string fshaderFilename = "assets/shaders/opengl/default.frag";
		//
        ////std::string path = SDL_AndroidGetInternalStoragePath();
		////path += "/";
		//
		////if android
		////gHelperObject->ExtractAssetReturnFilename("assets/models/crate.obj", objFilename, false);
		////gHelperObject->ExtractAssetReturnFilename("assets/shaders/opengl/default.vert", vshaderFilename, false);
		////gHelperObject->ExtractAssetReturnFilename("assets/shaders/opengl/default.frag", fshaderFilename, false);
		////gHelperObject->ExtractAssetReturnFilename("assets/textures/crate.png", texFilename, false);
		//
		////auto shader = std::make_shared<RENDER::ShaderGl>(path + "assets/shaders/opengl/default.vert", path + "assets/shaders/opengl/default.frag");
		//auto shader = std::make_shared<RENDER::ShaderGl>(vshaderFilename, fshaderFilename);
		////tex = RENDER::TextureGl::Create(path + "assets/textures/crate.png", true);
		//tex = RENDER::TextureGl::Create(texFilename, true);
		//material->setShader(shader);
		//material->mUniformData["u_sampler"] = tex;
		//
		////model = std::make_shared<RENDER::ModelGl>(path + "assets/models/crate.obj");
		//model = std::make_shared<RENDER::ModelGl>(objFilename);
		//RESOURCES::AssimpParser parser;
		//IKIGAI::RESOURCES::ModelParserFlags flags = IKIGAI::RESOURCES::ModelParserFlags::TRIANGULATE;
		//flags |= IKIGAI::RESOURCES::ModelParserFlags::GEN_SMOOTH_NORMALS;
		//flags |= IKIGAI::RESOURCES::ModelParserFlags::FLIP_UVS;
		//flags |= IKIGAI::RESOURCES::ModelParserFlags::GEN_UV_COORDS;
		//flags |= IKIGAI::RESOURCES::ModelParserFlags::CALC_TANGENT_SPACE;
		//flags |= IKIGAI::RESOURCES::ModelParserFlags::JOIN_IDENTICAL_VERTICES;
		//flags |= IKIGAI::RESOURCES::ModelParserFlags::IMPROVE_CACHE_LOCALITY;
		//flags |= IKIGAI::RESOURCES::ModelParserFlags::FIND_INVALID_DATA;
		//flags |= IKIGAI::RESOURCES::ModelParserFlags::FIND_INSTANCES;
		//flags |= IKIGAI::RESOURCES::ModelParserFlags::OPTIMIZE_MESHES;
		//flags |= IKIGAI::RESOURCES::ModelParserFlags::OPTIMIZE_GRAPH;
		//flags |= IKIGAI::RESOURCES::ModelParserFlags::DEBONE;
		////parser.LoadModel(path + "assets/models/crate.obj", model, flags);
		//parser.LoadModel(objFilename, model, flags);

		init = true;
	}
	//static ast::PerspectiveCamera camera = createCamera();
	//static ast::OpenGLPipeline defaultPipeline = ast::OpenGLPipeline("default");
	//static ast::OpenGLMesh mesh = ast::OpenGLMesh(ast::assets::loadOBJFile("assets/models/crate.obj"));
	//static glm::mat4 meshTransform = createMeshTransform();
	//static ast::OpenGLTexture texture = ast::OpenGLTexture(ast::assets::loadBitmap("assets/textures/crate.png"));


	//std::cout << model->getMeshes()[0]->getIndexCount() << std::endl;
	step("pollEvent");
	core.window->pollEvent();
	/*
	glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#ifdef USE_SDL
	const glm::mat4 mvp{
		camera.getProjectionMatrix() *
		camera.getViewMatrix() *
		meshTransform };
#endif
	//defaultPipeline.render(mesh, texture, mvp);
	auto _s = std::static_pointer_cast<RENDER::ShaderGl>(material->getShader());
	material->bind(tex, true);
	//_s->bind();

	model->getMeshes()[0]->bind();
	//std::static_pointer_cast<RENDER::MeshGl>(model->getMeshes()[0])->mVertexBuffer->bind(*material->getShader());
	//std::static_pointer_cast<RENDER::MeshGl>(model->getMeshes()[0])->mIndexBuffer->bind(*material->getShader());
	// // Activate the 'a_vertexPosition' attribute and specify how it should be configured.
	//glEnableVertexAttribArray(0);
	//glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
	//// Activate the 'a_texCoord' attribute and specify how it should be configured.
	//glEnableVertexAttribArray(1);
	//glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
#ifdef USE_SDL
	_s->setMat4("u_mvp", mvp);
#endif
	//tex->bind(0);

	glDrawElements(GL_TRIANGLES, model->getMeshes()[0]->getIndexCount(), GL_UNSIGNED_INT, (GLvoid*)0);

	//glDisableVertexAttribArray(0);
	//glDisableVertexAttribArray(1);
	//std::static_pointer_cast<RENDER::MeshGl>(model->getMeshes()[0])->mVertexBuffer->unbind();
	//std::static_pointer_cast<RENDER::MeshGl>(model->getMeshes()[0])->mIndexBuffer->unbind();
	model->getMeshes()[0]->unbind();

	//_s->unbind();
	material->unbind();
	*/


	//profiler.Update(TIME::Timer::GetInstance().getDeltaTime().count());
	step("preUpdate");
	preUpdate(TIME::Timer::GetInstance().getDeltaTime());
	step("update");
	update(TIME::Timer::GetInstance().getDeltaTime());
	step("postUpdate");
	postUpdate(TIME::Timer::GetInstance().getDeltaTime());
	TIME::Timer::GetInstance().update();
	step("done");
#ifdef __EMSCRIPTEN__
	if (gWebLogFrames > 0) {
		--gWebLogFrames;
	}
#endif
	return true;
}

bool App::isRunning() const {
	return !core.window->isClosed();
}

void App::preUpdate(std::chrono::duration<double> dt) {
	core.window->preUpdate();
#ifdef USE_EDITOR
#ifndef OCULUS
	if (core.editorRender) {
		core.editorRender->draw();
	}
#endif
#endif
}

void App::update(std::chrono::duration<double> dt) {
	//PROFILER_EVENT();
	//core.renderer->getUBO().setSubData(static_cast<float>(TIME::Timer::GetInstance().getTimeSinceStart().count()), 
	//	3 * sizeof(MATHGL::Matrix4) + sizeof(MATHGL::Vector3));

	//EMSCRIPTEN has problems with threads
#ifndef __EMSCRIPTEN__
	auto taskUpdatePhysics = RESOURCES::ServiceManager::Get<TASK::TaskSystem>().submit("UpdatePhysics", 2, nullptr, [this]() {
		core.physicsManger->startFrame();
		auto duration = static_cast<float>(TIME::Timer::GetInstance().getDeltaTime().count());
		if (duration > 0.0f) {
			core.physicsManger->runPhysics(duration);
		}
	});

	static std::chrono::duration<double> fwwait = std::chrono::milliseconds(0);
	fwwait += dt;
	if (fwwait > std::chrono::milliseconds(5000)) {
		fwwait = std::chrono::milliseconds(0);
		auto taskUpdatePhysics = RESOURCES::ServiceManager::Get<TASK::TaskSystem>().submit("UpdateFileWatcher", 2, nullptr, [this]() {
			RESOURCES::FileWatcher::getInstance()->update();
		});
	}
	RESOURCES::FileWatcher::getInstance()->applyUpdate();
#endif

	WebLog("physics start");
	core.physicsManger->startFrame();
	auto duration = static_cast<float>(TIME::Timer::GetInstance().getDeltaTime().count());
	if (duration > 0.0f) {
		core.physicsManger->runPhysics(duration);
	}
	WebLog("physics done");
	
#if defined(USE_SDL) || defined(OCULUS)
	if (RESOURCES::ServiceManager::Check<INPUT_SYSTEM::InputActions>() && RESOURCES::ServiceManager::Check<ECS2::World>()) {
		auto& actions = RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputActions>();
		actions.prepareMaps(RESOURCES::ServiceManager::Get<ECS2::World>());
		actions.resolve(dt);
	}
#endif

	if (core.sceneManager->hasCurrentScene()) {
		WebLog("scene update");
		auto& currentScene = core.sceneManager->getCurrentScene();
		WebLog("scene fixedUpdate");
		currentScene.fixedUpdate(dt);
		WebLog("scene fixedUpdate done");

		auto& world = RESOURCES::ServiceManager::Get<ECS2::World>();
		auto systemManager = world.getSystemManager();
		WebLog("ecs runFixedUpdate");
		systemManager->runFixedUpdate(dt);
		WebLog("ecs runFixedUpdate done");
		WebLog("ecs runUpdate");
		systemManager->runUpdate(dt);
		WebLog("ecs runUpdate done");
		WebLog("scene object update");
		currentScene.update(dt);
		WebLog("scene object update done");
		WebLog("ecs runLateUpdate");
		systemManager->runLateUpdate(dt);
		WebLog("ecs runLateUpdate done");
#ifndef OCULUS
		WebLog("window update");
		core.window->update();
		WebLog("renderScene");
		core.renderer->renderScene();
		WebLog("renderScene done");
#endif
	}
#ifndef __EMSCRIPTEN__
	RESOURCES::ServiceManager::Get<TASK::TaskSystem>().waitSync();
#endif

	if (RENDER::DriverInterface::settings.backend != RENDER::RenderSettings::Backend::OPENGL) {
		RESOURCES::ServiceManager::Get<RENDER::Renderer>().submit();
	}

	//EDITOR
#ifdef USE_EDITOR
#ifndef OCULUS
#endif
#endif

#ifndef OCULUS
#ifndef VULKAN_BACKEND
	//core.window->update();
#endif
#else
	core.window->update([this](XrCompositionLayerProjectionView &layerView,
							   render_target_t &rtarget, XrPosef &stagePose,
							   uint32_t viewID) {
		static_cast<RENDER::GameRenderer*>(core.renderer.get())->renderSceneOculus(layerView, rtarget, stagePose, viewID);
	});
#endif



	WebLog("sceneManager update");
	core.sceneManager->update();
	WebLog("sceneManager update done");
}

void App::postUpdate(std::chrono::duration<double> dt) {
	//PROFILER_EVENT();
	//core.window->pollEvent();
//#ifdef OPENGL_BACKEND
//	core.debugRender->draw(core);
//#endif
//	core.debugRender->postDraw();
	WebLog("window draw");
	core.window->draw();
	WebLog("window draw done");
#if defined(USE_SDL) || defined(OCULUS)
	if (RESOURCES::ServiceManager::Check<INPUT_SYSTEM::InputManager>()) {
		RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputManager>().endFrame();
	}
#endif
}

IKIGAI::UTILS::Ref<Core> App::getCore() {
	return core;
}
