#include "textureWatcher.h"
#ifdef USE_EDITOR
#include "imgui.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "renderModule/gameRenderer.h"
#include "resourceModule/serviceManager.h"

void IKIGAI::EDITOR::TextureWatcherWindow::draw() {
	ImGui::Begin("Texture Watcher");
	try {
		auto& renderer = RESOURCES::ServiceManager::Get<RENDER::GameRendererInterface>();
		const auto& textures = renderer.getCurrentPipeline().mTextures;
		if (textures.empty()) {
			ImGui::TextUnformatted("No pipeline textures");
		}
		for (const auto& [name, texture] : textures) {
			if (!texture) {
				continue;
			}
			ImGui::TextUnformatted(name.c_str());
			ImGui::Image(
				reinterpret_cast<ImTextureID>(texture->getImguiId()),
				ImVec2(128.0f, 128.0f),
				ImVec2(0, 1),
				ImVec2(1, 0));
		}
	} catch (...) {
		ImGui::TextUnformatted("Render pipeline is not ready");
	}
	ImGui::End();
}
#endif
