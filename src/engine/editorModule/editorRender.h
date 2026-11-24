#pragma once
#include "fileBrowser.h"
#ifdef USE_EDITOR
#include "coreModule/core/editorFrontend.h"
#include <functional>
#include <map>

#include "commandHistory.h"
#include "editorWindow.h"
#include <memory>
#include <string>

namespace IKIGAI {
namespace RENDER {
	class TextureInterface;
	class MaterialInterface;
}
}

namespace IKIGAI {
namespace ECS {
	class Object;
}
}

namespace IKIGAI::EDITOR {
class EditorRender : public CORE::EditorFrontend {
public:
	struct GlobalState {
		std::shared_ptr<ECS::Object> mSelectObject;
		bool mSceneEditing = false;
		std::shared_ptr<RENDER::MaterialInterface> mSelectMaterial;

		std::string mDndStringPayload;

		std::map<std::string, bool> mPopupStates;
		std::function<void(std::string)> mFileChooserCb;
		std::function<std::string()> mFileFormatsCb;

		std::unordered_map<std::string, std::shared_ptr<RENDER::TextureInterface>> mTextureCache;

		std::string mResPath;
		File::FileType mResType;
		CommandHistory mCommandHistory;
	};
	inline static GlobalState GlobalState;

	struct Internal;

	EditorRender();

	std::unique_ptr<Internal> mData;

	~EditorRender() override;
	void draw() override;
	void openSurfaceBrushes();
	void drawPopup();
};
}
#endif
