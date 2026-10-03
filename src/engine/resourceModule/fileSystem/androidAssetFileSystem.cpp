#ifdef OCULUS
#include "androidAssetFileSystem.h"

#include <android_native_app_glue.h>
#include <jni.h>
#include <functional>

namespace IKIGAI::RESOURCES {

	AndroidAssetFile::AndroidAssetFile(const vfspp::EntryInfo& fileInfo, AAssetManager* assets)
		: m_fileInfo(fileInfo), m_assets(assets) {}

	AndroidAssetFile::~AndroidAssetFile() {
		AndroidAssetFile::Close();
	}

	const vfspp::EntryInfo& AndroidAssetFile::GetEntryInfo() const {
		return m_fileInfo;
	}

	uint64_t AndroidAssetFile::Size() const {
		return m_asset ? static_cast<uint64_t>(AAsset_getLength(m_asset)) : 0;
	}

	bool AndroidAssetFile::IsReadOnly() const {
		return true;
	}

	bool AndroidAssetFile::Open(FileMode) {
		Close();
		if (!m_assets) {
			return false;
		}
		m_asset = AAssetManager_open(m_assets, m_fileInfo.NativePath().c_str(), AASSET_MODE_RANDOM);
		m_pos = 0;
		return m_asset != nullptr;
	}

	void AndroidAssetFile::Close() {
		if (m_asset) {
			AAsset_close(m_asset);
			m_asset = nullptr;
		}
		m_pos = 0;
	}

	bool AndroidAssetFile::IsOpened() const {
		return m_asset != nullptr;
	}

	uint64_t AndroidAssetFile::Seek(uint64_t offset, Origin origin) {
		if (!m_asset) {
			return 0;
		}
		int whence = SEEK_SET;
		if (origin == Origin::Set) {
			whence = SEEK_CUR;
		} else if (origin == Origin::End) {
			whence = SEEK_END;
		}
		const off_t pos = AAsset_seek(m_asset, static_cast<off_t>(offset), whence);
		m_pos = pos >= 0 ? static_cast<uint64_t>(pos) : 0;
		return m_pos;
	}

	uint64_t AndroidAssetFile::Tell() const {
		return m_pos;
	}

	uint64_t AndroidAssetFile::Read(std::span<uint8_t> buffer) {
		if (!m_asset || buffer.empty()) {
			return 0;
		}
		const int read = AAsset_read(m_asset, buffer.data(), buffer.size());
		if (read > 0) {
			m_pos += static_cast<uint64_t>(read);
			return static_cast<uint64_t>(read);
		}
		return 0;
	}

	uint64_t AndroidAssetFile::Read(std::vector<uint8_t>& buffer, uint64_t size) {
		buffer.resize(static_cast<size_t>(size));
		return Read(std::span<uint8_t>(buffer.data(), buffer.size()));
	}

	uint64_t AndroidAssetFile::Write(std::span<const uint8_t>) {
		return 0;
	}

	uint64_t AndroidAssetFile::Write(const std::vector<uint8_t>&) {
		return 0;
	}

	AndroidAssetFileSystem::AndroidAssetFileSystem(const std::string& aliasPath, const std::string& basePath,
		AAssetManager* assets, void* androidApp)
		: m_aliasPath(aliasPath), m_basePath(basePath), m_assets(assets), m_androidApp(androidApp) {}

	AndroidAssetFileSystem::~AndroidAssetFileSystem() {
		Shutdown();
	}

	bool AndroidAssetFileSystem::Initialize() {
		m_isInitialized = true;
		m_fileList.clear();
		collectEntries();
		return true;
	}

