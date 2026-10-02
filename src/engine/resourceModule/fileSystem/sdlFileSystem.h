#pragma once

#include <vfspp/IFile.h>
#include <vfspp/IFileSystem.h>
#include <SDL3/SDL.h>

namespace IKIGAI::RESOURCES {

    class SdlFile : public vfspp::IFile {
    public:
        SdlFile(const vfspp::EntryInfo& fileInfo);
        ~SdlFile() override;

        const vfspp::EntryInfo& GetEntryInfo() const override;
        uint64_t Size() const override;
        bool IsReadOnly() const override;
        bool Open(FileMode mode) override;
        void Close() override;
        bool IsOpened() const override;
        uint64_t Seek(uint64_t offset, Origin origin) override;
        uint64_t Tell() const override;
        uint64_t Read(std::span<uint8_t> buffer) override;
        uint64_t Read(std::vector<uint8_t>& buffer, uint64_t size) override;
        uint64_t Write(std::span<const uint8_t> buffer) override;
        uint64_t Write(const std::vector<uint8_t>& buffer) override;

    private:
        std::string getSdlMode(FileMode mode) const;

        vfspp::EntryInfo m_fileInfo;
        SDL_IOStream* m_io = nullptr;
    };

    class SdlFileSystem : public vfspp::IFileSystem {
    public:
        SdlFileSystem(const std::string& aliasPath, const std::string& basePath);
        ~SdlFileSystem() override;

        bool Initialize() override;
        void Shutdown() override;
        bool IsInitialized() const override;
        const std::string& BasePath() const override;
        const std::string& VirtualPath() const override;
        EntriesList GetEntriesList(bool excludeDirectories = true) const override;
        bool IsReadOnly() const override;

        vfspp::IFilePtr OpenFile(const std::string& virtualPath, vfspp::IFile::FileMode mode) override;
        void CloseFile(vfspp::IFilePtr file) override;
        vfspp::IFilePtr CreateFile(const std::string& virtualPath) override;
        bool RemoveFile(const std::string& virtualPath) override;
        bool CopyFile(const std::string& srcVirtualPath, const std::string& dstVirtualPath, bool overwrite = false) override;
        bool RenameFile(const std::string& srcVirtualPath, const std::string& dstVirtualPath) override;
        bool MakeDirectory(const std::string& virtualPath) override;
        bool DeleteDirectory(const std::string& virtualPath, bool recursive = false) override;
        bool RenameDirectory(const std::string& srcVirtualPath, const std::string& dstVirtualPath) override;
        bool IsFileExists(const std::string& virtualPath) const override;
        bool IsDirectoryExists(const std::string& virtualPath) const override;
        std::optional<vfspp::EntryInfo> GetEntryInfo(const std::string& virtualPath) const override;

    private:
        std::string m_aliasPath;
        std::string m_basePath;
        bool m_isInitialized = false;
        EntriesList m_fileList;
    };

}
