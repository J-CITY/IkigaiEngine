#pragma once
#include "renderModule/backends/interface/resourceStruct.h"
#include "renderModule/backends/interface/uniformTypes.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/imguiHelper/imguiWidgets.h"
#include "utilsModule/log/loggerDefine.h"
#ifdef USE_EDITOR
#include "editorWindow.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <serdepp/serde.hpp>
#include <serdepp/adaptor/reflection.hpp>

namespace IKIGAI::EDITOR {
	template<class T, class = void>
	struct has_get_members : std::false_type {};

	template<class T>
	struct has_get_members<T, std::void_t<decltype(T::GetMembers())>> : std::true_type {};

	template<class T>
	struct is_vector_of_get_members : std::false_type {};

	template<class T, class A>
	struct is_vector_of_get_members<std::vector<T, A>> : has_get_members<T> {};

	template<class T, typename std::enable_if<
		!std::is_enum<T>::value &&
		!has_get_members<T>::value &&
		!is_vector_of_get_members<T>::value
	>::type* = nullptr>
	inline bool drawMember(const std::string& name, T& data) {
		LOG_INFO << "Not support: " << name;
		return false;
	}

	template<class ENUM, typename std::enable_if<std::is_enum<ENUM>::value>::type* = nullptr>
	inline bool drawMember(const std::string& name, ENUM& data) {
		//TODO: support my enums
		
		constexpr auto enumNames = magic_enum::enum_names<ENUM>();
		constexpr auto enumValues = magic_enum::enum_values<ENUM>();

		int itemCurrentIndex = [&enumValues, &data]() {
			int i = 0;
			for (auto& e : enumValues) {
				if (e == data) {
					break;
				}
				i++;
			}
			return i;
		}();
		bool res = false;
		const auto comboLabel = enumNames[itemCurrentIndex];
		if (ImGui::BeginCombo(name.c_str(), comboLabel.data())) {
			for (int n = 0; n < enumNames.size(); n++) {
				const bool isSelected = (itemCurrentIndex == n);
				if (ImGui::Selectable(enumNames[n].data(), isSelected)) {
					itemCurrentIndex = n;
					res = true;
					data = enumValues[itemCurrentIndex];
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		return res;
	}

	template<class ENUM, typename std::enable_if<std::is_enum<ENUM>::value>::type* = nullptr>
	inline bool drawMember(const std::string& name, std::vector<ENUM>& data) {
		//TODO: change to my enums

		constexpr auto enumNames = magic_enum::enum_names<ENUM>();
		constexpr auto enumValues = magic_enum::enum_values<ENUM>();

		bool res = false;
		ImGui::Text(name.c_str());
		int i = 0;

		ImGui::PushID(name.c_str());
		for (int n = 0; n < enumNames.size(); n++) {
			bool b = std::find(data.begin(), data.end(), enumValues[i]) != data.end();
			if (ImGui::Checkbox(enumNames[n].data(), &b)) {
				if (b) {
					data.push_back(enumValues[i]);
					res = true;
				}
				else  {
					std::erase_if(data, [val= enumValues[i]](auto& e) { return e == val; });
					res = true;
				}
			}
			i++;
		}
		ImGui::PopID();
		return res;
	}

	template<>
	inline bool drawMember<int>(const std::string& name, int& data) {
		return ImGui::DragInt(name.c_str(), &data);
	}

	// helper type for the visitor
	template<class... Ts>
	struct overloaded : Ts... { using Ts::operator()...; };
	// explicit deduction guide (not needed as of C++20)
	template<class... Ts>
	overloaded(Ts...) -> overloaded<Ts...>;
	template<>
	inline bool drawMember<std::map<std::string, RENDER::MaterialResource::UniformType>>(const std::string& name, std::map<std::string, RENDER::MaterialResource::UniformType>& data) {
		ImGui::Text(name.c_str());
		bool res = false;
		int i = 0;
		std::string delItem;
		ImGui::PushID(name.c_str());
		for (auto& [key, val] : data) {
			std::visit(overloaded{
			[&key](std::string& arg) {
				ImGui::InputText(key.c_str(), &arg);
			},
			[&key](int& arg) {
				ImGui::DragInt(key.c_str(), &arg);
			},
			[&key](float& arg) {
				ImGui::DragFloat(key.c_str(), &arg);
			},
			[&key](bool& arg) {
				ImGui::Checkbox(key.c_str(), &arg);
			},
			[&key](MATH::Vector2f& arg) {
				if (!IMGUI::CombineVecEdit::Data.count(key)) {
					IMGUI::CombineVecEdit::Data.insert({key, IMGUI::CombineVecEdit(key, 2)});
				}
				if (IMGUI::CombineVecEdit::Data.at(key).draw(arg)) {}
			},
			[&key](MATH::Vector3f& arg) {
				if (!IMGUI::CombineVecEdit::Data.count(key)) {
					IMGUI::CombineVecEdit::Data.insert({key, IMGUI::CombineVecEdit(key, 3)});
				}
				if (IMGUI::CombineVecEdit::Data.at(key).draw(arg)) {}
			},
			[&key](MATH::Vector4f& arg) {
				if (!IMGUI::CombineVecEdit::Data.count(key)) {
					IMGUI::CombineVecEdit::Data.insert({key, IMGUI::CombineVecEdit(key, 4)});
				}
				if (IMGUI::CombineVecEdit::Data.at(key).draw(arg)) {}
			},
			[](auto& arg) {
				
			}}, val);

			ImGui::SameLine();
			if (ImGui::Button("X")) {
				delItem = key;
				res = true;
			}
			i++;
		}
		if (!delItem.empty()) {
			data.erase(delItem);
		}


		if (ImGui::Button("Add")) {
			//data.push_back(0);
		}
		ImGui::PopID();
		return res;
	}

	template<>
	inline bool drawMember<std::vector<uint8_t>>(const std::string& name, std::vector<uint8_t>& data) {
		ImGui::Text(name.c_str());
		bool res = false;
		int i = 0;
		int delItem = -1;
		ImGui::PushID(name.c_str());
		for (auto& e : data) {
			const int m = 255;
			if (ImGui::DragScalar(std::to_string(i + 1).c_str(), ImGuiDataType_U8, &e, 1, 0, &m)) {
				res = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("X")) {
				delItem = i;
				res = true;
			}
			i++;
		}

		if (delItem >= 0) {
			data.erase(data.begin() + i);
		}
		if (ImGui::Button("Add")) {
			data.push_back(0);
		}
		ImGui::PopID();
		return res;
	}

	template<>
	inline bool drawMember<std::vector<std::string>>(const std::string& name, std::vector<std::string>& data) {
		ImGui::Text(name.c_str());
		bool res = false;
		int i = 0;
		int delItem = -1;
		ImGui::PushID(name.c_str());
		for (auto& e : data) {
			if (ImGui::InputText(std::to_string(i+1).c_str(), &e)) {
				res = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("X")) {
				delItem = i;
				res = true;
			}
			i++;
		}
		if (delItem >= 0) {
			data.erase(data.begin() + i);
		}
		if (ImGui::Button("Add")) {
			data.push_back("");
		}
		ImGui::PopID();
		return res;
	}

	template<>
	inline bool drawMember<float>(const std::string& name, float& data) {
		return ImGui::DragFloat(name.c_str(), &data);
	}

	template<>
	inline bool drawMember<std::string>(const std::string& name, std::string& data) {
		return ImGui::InputText(name.c_str(), &data);
	}

	template<>
	inline bool drawMember<bool>(const std::string& name, bool& data) {
		return ImGui::Checkbox(name.c_str(), &data);
	}

	template<class R, int N>
	struct MemberDrawer {
		void run(R& res);
	};

	template<class T>
	inline void drawReflectedObject(const std::string& name, T& data) {
		if (ImGui::TreeNode(name.c_str())) {
			auto members = T::GetMembers();
			MemberDrawer<T, static_cast<int>(std::tuple_size<decltype(members)>::value) - 1>().run(data);
			ImGui::TreePop();
		}
	}

	template<class T>
	inline void drawReflectedVector(const std::string& name, std::vector<T>& data) {
		ImGui::Text("%s", name.c_str());
		ImGui::PushID(name.c_str());
		int delItem = -1;
		int i = 0;
		for (auto& e : data) {
			ImGui::PushID(i);
			if (ImGui::TreeNode("item", "[%d]", i)) {
				auto members = T::GetMembers();
				MemberDrawer<T, static_cast<int>(std::tuple_size<decltype(members)>::value) - 1>().run(e);
				ImGui::TreePop();
			}
			ImGui::SameLine();
			if (ImGui::Button("X")) {
				delItem = i;
			}
			ImGui::PopID();
			++i;
		}
		if (delItem >= 0) {
			data.erase(data.begin() + delItem);
		}
		if (ImGui::Button("Add")) {
			data.push_back(T{});
		}
		ImGui::PopID();
	}

	template<class R, int N>
	void MemberDrawer<R, N>::run(R& res) {
		auto members = R::GetMembers();

		auto& m = std::get<N>(members);
		auto& name = m.getName();
		auto data = m.get(res);
		using DataT = std::decay_t<decltype(data)>;
		if constexpr (is_vector_of_get_members<DataT>::value) {
			drawReflectedVector(name, data);
			m.set(res, data);
		}
		else if constexpr (has_get_members<DataT>::value) {
			drawReflectedObject(name, data);
			m.set(res, data);
		}
		else if (drawMember(name, data)) {
			m.set(res, data);
		}

		if constexpr (N > 0) {
			MemberDrawer<R, N - 1>().run(res);
		}
	}

	template<class T>
	class ResourceEditorWindow : public EditorWindow {
		T mResourceData;
		const std::string mWindowName;
		const std::string mPath;
	public:
		ResourceEditorWindow(const std::string& path, T data): mResourceData(std::move(data)), mWindowName("Resource: " + path), mPath(path) {}

		virtual ~ResourceEditorWindow() override = default;

		void draw() override {
			ImGui::Begin(mWindowName.c_str(),&mIsOpen);
			ImGui::Text(mPath.c_str());
			auto members = T::GetMembers();
			MemberDrawer<T, std::tuple_size<decltype(members)>::value-1>().run(mResourceData);
			if (ImGui::Button("Save")) {
				auto jsonStrRes = UTILS::ToJsonStr(mResourceData);
				if (jsonStrRes.isErr()) {
					LOG_ERROR << "SceneManager::saveToFile Can not save: " << mPath;
					return;
				}
				auto jsonStr = jsonStrRes.unwrap();
				//write
				//std::ofstream f(UTILS::GetRealPath(mPath));
				//f << jsonStr << std::endl;
				//f.close();
			}
			ImGui::End();
		}
	};
}
#endif
