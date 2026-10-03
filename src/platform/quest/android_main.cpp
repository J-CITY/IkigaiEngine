#include "coreModule/core/app.h"
#include "sceneModule/sceneManager.h"

void android_main(struct android_app* app) {
	IKIGAI::CORE::App game(app);
	game.getCore()->sceneManager->loadFromFile("scenes/scene.json");
	game.run();
}
