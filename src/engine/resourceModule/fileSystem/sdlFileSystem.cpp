#include "sdlFileSystem.h"
#include <algorithm>
#include <functional>
#include <vector>
#include <filesystem>
#ifdef __ANDROID__
#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <SDL3/SDL.h>
#endif

namespace IKIGAI::RESOURCES {

// --- SdlFile ---

SdlFile::SdlFile(const vfspp::EntryInfo& fileInfo, bool readOnly)
    : m_fileInfo(fileInfo), m_readOnly(readOnly)
{}

SdlFile::~SdlFile() {
    SdlFile::Close();
}

const vfspp::EntryInfo& SdlFile::GetEntryInfo() const {
    return m_fileInfo;
}

uint64_t SdlFile::Size() const {
    if (m_io) {
        const Sint64 size = SDL_GetIOSize(m_io);
        return size > 0 ? static_cast<uint64_t>(size) : 0;
    }
    return 0;
}

bool SdlFile::IsReadOnly() const {
    if (m_readOnly) return true;
    std::error_code ec;
    auto perms = std::filesystem::status(m_fileInfo.NativePath(), ec).permissions();
    if (ec) {
        return true; 
    }
    return (perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none;
}

std::string SdlFile::getSdlMode(FileMode mode) const {
    const bool read = ModeHasFlag(mode, FileMode::Read);
    const bool write = ModeHasFlag(mode, FileMode::Write);
    if (!write) return "rb";
    if (ModeHasFlag(mode, FileMode::Append)) {
        return read ? "a+b" : "ab";
    }
    if (ModeHasFlag(mode, FileMode::Truncate)) {
        return read ? "w+b" : "wb";
    }
    return read ? "r+b" : "wb";
}

bool SdlFile::Open(FileMode mode) {
    if (!IsModeValid(mode) || (ModeHasFlag(mode, FileMode::Write) && IsReadOnly())) {
        return false;
    }
    if (IsOpened()) {
        Close();
    }
    m_io = SDL_IOFromFile(m_fileInfo.NativePath().c_str(), getSdlMode(mode).c_str());
    return m_io != nullptr;
}

void SdlFile::Close() {
    if (m_io) {
        SDL_CloseIO(m_io);
        m_io = nullptr;
    }
}

bool SdlFile::IsOpened() const {
    return m_io != nullptr;
}

uint64_t SdlFile::Seek(uint64_t offset, Origin origin) {
    if (!m_io) return 0;
    
    SDL_IOWhence whence = SDL_IO_SEEK_SET;
    if (origin == Origin::Begin) {
        whence = SDL_IO_SEEK_SET;
    } else if (origin == Origin::Set) {
        whence = SDL_IO_SEEK_CUR;
    } else if (origin == Origin::End) {
        whence = SDL_IO_SEEK_END;
    }

    SDL_SeekIO(m_io, static_cast<Sint64>(offset), whence);
    return Tell();
}

uint64_t SdlFile::Tell() const {
    if (!m_io) return 0;
    const Sint64 pos = SDL_TellIO(m_io);
    return pos > 0 ? static_cast<uint64_t>(pos) : 0;
}

uint64_t SdlFile::Read(std::span<uint8_t> buffer) {
    if (!m_io || buffer.empty()) return 0;
    return static_cast<uint64_t>(SDL_ReadIO(m_io, buffer.data(), buffer.size_bytes()));
}

uint64_t SdlFile::Read(std::vector<uint8_t>& buffer, uint64_t size) {
    buffer.resize(size);
    if (!m_io || size == 0) return 0;
    return static_cast<uint64_t>(SDL_ReadIO(m_io, buffer.data(), static_cast<size_t>(size)));
}

uint64_t SdlFile::Write(std::span<const uint8_t> buffer) {
    if (!m_io || buffer.empty()) return 0;
    return static_cast<uint64_t>(SDL_WriteIO(m_io, buffer.data(), buffer.size_bytes()));
}

uint64_t SdlFile::Write(const std::vector<uint8_t>& buffer) {
    if (!m_io || buffer.empty()) return 0;
    return static_cast<uint64_t>(SDL_WriteIO(m_io, buffer.data(), buffer.size()));
}


// --- SdlFileSystem ---

SdlFileSystem::SdlFileSystem(const std::string& aliasPath, const std::string& basePath, bool readOnly)
    : m_aliasPath(aliasPath), m_basePath(basePath), m_readOnly(readOnly)
{}

SdlFileSystem::~SdlFileSystem() {
    SdlFileSystem::Shutdown();
}

bool SdlFileSystem::Initialize() {
    m_isInitialized = false;
    m_fileList.clear();
    m_isInitialized = collectEntries();
    return m_isInitialized;
}

bool SdlFileSystem::collectEntries() {
#ifdef __ANDROID__
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) {
        return false;
    }

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getAssets = env->GetMethodID(activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
    jobject assetManager = getAssets ? env->CallObjectMethod(activity, getAssets) : nullptr;
    jclass assetClass = assetManager ? env->GetObjectClass(assetManager) : nullptr;
    jmethodID listMethod = assetClass ? env->GetMethodID(assetClass, "list", "(Ljava/lang/String;)[Ljava/lang/String;") : nullptr;
    AAssetManager* nativeAssets = assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr;

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    if (nativeAssets && listMethod) {
        const std::string rootVirtual = m_aliasPath.empty() ? std::string("/") : m_aliasPath;
        m_fileList.emplace_back(m_aliasPath, m_basePath, rootVirtual, vfspp::EntryType::Directory);
        std::function<void(const std::string&, const std::string&)> scan =
            [&](const std::string& assetDir, const std::string& virtualDir) {
                jstring jPath = env->NewStringUTF(assetDir.c_str());
                auto names = static_cast<jobjectArray>(env->CallObjectMethod(assetManager, listMethod, jPath));
                env->DeleteLocalRef(jPath);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    if (names) {
                        env->DeleteLocalRef(names);
                    }
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
                    if (nameChars) {
                        env->ReleaseStringUTFChars(jName, nameChars);
                    }
                    if (jName) {
                        env->DeleteLocalRef(jName);
                    }
                    if (name.empty() || name == "." || name == "..") {
                        continue;
                    }

                    const std::string childAsset = assetDir.empty() ? name : assetDir + "/" + name;
                    const std::string childVirtual = (virtualDir == "/" ? std::string("/") : virtualDir + "/") + name;
                    AAsset* asset = AAssetManager_open(nativeAssets, childAsset.c_str(), AASSET_MODE_UNKNOWN);
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
    }

    if (assetClass) {
        env->DeleteLocalRef(assetClass);
    }
    if (assetManager) {
        env->DeleteLocalRef(assetManager);
    }
    if (activityClass) {
        env->DeleteLocalRef(activityClass);
    }
    env->DeleteLocalRef(activity);
    return nativeAssets && listMethod;
#else
    const std::string rootVirtual = m_aliasPath.empty() ? std::string("/") : m_aliasPath;
    SDL_PathInfo info{};
    if (!SDL_GetPathInfo(m_basePath.c_str(), &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
        return false;
    }
    m_fileList.emplace_back(m_aliasPath, m_basePath, rootVirtual, vfspp::EntryType::Directory);
    return collectDirectory(m_basePath, rootVirtual);
#endif
}

bool SdlFileSystem::collectDirectory(const std::string& nativePath, const std::string& virtualPath) {
    struct Context {
        SdlFileSystem* filesystem;
        const std::string& virtualPath;
    } context{this, virtualPath};
    return SDL_EnumerateDirectory(nativePath.c_str(), [](void* userdata, const char* dirname, const char* name) {
        auto& context = *static_cast<Context*>(userdata);
        auto& filesystem = *context.filesystem;
        const std::string childNative = std::string(dirname) + name;
        const std::string childVirtual = (context.virtualPath == "/" ? std::string("/") : context.virtualPath + "/") + name;
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo(childNative.c_str(), &info)) return SDL_ENUM_FAILURE;
        const bool isDirectory = info.type == SDL_PATHTYPE_DIRECTORY;
        if (!isDirectory && info.type != SDL_PATHTYPE_FILE) return SDL_ENUM_CONTINUE;
        filesystem.m_fileList.emplace_back(filesystem.m_aliasPath, filesystem.m_basePath, childVirtual,
            isDirectory ? vfspp::EntryType::Directory : vfspp::EntryType::File);
        if (isDirectory && !filesystem.collectDirectory(childNative, childVirtual)) return SDL_ENUM_FAILURE;
        return SDL_ENUM_CONTINUE;
    }, &context);
}

void SdlFileSystem::Shutdown() {
    m_isInitialized = false;
    m_fileList.clear();
}

bool SdlFileSystem::IsInitialized() const {
    return m_isInitialized;
}

const std::string& SdlFileSystem::BasePath() const {
    return m_basePath;
}

const std::string& SdlFileSystem::VirtualPath() const {
    return m_aliasPath;
}

vfspp::IFileSystem::EntriesList SdlFileSystem::GetEntriesList(bool excludeDirectories) const {
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

bool SdlFileSystem::IsReadOnly() const {
    if (m_readOnly) return true;
    std::error_code ec;
    auto perms = std::filesystem::status(m_basePath, ec).permissions();
    if (ec) {
        return true; 
    }
    return (perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none;
}

std::optional<vfspp::EntryInfo> SdlFileSystem::GetEntryInfo(const std::string& virtualPath) const {
    vfspp::EntryInfo candidate(m_aliasPath, m_basePath, virtualPath);
    SDL_PathInfo info{};
    if (SDL_GetPathInfo(candidate.NativePath().c_str(), &info)) {
        if (info.type == SDL_PATHTYPE_FILE || info.type == SDL_PATHTYPE_DIRECTORY) {
            return vfspp::EntryInfo(m_aliasPath, m_basePath, virtualPath,
                info.type == SDL_PATHTYPE_DIRECTORY ? vfspp::EntryType::Directory : vfspp::EntryType::File);
        }
        return std::nullopt;
    }
#ifdef __ANDROID__
    // APK assets are not native paths. Use the AssetManager index or SDL IO fallback.
    for (const auto& entry : m_fileList) {
        if (entry.VirtualPath() == candidate.VirtualPath()) return entry;
    }
    if (auto* io = SDL_IOFromFile(candidate.NativePath().c_str(), "rb")) {
        SDL_CloseIO(io);
        return candidate;
    }
#endif
    return std::nullopt;
}

vfspp::IFilePtr SdlFileSystem::OpenFile(const std::string& virtualPath, vfspp::IFile::FileMode mode) {
    const bool write = vfspp::IFile::ModeHasFlag(mode, vfspp::IFile::FileMode::Write);
    if (write && IsReadOnly()) return nullptr;
    const auto entry = GetEntryInfo(virtualPath);
    if ((!entry && !write) || (entry && entry->IsDirectory())) return nullptr;
    auto file = std::make_shared<SdlFile>(entry.value_or(vfspp::EntryInfo(m_aliasPath, m_basePath, virtualPath)), m_readOnly);
    if (!file->Open(mode)) {
        return nullptr;
    }
    return file;
}

void SdlFileSystem::CloseFile(vfspp::IFilePtr file) {
    if (file) {
        file->Close();
    }
}

vfspp::IFilePtr SdlFileSystem::CreateFile(const std::string& virtualPath) {
    if (IsReadOnly()) return nullptr;
    const vfspp::EntryInfo entry(m_aliasPath, m_basePath, virtualPath);
    SDL_IOStream* io = SDL_IOFromFile(entry.NativePath().c_str(), "wb");
    if (io) {
        SDL_CloseIO(io);
        return OpenFile(virtualPath, vfspp::IFile::FileMode::ReadWrite);
    }
    return nullptr;
}

bool SdlFileSystem::RemoveFile(const std::string& virtualPath) {
    if (IsReadOnly()) return false;
    std::error_code ec;
    return std::filesystem::remove(vfspp::EntryInfo(m_aliasPath, m_basePath, virtualPath).NativePath(), ec);
}

bool SdlFileSystem::CopyFile(const std::string& srcVirtualPath, const std::string& dstVirtualPath, bool overwrite) {
    if (IsReadOnly()) return false;
    std::error_code ec;
    auto options = overwrite ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none;
    return std::filesystem::copy_file(vfspp::EntryInfo(m_aliasPath, m_basePath, srcVirtualPath).NativePath(), vfspp::EntryInfo(m_aliasPath, m_basePath, dstVirtualPath).NativePath(), options, ec);
}

bool SdlFileSystem::RenameFile(const std::string& srcVirtualPath, const std::string& dstVirtualPath) {
    if (IsReadOnly()) return false;
    std::error_code ec;
    std::filesystem::rename(vfspp::EntryInfo(m_aliasPath, m_basePath, srcVirtualPath).NativePath(), vfspp::EntryInfo(m_aliasPath, m_basePath, dstVirtualPath).NativePath(), ec);
    return !ec;
}

bool SdlFileSystem::MakeDirectory(const std::string& virtualPath) {
    if (IsReadOnly()) return false;
    std::error_code ec;
    return std::filesystem::create_directories(vfspp::EntryInfo(m_aliasPath, m_basePath, virtualPath).NativePath(), ec);
}

bool SdlFileSystem::DeleteDirectory(const std::string& virtualPath, bool recursive) {
    if (IsReadOnly()) return false;
    std::error_code ec;
    if (recursive) {
        return std::filesystem::remove_all(vfspp::EntryInfo(m_aliasPath, m_basePath, virtualPath).NativePath(), ec) > 0;
    }
    return std::filesystem::remove(vfspp::EntryInfo(m_aliasPath, m_basePath, virtualPath).NativePath(), ec);
}

bool SdlFileSystem::RenameDirectory(const std::string& srcVirtualPath, const std::string& dstVirtualPath) {
    return RenameFile(srcVirtualPath, dstVirtualPath);
}

bool SdlFileSystem::IsFileExists(const std::string& virtualPath) const {
    const auto entry = GetEntryInfo(virtualPath);
    return entry && entry->IsFile();
}

bool SdlFileSystem::IsDirectoryExists(const std::string& virtualPath) const {
    const auto entry = GetEntryInfo(virtualPath);
    return entry && entry->IsDirectory();
}

}
