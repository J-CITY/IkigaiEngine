#include "cameraControlWindow.h"
#include "prefabEditor.h"

#ifdef USE_EDITOR
#ifndef OCULUS
#include "imgui.h"
#endif
#include "renderModule/gameRenderer.h"
#include "sceneModule/sceneManager.h"

using namespace IKIGAI::EDITOR;

void CameraControlWindow::draw() {
#ifndef OCULUS
	if (!RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().hasCurrentScene()) {
		return;
	}
	auto& renderSettings = RENDER::DriverInterface::settings;
	const bool supportsUpscale
		= renderSettings.backend == RENDER::RenderSettings::Backend::VULKAN || renderSettings.backend == RENDER::RenderSettings::Backend::DIRECTX12;
#if USE_JSON_VIEWS
	if (!supportsUpscale) {
		return;
	}
#else
	float width = 50.0f;
	float height = 50.0f;
#endif
	static bool show_demo_window = true;
#if USE_JSON_VIEWS
	ImGui::Begin("Render Controls", &show_demo_window);
#else
	ImGui::Begin("Camera Control", &show_demo_window);
#endif
	if (supportsUpscale) {
		const auto& pipeline = RESOURCES::ServiceManager::Get<RENDER::GameRendererInterface>().getCurrentPipeline();
		const bool hasStage = std::any_of(pipeline.mStages.begin(), pipeline.mStages.end(),
			[](const auto& s) { return s->mDescriptor.Enabled && s->mDrawContent == RENDER::DrawContent::UPSCALE; });
		ImGui::SeparatorText("Upscaling");
		ImGui::BeginDisabled(!hasStage);
		int technology = static_cast<int>(renderSettings.upscaler.technology);
		if (ImGui::Combo("Technology", &technology, "Off\0FSR\0DLSS\0Auto\0")) {
			renderSettings.upscaler.technology = static_cast<RENDER::UpscalerType>(technology);
		}
		int quality = static_cast<int>(renderSettings.upscaler.quality);
		if (ImGui::Combo("Quality", &quality, "Quality\0Balanced\0Performance\0")) {
			renderSettings.upscaler.quality = static_cast<RENDER::UpscalerQuality>(quality);
		}
		if (pipeline.mUpscalerStatus.Sharpening) {
			ImGui::SliderFloat("Sharpness", &renderSettings.upscaler.sharpness, 0, 1);
		}
		ImGui::EndDisabled();
		if (!hasStage) {
			ImGui::TextWrapped("Select a pipeline with an UPSCALE stage (migrated-upscale).");
		}
		else {
			ImGui::Text("Render %dx%d -> Display %dx%d", pipeline.mRenderSize.x, pipeline.mRenderSize.y, pipeline.mSize.x, pipeline.mSize.y);
			if (!pipeline.mUpscalerStatus.Implementation.empty()) {
				ImGui::TextWrapped("%s", pipeline.mUpscalerStatus.Implementation.c_str());
			}
			if (!pipeline.mUpscalerStatus.Reason.empty()) {
				ImGui::TextWrapped("Fallback: %s", pipeline.mUpscalerStatus.Reason.c_str());
			}
		}
		const bool hasFG = std::any_of(pipeline.mStages.begin(), pipeline.mStages.end(),
			[](const auto& s) { return s->mDescriptor.Enabled && s->mDrawContent == RENDER::DrawContent::FRAME_GENERATION; });
		ImGui::SeparatorText("Frame Generation");
		ImGui::BeginDisabled(!hasFG);
		int fgTechnology = static_cast<int>(renderSettings.frameGeneration.technology);
		if (ImGui::Combo("FG technology", &fgTechnology, "Off\0FSR\0DLSS\0Auto\0")) {
			renderSettings.frameGeneration.technology = static_cast<RENDER::FrameGenerationType>(fgTechnology);
		}
		int multiplier = static_cast<int>(renderSettings.frameGeneration.multiplier);
		if (ImGui::SliderInt("FG multiplier", &multiplier, 2, 6)) {
			renderSettings.frameGeneration.multiplier = static_cast<unsigned>(multiplier);
		}
		ImGui::Checkbox("FG VSync", &renderSettings.frameGeneration.vsync);
		ImGui::EndDisabled();
		if (auto* driver = RENDER::DriverInterface::Get()) {
			const auto& status = driver->mFrameGenerationStatus;
			if (!status.Implementation.empty()) {
				ImGui::TextWrapped("%s", status.Implementation.c_str());
			}
			if (!status.Reason.empty()) {
				ImGui::TextWrapped("FG unavailable: %s", status.Reason.c_str());
			}
			if (status.Supported) {
				ImGui::Text("Real %llu / Generated %llu / Presented %llu", static_cast<unsigned long long>(status.RealFrames),
					static_cast<unsigned long long>(status.GeneratedFrames), static_cast<unsigned long long>(status.PresentedFrames));
			}
		}
	}

#if !USE_JSON_VIEWS
	auto& curScene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
	auto cameraOpt = curScene.findMainCamera();
	if (!cameraOpt) {
		ImGui::End();
		return;
	}
	auto camera = cameraOpt;
	if (PREFABS::Locked(camera->obj.get())) {
		ImGui::TextUnformatted("Unpack prefab to move its internal camera");
		ImGui::End();
		return;
	}
	if (ImGui::Button("UP", ImVec2(width, height))) {
		auto pos = camera->obj->tryGetTransform()->getLocalPosition();
		pos.y += 0.3f;
		camera->obj->tryGetTransform()->setLocalPosition(pos);
	}
	ImGui::SameLine();
	if (ImGui::Button("FORWARD", ImVec2(width, height))) {
		auto pos = camera->obj->tryGetTransform()->getLocalPosition();
		pos.z += 0.3f;
		camera->obj->tryGetTransform()->setLocalPosition(pos);
	}
	ImGui::SameLine();
	if (ImGui::Button("DOWN", ImVec2(width, height))) {
		auto pos = camera->obj->tryGetTransform()->getLocalPosition();
		pos.y -= 0.3f;
		camera->obj->tryGetTransform()->setLocalPosition(pos);
	}

	if (ImGui::Button("LEFT", ImVec2(width, height))) {
		auto pos = camera->obj->tryGetTransform()->getLocalPosition();
		pos.x += 0.3f;
		camera->obj->tryGetTransform()->setLocalPosition(pos);
	}
	ImGui::SameLine();
	if (ImGui::Button("BACKWARD", ImVec2(width, height))) {
		auto pos = camera->obj->tryGetTransform()->getLocalPosition();
		pos.z -= 0.3f;
		camera->obj->tryGetTransform()->setLocalPosition(pos);
	}
	ImGui::SameLine();
	if (ImGui::Button("RIGHT", ImVec2(width, height))) {
		auto pos = camera->obj->tryGetTransform()->getLocalPosition();
		pos.x -= 0.3f;
		camera->obj->tryGetTransform()->setLocalPosition(pos);
	}

	if (ImGui::Button("R LEFT", ImVec2(width, height))) {
		MATH::QuaternionF addRot = MATH::QuaternionF(MATH::Vector3f(0.0, 10.0, 0.0));
		camera->obj->tryGetTransform()->rotateLocal(addRot);
	}
	ImGui::SameLine();
	if (ImGui::Button("R RIGHT", ImVec2(width, height))) {
		MATH::QuaternionF addRot = MATH::QuaternionF(MATH::Vector3f(0.0, -10.0, 0.0));
		camera->obj->tryGetTransform()->rotateLocal(addRot);
	}

	if (ImGui::Button("R UP", ImVec2(width, height))) {
		MATH::QuaternionF addRot = MATH::QuaternionF(MATH::Vector3f(-10.0, 0.0, 0.0));
		camera->obj->tryGetTransform()->rotateLocal(addRot);
	}
	ImGui::SameLine();
	if (ImGui::Button("R DOWN", ImVec2(width, height))) {
		MATH::QuaternionF addRot = MATH::QuaternionF(MATH::Vector3f(10.0, 0.0, 0.0));
		camera->obj->tryGetTransform()->rotateLocal(addRot);
	}

#endif
	ImGui::End();
#endif
}
#endif
