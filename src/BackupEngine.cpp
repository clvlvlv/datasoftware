#include "datasoftware/BackupEngine.h"
#include "datasoftware/FileTraverser.h"
#include "datasoftware/ArchiveWriter.h"
#include "datasoftware/ArchiveReader.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>

namespace fs = std::filesystem;

namespace datasoftware {

// Helper: convert filesystem path to UTF-8 std::string
static std::string pathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    #if defined(__cpp_lib_char8_t) || (defined(_MSC_VER) && _MSVC_LANG >= 202002)
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
    #else
    return u8;
    #endif
}

size_t BackupEngine::backup(const std::string& sourceDir,
                            const std::string& archivePath,
                            ProgressCallback progress) {
    return backup(sourceDir, archivePath, BackupFilter{}, progress);
}

    size_t BackupEngine::backup(const std::string& sourceDir,
                            const std::string& archivePath,
                            const BackupFilter& filter,
                            ProgressCallback progress) {
    if (progress) progress(0, 1, "Scanning directory...");
   std::vector<FileEntry> entries = FileTraverser::traverse(sourceDir, progress, filter);

    // Prepend root directory name so restore creates the source folder
    {
        std::string rootName = pathToUtf8(fs::path(sourceDir).filename());
        if (!rootName.empty() && rootName != ".") {
            for (auto& e : entries) {
                e.relativePath = rootName + "/" + e.relativePath;
            }
        }
    }
    size_t fileCount = 0;
    for (const auto& entry : entries) {
        if (entry.fileType != FileType::Directory) {
            fileCount++;
        }
    }

    if (progress) progress(0, fileCount, "Writing archive...");
    ArchiveWriter::write(archivePath, entries);

    if (progress) progress(fileCount, fileCount, "Backup complete!");
    return fileCount;
}

size_t BackupEngine::backupFiles(const std::vector<std::string>& filePaths,
                                  const std::string& archivePath,
                                  ProgressCallback progress) {
    size_t total = filePaths.size();
    if (progress) progress(0, total, "Reading files...");

    std::vector<FileEntry> entries;
    entries.reserve(total);

    for (size_t i = 0; i < total; ++i) {
        const auto& fullPath = filePaths[i];
        fs::path p = fs::u8path(fullPath);
        std::string fileName;
        try { fileName = pathToUtf8(p.filename()); } catch (...) { fileName = p.filename().string(); }

        if (progress) progress(i, total, fileName);

        uint64_t size = fs::file_size(p);
        std::ifstream file(p, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + fileName);
        }
        std::vector<char> buf(size);
        if (size > 0) file.read(buf.data(), size);
        file.close();

        FileEntry fe(fileName, size, std::move(buf));

        // Read metadata using Windows API
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &info)) {
            fe.metadata.createTime = (static_cast<int64_t>(info.ftCreationTime.dwHighDateTime) << 32)
                                    | static_cast<int64_t>(info.ftCreationTime.dwLowDateTime);
            fe.metadata.modTime = (static_cast<int64_t>(info.ftLastWriteTime.dwHighDateTime) << 32)
                                 | static_cast<int64_t>(info.ftLastWriteTime.dwLowDateTime);
            fe.metadata.accessTime = (static_cast<int64_t>(info.ftLastAccessTime.dwHighDateTime) << 32)
                                    | static_cast<int64_t>(info.ftLastAccessTime.dwLowDateTime);
            fe.metadata.attributes = info.dwFileAttributes;
        }

        entries.push_back(std::move(fe));
    }

    if (progress) progress(total, total, "Writing archive...");
    ArchiveWriter::write(archivePath, entries);

    if (progress) progress(total, total, "Backup complete!");
    return total;
}

// ---- restore file metadata using Windows API ----

// ---- enable SE_RESTORE_NAME privilege for owner restoration ----
static bool enableRestorePrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken))
        return false;
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 2;
    if (!LookupPrivilegeValueW(nullptr,
                             L"SeTakeOwnershipPrivilege", &tp.Privileges[1].Luid)) { tp.Privileges[1].Luid.LowPart = 0; }
    if (!LookupPrivilegeValueW(nullptr, L"SeRestorePrivilege", &tp.Privileges[0].Luid)) {
        CloseHandle(hToken); return false;
    }
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    tp.Privileges[1].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
    CloseHandle(hToken);
    return ok && GetLastError() == ERROR_SUCCESS;
}

static void restoreFileMetadata(const fs::path& filePath, const FileMetadata& md) {
    if (md.isEmpty()) return;

    HANDLE hFile = CreateFileW(
        filePath.c_str(),
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,  // Support both files and directories
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) return;

    // Restore timestamps
    if (md.createTime != 0 || md.modTime != 0 || md.accessTime != 0) {
        FILETIME create, access, write;
        create.dwLowDateTime  = static_cast<DWORD>(md.createTime & 0xFFFFFFFF);
        create.dwHighDateTime = static_cast<DWORD>(md.createTime >> 32);
        access.dwLowDateTime  = static_cast<DWORD>(md.accessTime & 0xFFFFFFFF);
        access.dwHighDateTime = static_cast<DWORD>(md.accessTime >> 32);
        write.dwLowDateTime   = static_cast<DWORD>(md.modTime & 0xFFFFFFFF);
        write.dwHighDateTime  = static_cast<DWORD>(md.modTime >> 32);
        SetFileTime(hFile, &create, &access, &write);
    }

    CloseHandle(hFile);

    // Restore file attributes
    if (md.attributes != 0) {
        SetFileAttributesW(filePath.c_str(), md.attributes);
    }

    // Restore file owner (may fail without admin privilege)
    if (!md.owner.empty()) {
        std::wstring wOwner(md.owner.begin(), md.owner.end());
        BYTE sidBuf[SECURITY_MAX_SID_SIZE];
        DWORD sidLen = SECURITY_MAX_SID_SIZE;
        wchar_t domain[256];
        DWORD domainLen = 256;
        SID_NAME_USE peUse;
        if (LookupAccountNameW(nullptr, wOwner.c_str(), (PSID)sidBuf, &sidLen,
                               domain, &domainLen, &peUse)) {
            std::wstring wPath = filePath.wstring();
            enableRestorePrivilege();
            SetNamedSecurityInfoW(&wPath[0], SE_FILE_OBJECT,
                                  OWNER_SECURITY_INFORMATION,
                                  (PSID)sidBuf, nullptr, nullptr, nullptr);
        }
    }
}

