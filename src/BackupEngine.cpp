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

namespace fs = std::filesystem;

namespace datasoftware {

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

    if (progress) progress(0, entries.size(), "Writing archive...");
    ArchiveWriter::write(archivePath, entries);

    if (progress) progress(entries.size(), entries.size(), "Backup complete!");
    return entries.size();
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
        fs::path p(fullPath);
        std::string fileName = p.filename().string();

        if (progress) progress(i, total, fileName);

        uint64_t size = fs::file_size(fullPath);
        std::ifstream file(fullPath, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + fullPath);
        }
        std::vector<char> buf(size);
        if (size > 0) file.read(buf.data(), size);
        file.close();

        FileEntry fe(fileName, size, std::move(buf));

        // Read metadata using Windows API
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExW(fs::path(fullPath).c_str(), GetFileExInfoStandard, &info)) {
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
static void restoreFileMetadata(const fs::path& filePath, const FileMetadata& md) {
    if (md.isEmpty()) return;

    HANDLE hFile = CreateFileW(
        filePath.c_str(),
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,  // No special flags required for regular files
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
}

size_t BackupEngine::restore(const std::string& archivePath,
                             const std::string& restoreDir,
                             ProgressCallback progress) {
    std::vector<FileEntry> entries = ArchiveReader::read(archivePath);
    size_t total = entries.size();

    if (progress) progress(0, total, "Restoring files...");

    fs::path restorePath(restoreDir);
    fs::create_directories(restorePath);

    for (size_t i = 0; i < total; ++i) {
        const auto& entry = entries[i];
        if (progress) progress(i, total, entry.relativePath);

        fs::path filePath = restorePath / entry.relativePath;

        switch (entry.fileType) {
        case FileType::Regular: {
            fs::create_directories(filePath.parent_path());
            std::ofstream out(filePath, std::ios::binary);
            if (!out.is_open()) {
                throw std::runtime_error("Cannot create file: "
                                         + filePath.string());
            }
            if (entry.fileSize > 0) {
                out.write(entry.data.data(), entry.fileSize);
            }
            out.close();
            break;
        }
        case FileType::Directory: {
            fs::create_directories(filePath);
            break;
        }
        case FileType::Symlink: {
            fs::create_directories(filePath.parent_path());
            std::error_code ec;
            fs::remove(filePath, ec);
            fs::create_symlink(entry.symlinkTarget, filePath, ec);
            break;
        }
        case FileType::HardLink: {
            fs::create_directories(filePath.parent_path());
            std::ofstream out(filePath, std::ios::binary);
            if (!out.is_open()) {
                throw std::runtime_error("Cannot create file: "
                                         + filePath.string());
            }
            if (entry.fileSize > 0) {
                out.write(entry.data.data(), entry.fileSize);
            }
            out.close();
            break;
        }
        case FileType::Fifo: {
            fs::create_directories(filePath.parent_path());
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
            fs::create_directories(filePath.parent_path());
            #ifndef _WIN32
            std::error_code ec;
            dev_t dev = makedev(entry.deviceMajor, entry.deviceMinor);
            fs::create_device(filePath, fs::status(filePath).permissions(),
                              dev, ec);
            #endif
            break;
        }
        }

        // Restore metadata after writing the file
        restoreFileMetadata(filePath, entry.metadata);
    }

    if (progress) progress(total, total, "Restore complete!");
    return total;
}

} // namespace datasoftware