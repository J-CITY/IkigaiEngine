#include "sdlFileSystem.h"
#include <algorithm>
#include <vector>
#include <filesystem>

namespace IKIGAI::RESOURCES {

// --- SdlFile ---

SdlFile::SdlFile(const vfspp::EntryInfo& fileInfo)
    : m_fileInfo(fileInfo)
{}

SdlFile::~SdlFile() {
    SdlFile::Close();
}

const vfspp::EntryInfo& SdlFile::GetEntryInfo() const {
    return m_fileInfo;
}

uint64_t SdlFile::Size() const {
    if (m_rwops) {
        return static_cast<uint64_t>(SDL_RWsize(m_rwops));
    }
    return 0;
}

bool SdlFile::IsReadOnly() const {
    std::error_code ec;
    auto perms = std::filesystem::status(m_fileInfo.NativePath(), ec).permissions();
    if (ec) {
        return true; 
    }
    return (perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none;
}

std::string SdlFile::getSdlMode(FileMode mode) const {
    std::string strMode = "rb";
    if (ModeHasFlag(mode, FileMode::ReadWrite)) {
        strMode = "r+b";
    } else if (ModeHasFlag(mode, FileMode::Write)) {
        strMode = "wb";
    }
    if (ModeHasFlag(mode, FileMode::Append)) {
        strMode = "ab";
    }
    return strMode;
}

bool SdlFile::Open(FileMode mode) {
    if (IsOpened()) {
        Close();
    }
    m_rwops = SDL_RWFromFile(m_fileInfo.NativePath().c_str(), getSdlMode(mode).c_str());
    return m_rwops != nullptr;
}

void SdlFile::Close() {
    if (m_rwops) {
        SDL_RWclose(m_rwops);
        m_rwops = nullptr;
    }
}

bool SdlFile::IsOpened() const {
    return m_rwops != nullptr;
}

uint64_t SdlFile::Seek(uint64_t offset, Origin origin) {
    if (!m_rwops) return 0;
    
    int whence = RW_SEEK_SET;
    if (origin == Origin::Begin) {
        whence = RW_SEEK_SET;
    } else if (origin == Origin::Set) {
        whence = RW_SEEK_CUR;
    } else if (origin == Origin::End) {
        whence = RW_SEEK_END;
    }

    SDL_RWseek(m_rwops, static_cast<Sint64>(offset), whence);
    return Tell();
}

uint64_t SdlFile::Tell() const {
    if (!m_rwops) return 0;
    return static_cast<uint64_t>(SDL_RWtell(m_rwops));
}

uint64_t SdlFile::Read(std::span<uint8_t> buffer) {
    if (!m_rwops || buffer.empty()) return 0;
    return static_cast<uint64_t>(SDL_RWread(m_rwops, buffer.data(), 1, buffer.size_bytes()));
}

uint64_t SdlFile::Read(std::vector<uint8_t>& buffer, uint64_t size) {
    buffer.resize(size);
    if (!m_rwops || size == 0) return 0;
    return static_cast<uint64_t>(SDL_RWread(m_rwops, buffer.data(), 1, size));
}

uint64_t SdlFile::Write(std::span<const uint8_t> buffer) {
    if (!m_rwops || buffer.empty()) return 0;
    return static_cast<uint64_t>(SDL_RWwrite(m_rwops, buffer.data(), 1, buffer.size_bytes()));
}

uint64_t SdlFile::Write(const std::vector<uint8_t>& buffer) {
    if (!m_rwops || buffer.empty()) return 0;
    return static_cast<uint64_t>(SDL_RWwrite(m_rwops, buffer.data(), 1, buffer.size()));
}


// --- SdlFileSystem ---

SdlFileSystem::SdlFileSystem(const std::string& aliasPath, const std::string& basePath)
    : m_aliasPath(aliasPath), m_basePath(basePath)
{}

SdlFileSystem::~SdlFileSystem() {
    SdlFileSystem::Shutdown();
}

bool SdlFileSystem::Initialize() {
    m_isInitialized = true;
    return true;
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
    return m_fileList;
}

bool SdlFileSystem::IsReadOnly() const {
    std::error_code ec;
    auto perms = std::filesystem::status(m_basePath, ec).permissions();
    if (ec) {
        return true; 
    }
    return (perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none;
}

std::optional<vfspp::EntryInfo> SdlFileSystem::GetEntryInfo(const std::string& virtualPath) const {
    return vfspp::EntryInfo(m_aliasPath, m_basePath, virtualPath);
}

vfspp::IFilePtr SdlFileSystem::OpenFile(const std::string& virtualPath, vfspp::IFile::FileMode mode) {
    auto file = std::make_shared<SdlFile>(GetEntryInfo(virtualPath).value());
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
    auto entry = GetEntryInfo(virtualPath).value();
    SDL_RWops* rw = SDL_RWFromFile(entry.NativePath().c_str(), "wb");
    if (rw) {
        SDL_RWclose(rw);
        return OpenFile(virtualPath, vfspp::IFile::FileMode::ReadWrite);
    }
    return nullptr;
}

bool SdlFileSystem::RemoveFile(const std::string& virtualPath) {
    if (IsReadOnly()) return false;
    std::error_code ec;
    return std::filesystem::remove(GetEntryInfo(virtualPath).value().NativePath(), ec);
}

bool SdlFileSystem::CopyFile(const std::string& srcVirtualPath, const std::string& dstVirtualPath, bool overwrite) {
    if (IsReadOnly()) return false;
    std::error_code ec;
    auto options = overwrite ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none;
    return std::filesystem::copy_file(GetEntryInfo(srcVirtualPath).value().NativePath(), GetEntryInfo(dstVirtualPath).value().NativePath(), options, ec);
}

bool SdlFileSystem::RenameFile(const std::string& srcVirtualPath, const std::string& dstVirtualPath) {
    if (IsReadOnly()) return false;
    std::error_code ec;
    std::filesystem::rename(GetEntryInfo(srcVirtualPath).value().NativePath(), GetEntryInfo(dstVirtualPath).value().NativePath(), ec);
    return !ec;
}

bool SdlFileSystem::MakeDirectory(const std::string& virtualPath) {
    if (IsReadOnly()) return false;
    std::error_code ec;
    return std::filesystem::create_directories(GetEntryInfo(virtualPath).value().NativePath(), ec);
}

bool SdlFileSystem::DeleteDirectory(const std::string& virtualPath, bool recursive) {
    if (IsReadOnly()) return false;
    std::error_code ec;
    if (recursive) {
        return std::filesystem::remove_all(GetEntryInfo(virtualPath).value().NativePath(), ec) > 0;
    }
    return std::filesystem::remove(GetEntryInfo(virtualPath).value().NativePath(), ec);
}

bool SdlFileSystem::RenameDirectory(const std::string& srcVirtualPath, const std::string& dstVirtualPath) {
    return RenameFile(srcVirtualPath, dstVirtualPath);
}

bool SdlFileSystem::IsFileExists(const std::string& virtualPath) const {
    auto entry = GetEntryInfo(virtualPath).value();
    std::error_code ec;
    if (std::filesystem::exists(entry.NativePath(), ec) && std::filesystem::is_regular_file(entry.NativePath(), ec)) {
        return true;
    }

    // Fallback для SDL-специфичных путей
    SDL_RWops* rwOps = SDL_RWFromFile(entry.NativePath().c_str(), "rb");
    if (rwOps) {
        SDL_RWclose(rwOps);
        return true;
    }
    return false;
}

bool SdlFileSystem::IsDirectoryExists(const std::string& virtualPath) const {
    auto entry = GetEntryInfo(virtualPath).value();
    std::error_code ec;
    bool isDirectory = std::filesystem::is_directory(entry.NativePath(), ec);
    if (!ec) {
        return isDirectory;
    }
    return false;
}

}