// ---- streaming backup (avoids loading files into memory) ----
size_t BackupEngine::backupStream(const std::string& sourceDir,
                                   const std::string& archivePath,
                                   ProgressCallback progress,
                                   const BackupFilter& filter) {
    // List files without reading content
    auto files = FileTraverser::listFiles(sourceDir, progress, filter);
    size_t total = files.size();
    if (progress) progress(0, total, "Writing archive...");

    // Open archive and write header
    std::ofstream out(archivePath, std::ios::binary);
    if (!out.is_open())
        throw std::runtime_error("Cannot create archive: " + archivePath);

    ArchiveWriter::writeHeader(out, static_cast<uint32_t>(total));

    // Write each file directly from disk
    for (size_t i = 0; i < total; ++i) {
        const auto& fi = files[i];
        if (progress) progress(i, total, fi.relativePath);

        fs::path fullPath = fs::u8path(sourceDir) / fs::u8path(fi.relativePath);
        ArchiveWriter::writeEntryStream(out, fullPath.string(),
                                         fi.relativePath, fi.metadata);
    }

    out.close();
    if (progress) progress(total, total, "Backup complete!");
    return total;
}

// ---- streaming restore (avoids loading files into memory) ----
size_t BackupEngine::restoreStream(const std::string& archivePath,
                                    const std::string& restoreDir,
                                    ProgressCallback progress) {
    std::ifstream in(archivePath, std::ios::binary);
    if (!in.is_open())
        throw std::runtime_error("Cannot open archive: " + archivePath);

    uint32_t entryCount = 0;
    uint32_t version = 0;
    ArchiveReader::readHeader(in, entryCount, version);

    if (progress) progress(0, entryCount, "Restoring...");

    fs::path restorePath(restoreDir);
    fs::create_directories(restorePath);

    for (uint32_t i = 0; i < entryCount; ++i) {
        if (progress) progress(i, entryCount, "");
        ArchiveReader::extractNext(in, restoreDir, version);
    }

    in.close();
    if (progress) progress(entryCount, entryCount, "Restore complete!");
    return entryCount;
}

size_t BackupEngine::restore(const std::string& archivePath,
                             const std::string& restoreDir,
                             ProgressCallback progress) {
    std::vector<FileEntry> entries = ArchiveReader::read(archivePath);
    
    size_t total = 0;
    for (const auto& entry : entries) {
        if (entry.fileType != FileType::Directory) {
            total++;
        }
    }

    if (progress) progress(0, total, "Restoring files...");

    fs::path restorePath(restoreDir);
    fs::create_directories(restorePath);

    size_t restoredCount = 0;
    for (const auto& entry : entries) {
        if (entry.fileType == FileType::Directory) {
            fs::path filePath = restorePath / fs::u8path(entry.relativePath);
            fs::create_directories(filePath);
            restoreFileMetadata(filePath, entry.metadata);
            continue;
        }

        if (progress) progress(restoredCount, total, entry.relativePath);

        fs::path filePath = restorePath / fs::u8path(entry.relativePath);
        fs::create_directories(filePath.parent_path());

        switch (entry.fileType) {
        case FileType::Regular: {
            std::ofstream out(filePath, std::ios::binary);
            if (!out.is_open()) {
                throw std::runtime_error("Cannot create file: " + filePath.string());
            }
            if (entry.fileSize > 0) {
                out.write(entry.data.data(), entry.fileSize);
            }
            out.close();
            break;
        }
        case FileType::Symlink: {
            std::error_code ec;
            fs::remove(filePath, ec);
            fs::create_symlink(entry.symlinkTarget, filePath, ec);
            break;
        }
        case FileType::HardLink: {
            std::ofstream out(filePath, std::ios::binary);
            if (!out.is_open()) {
                throw std::runtime_error("Cannot create file: " + filePath.string());
            }
            if (entry.fileSize > 0) {
                out.write(entry.data.data(), entry.fileSize);
            }
            out.close();
            break;
        }
        case FileType::Fifo: {
            std::error_code ec;
            fs::remove(filePath, ec);
            #ifdef _WIN32
            std::ofstream out(filePath, std::ios::binary);
            out.close();
            #else
            fs::create_fifo(filePath, ec);
            #endif
            break;
        }
        case FileType::Device: {
            #ifndef _WIN32
            std::error_code ec;
            dev_t dev = makedev(entry.deviceMajor, entry.deviceMinor);
            fs::create_device(filePath, fs::status(filePath).permissions(),
                              dev, ec);
            #endif
            break;
        }
        default:
            break;
        }

        // Restore metadata after writing the file
        restoreFileMetadata(filePath, entry.metadata);
        restoredCount++;
    }

    if (progress) progress(total, total, "Restore complete!");
    return restoredCount;
}

} // namespace datasoftware
