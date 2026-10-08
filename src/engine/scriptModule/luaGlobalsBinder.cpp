#include "luaGlobalsBinder.h"

#include <sol/sol.hpp>

#include <coreModule/ecs/object.h>
#include <sceneModule/sceneManager.h>
#include <resourceModule/modelManager.h>
#include <resourceModule/materialManager.h>
#include <windowModule/inputManager/inputManager.h>
#include <windowModule/inputManager/inputActions.h>
#include <resourceModule/serviceManager.h>

#include "utilsModule/log/loggerDefine.h"

using namespace IKIGAI;
using namespace IKIGAI::SCRIPTING;

void LuaGlobalsBinder::BindGlobals(sol::state & p_luaState) {
	using namespace IKIGAI::INPUT_SYSTEM;
	using namespace IKIGAI::MATH;
	using namespace IKIGAI::ECS;
	using namespace IKIGAI::SCENE_SYSTEM;

	//auto CreateActorOverload = sol::overload
	//(
	//	sol::resolve<std::shared_ptr<Object>(void)>(&Scene::createObject),
	//	sol::resolve<std::shared_ptr<Object>(const std::string&, const std::string&)>(&Scene::createObject)
	//);

	p_luaState.new_usertype<Scene>("Scene",
		/* Methods */
		"FindActorByName", &Scene::findObjectByName,
		"FindActorByTag", &Scene::findObjectByTag,
		"FindActorsByName", &Scene::findObjectsByTag,
		"FindActorsByTag", &Scene::findObjectsByTag//,
		//"CreateActor", CreateActorOverload
		);

	p_luaState.new_enum<EKey>("Key",
		{
			{"UNKNOWN",			EKey::KEY_UNKNOWN},
#ifndef OCULUS
			{"SPACE",			EKey::KEY_SPACE},
			{"APOSTROPHE",		EKey::KEY_APOSTROPHE},
			{"COMMA",			EKey::KEY_COMMA},
			{"MINUS",			EKey::KEY_MINUS},
			{"PERIOD",			EKey::KEY_PERIOD},
			{"SLASH",			EKey::KEY_SLASH},
			{"ALPHA_0",			EKey::KEY_0},
			{"ALPHA_1",			EKey::KEY_1},
			{"ALPHA_2",			EKey::KEY_2},
			{"ALPHA_3",			EKey::KEY_3},
			{"ALPHA_4",			EKey::KEY_4},
			{"ALPHA_5",			EKey::KEY_5},
			{"ALPHA_6",			EKey::KEY_6},
			{"ALPHA_7",			EKey::KEY_7},
			{"ALPHA_8",			EKey::KEY_8},
			{"ALPHA_9",			EKey::KEY_9},
			{"SEMICOLON",		EKey::KEY_SEMICOLON},
			{"EQUAL",			EKey::KEY_EQUAL},
			{"A",				EKey::KEY_A},
			{"B",				EKey::KEY_B},
			{"C",				EKey::KEY_C},
			{"D",				EKey::KEY_D},
			{"E",				EKey::KEY_E},
			{"F",				EKey::KEY_F},
			{"G",				EKey::KEY_G},
			{"H",				EKey::KEY_H},
			{"I",				EKey::KEY_I},
			{"J",				EKey::KEY_J},
			{"K",				EKey::KEY_K},
			{"L",				EKey::KEY_L},
			{"M",				EKey::KEY_M},
			{"N",				EKey::KEY_N},
			{"O",				EKey::KEY_O},
			{"P",				EKey::KEY_P},
			{"Q",				EKey::KEY_Q},
			{"R",				EKey::KEY_R},
			{"S",				EKey::KEY_S},
			{"T",				EKey::KEY_T},
			{"U",				EKey::KEY_U},
			{"V",				EKey::KEY_V},
			{"W",				EKey::KEY_W},
			{"X",				EKey::KEY_X},
			{"Y",				EKey::KEY_Y},
			{"Z",				EKey::KEY_Z},
			{"LEFT_BRACKET",	EKey::KEY_LEFT_BRACKET},
			{"BACKSLASH",		EKey::KEY_BACKSLASH},
			{"RIGHT_BRACKET",	EKey::KEY_RIGHT_BRACKET},
			{"GRAVE_ACCENT",	EKey::KEY_GRAVE_ACCENT},
#endif
#ifdef USE_GLFW
			{"WORLD_1",			EKey::KEY_WORLD_1},
			{"WORLD_2",			EKey::KEY_WORLD_2},
#endif
#ifndef OCULUS
			{"ESCAPE",			EKey::KEY_ESCAPE},
			{"ENTER",			EKey::KEY_ENTER},
			{"TAB",				EKey::KEY_TAB},
			{"BACKSPACE",		EKey::KEY_BACKSPACE},
			{"INSERT",			EKey::KEY_INSERT},
			{"DELETE",			EKey::KEY_DELETE},
			{"RIGHT",			EKey::KEY_RIGHT},
			{"LEFT",			EKey::KEY_LEFT},
			{"DOWN",			EKey::KEY_DOWN},
			{"UP",				EKey::KEY_UP},
			{"PAGE_UP",			EKey::KEY_PAGE_UP},
			{"PAGE_DOWN",		EKey::KEY_PAGE_DOWN},
			{"HOME",			EKey::KEY_HOME},
			{"END",				EKey::KEY_END},
			{"CAPS_LOCK",		EKey::KEY_CAPS_LOCK},
			{"SCROLL_LOCK",		EKey::KEY_SCROLL_LOCK},
			{"NUM_LOCK",		EKey::KEY_NUM_LOCK},
			{"PRINT_SCREEN",	EKey::KEY_PRINT_SCREEN},
			{"PAUSE",			EKey::KEY_PAUSE},
			{"F1",				EKey::KEY_F1},
			{"F2",				EKey::KEY_F2},
			{"F3",				EKey::KEY_F3},
			{"F4",				EKey::KEY_F4},
			{"F5",				EKey::KEY_F5},
			{"F6",				EKey::KEY_F6},
			{"F7",				EKey::KEY_F7},
			{"F8",				EKey::KEY_F8},
			{"F9",				EKey::KEY_F9},
			{"F10",				EKey::KEY_F10},
			{"F11",				EKey::KEY_F11},
			{"F12",				EKey::KEY_F12},
			{"F13",				EKey::KEY_F13},
			{"F14",				EKey::KEY_F14},
			{"F15",				EKey::KEY_F15},
			{"F16",				EKey::KEY_F16},
			{"F17",				EKey::KEY_F17},
			{"F18",				EKey::KEY_F18},
			{"F19",				EKey::KEY_F19},
			{"F20",				EKey::KEY_F20},
			{"F21",				EKey::KEY_F21},
			{"F22",				EKey::KEY_F22},
			{"F23",				EKey::KEY_F23},
			{"F24",				EKey::KEY_F24},
#endif
#ifdef USE_GLFW
			{"F25",				EKey::KEY_F25},
#endif
#ifndef OCULUS
			{"KP_0",			EKey::KEY_KP_0},
			{"KP_1",			EKey::KEY_KP_1},
			{"KP_2",			EKey::KEY_KP_2},
			{"KP_3",			EKey::KEY_KP_3},
			{"KP_4",			EKey::KEY_KP_4},
			{"KP_5",			EKey::KEY_KP_5},
			{"KP_6",			EKey::KEY_KP_6},
			{"KP_7",			EKey::KEY_KP_7},
			{"KP_8",			EKey::KEY_KP_8},
			{"KP_9",			EKey::KEY_KP_9},
			{"KP_DECIMAL",		EKey::KEY_KP_DECIMAL},
			{"KP_DIVIDE",		EKey::KEY_KP_DIVIDE},
			{"KP_MULTIPLY",		EKey::KEY_KP_MULTIPLY},
			{"KP_SUBTRACT",		EKey::KEY_KP_SUBTRACT},
			{"KP_ADD",			EKey::KEY_KP_ADD},
			{"KP_ENTER",		EKey::KEY_KP_ENTER},
			{"KP_EQUAL",		EKey::KEY_KP_EQUAL},
			{"LEFT_SHIFT",		EKey::KEY_LEFT_SHIFT},
			{"LEFT_CONTROL",	EKey::KEY_LEFT_CONTROL},
			{"LEFT_ALT",		EKey::KEY_LEFT_ALT},
			{"LEFT_SUPER",		EKey::KEY_LEFT_SUPER},
			{"RIGHT_SHIFT",		EKey::KEY_RIGHT_SHIFT},
			{"RIGHT_CONTROL",	EKey::KEY_RIGHT_CONTROL},
			{"RIGHT_ALT",		EKey::KEY_RIGHT_ALT},
			{"RIGHT_SUPER",		EKey::KEY_RIGHT_SUPER},
			{"MENU",			EKey::KEY_MENU}
#endif
		});

		p_luaState.new_enum<EMouseButton>("MouseButton",
		{
			{"BUTTON_1",		EMouseButton::MOUSE_BUTTON_1},
			{"BUTTON_2",		EMouseButton::MOUSE_BUTTON_2},
			{"BUTTON_3",		EMouseButton::MOUSE_BUTTON_3},
			{"BUTTON_4",		EMouseButton::MOUSE_BUTTON_4},
			{"BUTTON_5",		EMouseButton::MOUSE_BUTTON_5},
			{"BUTTON_6",		EMouseButton::MOUSE_BUTTON_6},
			{"BUTTON_7",		EMouseButton::MOUSE_BUTTON_7},
			{"BUTTON_8",		EMouseButton::MOUSE_BUTTON_8},
			{"BUTTON_LEFT",		EMouseButton::MOUSE_BUTTON_LEFT},
			{"BUTTON_RIGHT",	EMouseButton::MOUSE_BUTTON_RIGHT},
			{"BUTTON_MIDDLE",	EMouseButton::MOUSE_BUTTON_MIDDLE},
		});

	p_luaState.create_named_table("Debug");
	p_luaState.create_named_table("Inputs");
	p_luaState.create_named_table("Scenes");
	p_luaState.create_named_table("Resources");
	p_luaState.create_named_table("Math");
	p_luaState.create_named_table("Physics");

	p_luaState["Debug"]["Log"] = [](const std::string& p_message) { LOG_INFO << (p_message); };
	p_luaState["Debug"]["LogIngo"] = [](const std::string& p_message) { LOG_INFO << (p_message); };
	p_luaState["Debug"]["LogWarning"] = [](const std::string& p_message) { LOG_WARNING << (p_message); };
	p_luaState["Debug"]["LogError"] = [](const std::string& p_message) { LOG_ERROR << (p_message); };

	p_luaState["Inputs"]["GetKeyDown"] = [](EKey p_key) { return IKIGAI::RESOURCES::ServiceManager::Get<InputManager>().isKeyPressed(p_key); };
	p_luaState["Inputs"]["GetKeyUp"] = [](EKey p_key) { return IKIGAI::RESOURCES::ServiceManager::Get<InputManager>().isKeyReleased(p_key); };
	
	p_luaState["Inputs"]["GetMouseButtonDown"] = [](EMouseButton p_button) { return IKIGAI::RESOURCES::ServiceManager::Get<InputManager>().isMouseButtonPressed(p_button); };
	p_luaState["Inputs"]["GetMouseButtonUp"] = [](EMouseButton p_button) { return IKIGAI::RESOURCES::ServiceManager::Get<InputManager>().isMouseButtonReleased(p_button); };
	
	p_luaState["Inputs"]["GetMousePos"] = []()
	{
		auto mousePos = IKIGAI::RESOURCES::ServiceManager::Get<InputManager>().getMousePosition();
		return Vector2f(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
	};

	p_luaState["Inputs"]["IsPressed"] = [](const std::string& action) {
		return IKIGAI::RESOURCES::ServiceManager::Get<InputActions>().isPressed(action);
	};
	p_luaState["Inputs"]["WasPressedThisFrame"] = [](const std::string& action) {
		return IKIGAI::RESOURCES::ServiceManager::Get<InputActions>().wasPressedThisFrame(action);
	};
	p_luaState["Inputs"]["WasReleasedThisFrame"] = [](const std::string& action) {
		return IKIGAI::RESOURCES::ServiceManager::Get<InputActions>().wasReleasedThisFrame(action);
	};
	p_luaState["Inputs"]["ReadValue"] = [](sol::this_state state, const std::string& action) -> sol::object {
		auto& actions = IKIGAI::RESOURCES::ServiceManager::Get<InputActions>();
		sol::state_view lua(state);
		const auto value2 = actions.readValue2(action);
		if (value2.x != 0.0f || value2.y != 0.0f) {
			return sol::make_object(lua, value2);
		}
		return sol::make_object(lua, actions.readValue(action));
	};

	p_luaState["Scenes"]["GetCurrentScene"] = []() -> Scene& { return IKIGAI::RESOURCES::ServiceManager::Get<SceneManager>().getCurrentScene(); };

	p_luaState["Resources"]["GetModel"] = [](const std::string& p_resPath) { return IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::ModelLoader>().CreateFromFile(p_resPath, RESOURCES::ModelParserFlags::NONE); };
	//p_luaState["Resources"]["GetShader"] = [](const std::string& p_resPath) { return IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::ShaderLoader>().CreateFromFile(p_resPath); };
	//p_luaState["Resources"]["GetTexture"] = [](const std::string& p_resPath) { return IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::TextureLoader>().CreateFromFile(p_resPath); };
	p_luaState["Resources"]["GetMaterial"] = [](const std::string& p_resPath) { return IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::MaterialLoader>().Create(p_resPath); };
}