	void AndroidAssetFileSystem::collectEntries() {
		auto* app = static_cast<android_app*>(m_androidApp);
		if (!app || !app->activity || !m_assets) {
			return;
		}

		JavaVM* vm = app->activity->vm;
		JNIEnv* env = nullptr;
		bool detach = false;
		if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
			if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
				return;
			}
			detach = true;
		}

		jobject activity = app->activity->clazz;
		jclass activityClass = env->GetObjectClass(activity);
		jmethodID getAssets = env->GetMethodID(activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
		jobject assetManager = getAssets ? env->CallObjectMethod(activity, getAssets) : nullptr;
		jclass assetClass = assetManager ? env->GetObjectClass(assetManager) : nullptr;
		jmethodID listMethod = assetClass ? env->GetMethodID(assetClass, "list", "(Ljava/lang/String;)[Ljava/lang/String;") : nullptr;
		if (env->ExceptionCheck()) {
			env->ExceptionClear();
		}
		if (!listMethod) {
			if (assetClass) env->DeleteLocalRef(assetClass);
			if (assetManager) env->DeleteLocalRef(assetManager);
			if (activityClass) env->DeleteLocalRef(activityClass);
			if (detach) vm->DetachCurrentThread();
			return;
		}

		const std::string rootVirtual = m_aliasPath.empty() ? std::string("/") : m_aliasPath;
		std::function<void(const std::string&, const std::string&)> scan =
			[&](const std::string& assetDir, const std::string& virtualDir) {
				jstring jPath = env->NewStringUTF(assetDir.c_str());
				auto names = static_cast<jobjectArray>(env->CallObjectMethod(assetManager, listMethod, jPath));
				env->DeleteLocalRef(jPath);
				if (env->ExceptionCheck()) {
					env->ExceptionClear();
					if (names) env->DeleteLocalRef(names);
					return;
				}
				if (!names) {
					return;
				}
				const jsize count = env->GetArrayLength(names);
				for (jsize i = 0; i < count; ++i) {
					auto jName = static_cast<jstring>(env->GetObjectArrayElement(names, i));
					const char* nameChars = jName ? env->GetStringUTFChars(jName, nullptr) : nullptr;
					const std::string name = nameChars ? nameChars : "";
					if (nameChars) env->ReleaseStringUTFChars(jName, nameChars);
					if (jName) env->DeleteLocalRef(jName);
					if (name.empty() || name == "." || name == "..") {
						continue;
					}
					const std::string childAsset = assetDir.empty() ? name : assetDir + "/" + name;
					const std::string childVirtual = (virtualDir == "/" ? std::string("/") : virtualDir + "/") + name;
					AAsset* asset = AAssetManager_open(m_assets, childAsset.c_str(), AASSET_MODE_UNKNOWN);
					const bool isFile = asset != nullptr;
					if (asset) {
						AAsset_close(asset);
					}
					m_fileList.emplace_back(m_aliasPath, m_basePath, childVirtual,
						isFile ? vfspp::EntryType::File : vfspp::EntryType::Directory);
					if (!isFile) {
						scan(childAsset, childVirtual);
					}
				}
				env->DeleteLocalRef(names);
			};
		scan(m_basePath, rootVirtual);

		env->DeleteLocalRef(assetClass);
		env->DeleteLocalRef(assetManager);
		env->DeleteLocalRef(activityClass);
		if (detach) {
			vm->DetachCurrentThread();
		}
	}

	void AndroidAssetFileSystem::Shutdown() {
		m_isInitialized = false;
		m_fileList.clear();
	}

	bool AndroidAssetFileSystem::IsInitialized() const {
		return m_isInitialized;
	}

	const std::string& AndroidAssetFileSystem::BasePath() const {
		return m_basePath;
	}

	const std::string& AndroidAssetFileSystem::VirtualPath() const {
		return m_aliasPath;
	}

	vfspp::IFileSystem::EntriesList AndroidAssetFileSystem::GetEntriesList(bool excludeDirectories) const {
		if (!excludeDirectories) {
			return m_fileList;
		}
		EntriesList files;
		for (const auto& entry : m_fileList) {
			if (!entry.IsDirectory()) {
				files.push_back(entry);
			}
		}
		return files;
	}

	bool AndroidAssetFileSystem::IsReadOnly() const {
		return true;
	}

	std::optional<vfspp::EntryInfo> AndroidAssetFileSystem::GetEntryInfo(const std::string& virtualPath) const {
		return vfspp::EntryInfo(m_aliasPath, m_basePath, virtualPath);
	}

	vfspp::IFilePtr AndroidAssetFileSystem::OpenFile(const std::string& virtualPath, vfspp::IFile::FileMode mode) {
		auto file = std::make_shared<AndroidAssetFile>(GetEntryInfo(virtualPath).value(), m_assets);
		if (!file->Open(mode)) {
			return nullptr;
		}
		return file;
	}

	void AndroidAssetFileSystem::CloseFile(vfspp::IFilePtr file) {
		if (file) {
			file->Close();
		}
	}

	vfspp::IFilePtr AndroidAssetFileSystem::CreateFile(const std::string&) { return nullptr; }
	bool AndroidAssetFileSystem::RemoveFile(const std::string&) { return false; }
	bool AndroidAssetFileSystem::CopyFile(const std::string&, const std::string&, bool) { return false; }
	bool AndroidAssetFileSystem::RenameFile(const std::string&, const std::string&) { return false; }
	bool AndroidAssetFileSystem::MakeDirectory(const std::string&) { return false; }
	bool AndroidAssetFileSystem::DeleteDirectory(const std::string&, bool) { return false; }
	bool AndroidAssetFileSystem::RenameDirectory(const std::string&, const std::string&) { return false; }

	bool AndroidAssetFileSystem::IsFileExists(const std::string& virtualPath) const {
		if (!m_assets) {
			return false;
		}
		AAsset* asset = AAssetManager_open(m_assets, GetEntryInfo(virtualPath).value().NativePath().c_str(), AASSET_MODE_UNKNOWN);
		if (asset) {
			AAsset_close(asset);
			return true;
		}
		return false;
	}

	bool AndroidAssetFileSystem::IsDirectoryExists(const std::string& virtualPath) const {
		if (virtualPath.empty() || virtualPath == "/" || virtualPath == m_aliasPath) {
			return true;
		}
		for (const auto& entry : m_fileList) {
			if (entry.IsDirectory() && entry.VirtualPath() == virtualPath) {
				return true;
			}
		}
		return false;
	}

}
#endif
