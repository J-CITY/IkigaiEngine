#pragma once
#include "editorWindow.h"
#ifdef USE_EDITOR
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <list>
#include <ostream>
namespace IKIGAI
{
	namespace RENDER
	{
		class TextureInterface;
	}
}

namespace IKIGAI::EDITOR {

	struct File {
		enum class FileType {
			DIR,
			IMAGE,
			MODEL,
			TEXT,
			FONT,
			MATERIAL,
			TEXTURE_RES,
			SHADER_RES,
			AUDIO_RES,
			MODEL_RES,
		};
		FileType type;
		std::filesystem::path path;
		std::filesystem::path file;
		std::string ext;
		std::list<File> files;

		int uid = 0;
		File(std::filesystem::path path);

	private:
		inline static std::set<std::string> imageExt = {".png", ".jpg", ".jpeg", ".tga", ".dds"};
		inline static std::set<std::string> objExt = {".obj", ".fbx", ".dae"};
		inline static std::set<std::string> fontExt = {".ttf"};
		inline static std::set<std::string> materialExt = {".mat"};
		inline static std::set<std::string> textureResExt = {".texture"};
		inline static std::set<std::string> shaderResExt = {".shader"};
		inline static std::set<std::string> audioResExt = {".sound"};
		inline static std::set<std::string> modelResExt = {".model"};
	public:
		static FileType GetFileType(const std::filesystem::path& path);
		static std::string GetExtension(const std::filesystem::path& path);
	};
	inline std::ostream& operator<<(std::ostream& os, File::FileType type) {
		return os << static_cast<int>(type);
	}

	class FileBrowserWindow: public EditorWindow{
		std::string mPath;
		std::string mSelectedFolderPath;
		std::stack<std::string> mHistory;
		int mElementSize = 100;

		File mRoot;
		std::set<int> mSearchedFileTreeIds;
		std::unordered_map<std::string, std::shared_ptr<RENDER::TextureInterface>> mTextureCache;
	public:
		FileBrowserWindow(const std::string& path);
		virtual ~FileBrowserWindow() override = default;
		void draw() override;
	private:
		void initFileTree(File& fileTree);
		void drawFolder(std::string_view path);
		void drawItem(File& file);
	};
}
#endif
