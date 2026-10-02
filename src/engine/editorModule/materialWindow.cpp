#include "materialWindow.h"

#include "utilsModule/log/loggerDefine.h"
#ifdef USE_EDITOR
#include "IconsFontAwesome5.h"
#include "editorRender.h"
#include "imgui.h"
#include "renderModule/backends/gl/materialGl.h"
#include "resourceModule/materialManager.h"

#include "misc/cpp/imgui_stdlib.h"
#include "resourceModule/textureManager.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/imguiHelper/imguiWidgets.h"

bool isEngineUniform(const std::string& uniformName) {
	return uniformName.rfind("EngineUBO", 0) == 0 ||
		uniformName.rfind("engine_", 0) == 0 ||
		uniformName.rfind("Engine", 0) == 0;
}
#ifdef OPENGL_BACKEND
void IKIGAI::EDITOR::MaterialWindow::draw() {

	auto material = std::static_pointer_cast<RENDER::MaterialGl>(
		EditorRender::GlobalState.mSelectMaterial);
	ImGui::Begin("Material Editor");

	if (ImGui::Button("Save")) {
		auto d = material->getDescriptor();
		auto dataRes = UTILS::ToJson(d);
		if (dataRes.isErr()) {
			LOG_ERROR << "Can not save: " << material->getPath();
		} else {
			std::ofstream f(UTILS::GetRealPath(material->getPath()));
			f << dataRes.unwrap().dump(4) << std::endl;
			f.close();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload")) {
		auto path = material->getPath();
		// editMaterial = RESOURCES::MaterialLoader::Create(path);
		EditorRender::GlobalState.mSelectMaterial =
			RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>()
			.loadResource(path);
	}
	// ImGui::SameLine();
	// if (ImGui::Button("Create new")) {
	//	//TODO:
	//	//popupStates["create_new_material"] = true;
	// }

	ImGui::Text("Name: %s", material->getPath().c_str());

	static bool isMatSettingsOpen = true;
	if (ImGui::CollapsingHeader("Material settings", &isMatSettingsOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
		bool valBool{};
		int valInt{};

		valBool = material->isBlendable();
		if (ImGui::Checkbox("Is Blendable", &valBool)) {
			material->setBlendable(valBool);
		}
		valBool = material->isBackfaceCulling();
		if (ImGui::Checkbox("Back face culling", &valBool)) {
			material->setBackfaceCulling(valBool);
		}
		valBool = material->isFrontFaceCulling();
		if (ImGui::Checkbox("Front face culling", &valBool)) {
			material->setFrontfaceCulling(valBool);
		}
		valBool = material->isDepthTest();
		if (ImGui::Checkbox("Depth test", &valBool)) {
			material->setDepthTest(valBool);
		}
		valBool = material->isColorWriting();
		if (ImGui::Checkbox("Color writing", &valBool)) {
			material->setColorWriting(valBool);
		}
		valInt = material->getGPUInstances();
		if (ImGui::DragInt("GPU Instance", &valInt)) {
			material->setGpuInstances(valInt);
		}
		//material->generateStateMask();
	}

	std::string shaderVPath = material->mShader->mShaderPaths.at(RENDER::ShaderType::VERTEX);
	std::string shaderFPath = material->mShader->mShaderPaths.at(RENDER::ShaderType::FRAGMENT);
	if (ImGui::InputText("Shader Fragment:", &shaderVPath,
		ImGuiInputTextFlags_EnterReturnsTrue)) {
	}
	if (ImGui::InputText("Shader Vertex:", &shaderFPath,
		ImGuiInputTextFlags_EnterReturnsTrue)) {
	}

	auto shader = material->getShader();
	if (!shader) {
		ImGui::End();
		return;
	}
	const auto& refl = shader->getReflection();

	for (auto& data : refl.mUniforms) {
		auto& name = data.mName;
		if (isEngineUniform(name)) {
			continue;
		}
		if (data.mType == RENDER::ShaderReflection::UniformType::SAMPLER_2D) {
			//switch (data.mMembers[0].type) {
			//case RENDER::UNIFORM_TYPE::MAT4: break;
			//case RENDER::UNIFORM_TYPE::MAT3: break;
			//case RENDER::UNIFORM_TYPE::VEC4: {
			//	if (!IMGUI::CombineVecEdit::Data.count(name)) {
			//		IMGUI::CombineVecEdit::Data.insert({name,
			//			IMGUI::CombineVecEdit(name, 4)});
			//	}
			//	auto val =
			//		std::get<MATH::Vector4f>(material->get(name)); if
			//		(IMGUI::CombineVecEdit::Data.at(name).draw(val)) {
			//		material->set(name, val);
			//	}
			//	break;
			//}
			//case RENDER::UNIFORM_TYPE::VEC3: {
			//	if (!IMGUI::CombineVecEdit::Data.count(name)) {
			//		IMGUI::CombineVecEdit::Data.insert({name,
			//			IMGUI::CombineVecEdit(name, 3)});
			//	}
			//	auto val =
			//		std::get<MATH::Vector3f>(material->get(name)); if
			//		(IMGUI::CombineVecEdit::Data.at(name).draw(val)) {
			//		material->set(name, val);
			//	}
			//	break;
			//}
			//case RENDER::UNIFORM_TYPE::VEC2: {
			//	if (!IMGUI::CombineVecEdit::Data.count(name)) {
			//		IMGUI::CombineVecEdit::Data.insert({name,
			//			IMGUI::CombineVecEdit(name, 2)});
			//	}
			//	auto val =
			//		std::get<MATH::Vector2f>(material->get(name)); if
			//		(IMGUI::CombineVecEdit::Data.at(name).draw(val)) {
			//		material->set(name, val);
			//	}
			//	break;
			//}
			//case RENDER::UNIFORM_TYPE::INT: {
			//	if (!IMGUI::ScalarEdit::Data.count(name)) {
			//		IMGUI::ScalarEdit::Data.insert({name,
			//			IMGUI::ScalarEdit(name, IMGUI::ScalarEdit::TYPE::INT)});
			//	}
			//	auto val =
			//		std::get<int>(material->getUniforms().at(name)); if
			//		(IMGUI::ScalarEdit::Data.at(name).draw(val)) {
			//		material->set(name, val);
			//	}
			//	break;
			//}
			//case RENDER::UNIFORM_TYPE::FLOAT: {
			//	if (!IMGUI::ScalarEdit::Data.count(name)) {
			//		IMGUI::ScalarEdit::Data.insert({name,
			//			IMGUI::ScalarEdit(name, IMGUI::ScalarEdit::TYPE::FLOAT)});
			//	}
			//	auto val =
			//		std::get<float>(material->getUniforms().at(name)); if
			//		(IMGUI::ScalarEdit::Data.at(name).draw(val)) {
			//		material->set(name, val);
			//	}
			//	break;
			//}
			//case RENDER::UNIFORM_TYPE::BOOL: {
			//	if (!IMGUI::ScalarEdit::Data.count(name)) {
			//		IMGUI::ScalarEdit::Data.insert({name,
			//			IMGUI::ScalarEdit(name, IMGUI::ScalarEdit::TYPE::BOOL)});
			//	}
			//	auto val =
			//		std::get<bool>(material->getUniforms().at(name)); if
			//		(IMGUI::ScalarEdit::Data.at(name).draw(val)) {
			//		material->set(name, val);
			//	}
			//	break;
			//}
			//case RENDER::UNIFORM_TYPE::SAMPLER_2D: {
			//	auto val =
			//		std::get<std::shared_ptr<RENDER::TextureInterface>>(material->getUniforms().at(name));
			//	ImGui::PushID(("##" + name).c_str());
			//
			//	//auto size =
			//	RESOURCES::ServiceManager::Get<WINDOW_SYSTEM::Window>().getSize(); if
			//		(ImGui::Selectable(("##" + name).c_str(), false, 0, ImVec2(100, 100))) {
			//
			//	}
			//	auto pos = ImGui::GetCursorPos();
			//	ImGui::SetCursorPos(ImVec2(pos.x, pos.y - 100));
			//
			//	if (ImGui::BeginDragDropTarget()) {
			//		ImGuiDragDropFlags target_flags = 0;
			//		//target_flags |=
			//		ImGuiDragDropFlags_AcceptBeforeDelivery;    // Don't wait until the delivery
			//		(release mouse button on a target) to do something
			//			//target_flags |=
			//			ImGuiDragDropFlags_AcceptNoDrawDefaultRect; // Don't display the yellow
			//		rectangle if (const ImGuiPayload* payload =
			//			ImGui::AcceptDragDropPayload("DND_IMAGE_DATA", target_flags)) {
			//			auto path =
			//				EditorRender::GlobalState.mDndStringPayload; material->set(name,
			//				std::static_pointer_cast<RENDER::TextureGl>(RESOURCES::ServiceManager::Get<RESOURCES::TextureLoader>().createFromFile(path,
			//				true))); EditorRender::GlobalState.mDndStringPayload.clear();
			//		}
			//		ImGui::EndDragDropTarget();
			//	}
			//	//if (val) {
			//	//
			//	ImGui::Image(reinterpret_cast<ImTextureID>((uintptr_t)val->id), ImVec2(100,
			//		100), ImVec2(0, 1), ImVec2(1, 0));
			//	//} else {
			//	//
			//	ImGui::Image(EditorRender::GlobalState.mTextureCache["default_texture"]->getImguiId(),
			//		ImVec2(100, 100), ImVec2(0, 1), ImVec2(1, 0));
			//	//}
			//	ImGui::SameLine();
			//	if (ImGui::Button(ICON_FA_FILE)) {
			//		EditorRender::GlobalState.mPopupStates["file_chooser"]
			//			= true; EditorRender::GlobalState.mFileFormatsCb = [] { return
			//			".png,.jpeg,.jpg";
			//			};
			//		EditorRender::GlobalState.mFileChooserCb =
			//			[material, name](std::string path) mutable { material->set(name,
			//			std::static_pointer_cast<RENDER::TextureGl>(RESOURCES::ServiceManager::Get<RESOURCES::TextureLoader>().createFromFile(path,
			//			true)));
			//			};
			//	}
			//	ImGui::PopID();
			//	//ImGui::GetWindowDrawList()->AddImage(
			//	//	(void*)val->getId(),
			//	//	ImVec2(ImGui::GetCursorScreenPos()),
			//	//	ImVec2(ImGui::GetCursorScreenPos().x + size.x /
			//	2,
			//		//		ImGui::GetCursorScreenPos().y + size.y /
			//		2), ImVec2(0, 1), ImVec2(1, 0)); break;
			//}
			//case RENDER::UNIFORM_TYPE::SAMPLER_3D: break;
			//case RENDER::UNIFORM_TYPE::SAMPLER_CUBE: break;
			//default: break;
			//}
		} else if (data.mType == RENDER::ShaderReflection::UniformType::UNIFORM_BUFFER) {
			for (const auto& mamber : data.mMembers) {
				auto _name = mamber.mName;
				switch (mamber.mType) {
				case RENDER::ShaderReflection::UniformType::MAT4: break;
				case RENDER::ShaderReflection::UniformType::MAT3: break;
				case RENDER::ShaderReflection::UniformType::VEC4: {
					if (!IMGUI::CombineVecEdit::Data.count(_name)) {
						IMGUI::CombineVecEdit::Data.insert({_name, IMGUI::CombineVecEdit(_name,
							4)});
					}
					auto val =
						std::get<MATH::Vector4f>(material->get(name + "." + _name)); if
						(IMGUI::CombineVecEdit::Data.at(_name).draw(val)) {
						material->set(name + "." + _name,
							val);
					}
					break;
				}
				case RENDER::ShaderReflection::UniformType::VEC3: {
					if (!IMGUI::CombineVecEdit::Data.count(_name)) {
						IMGUI::CombineVecEdit::Data.insert({_name, IMGUI::CombineVecEdit(_name,
							3)});
					}
					auto val =
						std::get<MATH::Vector3f>(material->get(name + "." + _name)); if
						(IMGUI::CombineVecEdit::Data.at(_name).draw(val)) {
						material->set(name + "." +
							_name, val);
					}
					break;
				}
				case RENDER::ShaderReflection::UniformType::VEC2: {
					if (!IMGUI::CombineVecEdit::Data.count(_name)) {
						IMGUI::CombineVecEdit::Data.insert({_name, IMGUI::CombineVecEdit(_name,
							2)});
					}
					auto val =
						std::get<MATH::Vector2f>(material->get(name + "." + _name)); if
						(IMGUI::CombineVecEdit::Data.at(_name).draw(val)) {
						material->set(name + "." +
							_name, val);
					}
					break;
				}
				case RENDER::ShaderReflection::UniformType::INT: {
					if (!IMGUI::ScalarEdit::Data.count(name)) {
						IMGUI::ScalarEdit::Data.insert({name,
							IMGUI::ScalarEdit(name, IMGUI::ScalarEdit::TYPE::INT)});
					}
					auto val =
						std::get<int>(material->getUniforms().at(name)); if
						(IMGUI::ScalarEdit::Data.at(name).draw(val)) {
						material->set(name, val);
					}
					break;
				}
				case RENDER::ShaderReflection::UniformType::FLOAT: {
					if (!IMGUI::ScalarEdit::Data.count(name)) {
						IMGUI::ScalarEdit::Data.insert({name,
							IMGUI::ScalarEdit(name, IMGUI::ScalarEdit::TYPE::FLOAT)});
					}
					auto val =
						std::get<float>(material->getUniforms().at(name)); if
						(IMGUI::ScalarEdit::Data.at(name).draw(val)) {
						material->set(name, val);
					}
					break;
				}
				case RENDER::ShaderReflection::UniformType::BOOL: {
					if (!IMGUI::ScalarEdit::Data.count(name)) {
						IMGUI::ScalarEdit::Data.insert({name,
							IMGUI::ScalarEdit(name, IMGUI::ScalarEdit::TYPE::BOOL)});
					}
					auto val =
						std::get<bool>(material->getUniforms().at(name)); if
						(IMGUI::ScalarEdit::Data.at(name).draw(val)) {
						material->set(name, val);
					}
					break;
				}
				case RENDER::ShaderReflection::UniformType::SAMPLER_2D: break;
				case RENDER::ShaderReflection::UniformType::SAMPLER_3D: break;
				case RENDER::ShaderReflection::UniformType::SAMPLER_CUBE: break;
				default: break;
				}
			}
		}
	}
	ImGui::End();
}
#endif
#endif
