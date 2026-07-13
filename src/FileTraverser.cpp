#include "datasoftware/FileTraverser.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <chrono>
#include <cctype>
#include <ctime>
#include <unordered_set>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>

namespace fs = std::filesystem;

namespace datasoftware {

// Helper: convert filesystem path to UTF-8 std::string
static std::string pathToUtf8(const fs::path& p) {
    auto u8 = p.u8string();
    // C++17: u8string() returns std::string; C++20: returns std::u8string
    #if defined(__cpp_lib_char8_t) || defined(_MSC_VER) && _MSVC_LANG >= 202002
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
    #else
    return u8;
    #endif
}

// ---- read file metadata using Windows API ----
FileMetadata FileTraverser::readFileMetadata(const fs::path& fullPath) {
    FileMetadata md;

    WIN32_FILE_ATTRIBUTE_DATA info;
    if (GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &info)) {
        // FILE_TIMEs are already 100-ns intervals since 1601-01-01 (FILETIME format)
        md.createTime = (static_cast<int64_t>(info.ftCreationTime.dwHighDateTime) << 32)
                       | static_cast<int64_t>(info.ftCreationTime.dwLowDateTime);
        md.modTime = (static_cast<int64_t>(info.ftLastWriteTime.dwHighDateTime) << 32)
                    | static_cast<int64_t>(info.ftLastWriteTime.dwLowDateTime);
        md.accessTime = (static_cast<int64_t>(info.ftLastAccessTime.dwHighDateTime) << 32)
                       | static_cast<int64_t>(info.ftLastAccessTime.dwLowDateTime);
        md.attributes = info.dwFileAttributes;
    }

    // Populate owner using Windows API
    PSECURITY_DESCRIPTOR pSD = nullptr;
    std::wstring wpath = fullPath.wstring();
    if (GetNamedSecurityInfoW(&wpath[0], SE_FILE_OBJECT,
                              OWNER_SECURITY_INFORMATION, nullptr, nullptr,
                              nullptr, nullptr, &pSD) == ERROR_SUCCESS && pSD) {
        PSID pOwner = nullptr;
        BOOL ownerDefaulted = FALSE;
        if (GetSecurityDescriptorOwner(pSD, &pOwner, &ownerDefaulted) && pOwner) {
            wchar_t ownerName[256], domainName[256];
            DWORD ownerLen = 256, domainLen = 256;
            SID_NAME_USE peUse;
            if (LookupAccountSidW(nullptr, pOwner, ownerName, &ownerLen,
                                  domainName, &domainLen, &peUse)) {
                std::wstring ws(ownerName);
                md.owner = std::string(ws.begin(), ws.end());
            }
        }
        LocalFree(pSD);
    }

    return md;
}

// ---- attach metadata to a FileEntry ----
void FileTraverser::attachMetadata(FileEntry& fe, const fs::path& fullPath) {
    fe.metadata = readFileMetadata(fullPath);
}

// ---- hard link detection using Windows API ----
static bool getHardLinkInfo(const std::filesystem::path& path, uint64_t& fileIndex) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    BY_HANDLE_FILE_INFORMATION info;
    bool ok = GetFileInformationByHandle(h, &info);
    CloseHandle(h);

    if (ok && info.nNumberOfLinks > 1) {
        fileIndex = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
        return true;
    }
    return false;
}

// ---- helper: count files (fast) ----
static size_t countFiles(const std::string& sourceDir) {
    fs::path base = fs::u8path(sourceDir);
    size_t count = 0;
    auto opts = fs::directory_options::skip_permission_denied;
    for (auto it = fs::recursive_directory_iterator(base, opts);
         it != fs::recursive_directory_iterator(); ++it) {
        if (fs::is_symlink(it->symlink_status())) continue;
        if (fs::is_regular_file(it->symlink_status())) ++count;
    }
    return count;
}

// ---- traverse without progress or filter ----
std::vector<FileEntry> FileTraverser::traverse(const std::string& sourceDir) {
    return traverse(sourceDir, nullptr, BackupFilter{});
}

// ---- traverse with progress, no filter ----
std::vector<FileEntry> FileTraverser::traverse(const std::string& sourceDir,
                                                ProgressCallback progress) {
    return traverse(sourceDir, progress, BackupFilter{});
}

// ---- traverse with progress + filter ----
std::vector<FileEntry> FileTraverser::traverse(const std::string& sourceDir,
                                                ProgressCallback progress,
                                                const BackupFilter& filter) {
    fs::path base = fs::u8path(sourceDir);
    if (!fs::exists(base) || !fs::is_directory(base)) {
        throw std::runtime_error("Source directory does not exist: " + sourceDir);
    }

    size_t total = countFiles(sourceDir);
    if (progress) progress(0, total, "Scanning...");

    auto opts = fs::directory_options::skip_permission_denied;
    std::unordered_map<std::string, size_t> seen;
    std::unordered_set<std::string> addedDirs;
    std::vector<FileEntry> entries;
    size_t processed = 0;

    for (const auto& entry : fs::recursive_directory_iterator(base, opts)) {
        fs::path relative = fs::relative(entry.path(), base);
        std::string relStr = pathToUtf8(relative);
        auto status = entry.symlink_status();

        FileEntry fe;

        if (fs::is_directory(status)) {
            if (addedDirs.find(relStr) == addedDirs.end()) {
                FileEntry dirEntry(relStr, FileType::Directory, 0, std::vector<char>{});
                seen[relStr] = entries.size();
                entries.push_back(std::move(dirEntry));
                addedDirs.insert(relStr);
            }
            continue;
        }
        
        if (fs::is_symlink(status)) {
            std::error_code ec;
            fs::path target = fs::read_symlink(entry.path(), ec);
            fe = FileEntry(relStr, FileType::Symlink, 0,
                           std::vector<char>{},
                           ec ? "" : target.string());
        }
        else if (fs::is_regular_file(status)) {
            // 应用过滤器
            if (filter.isActive()) {
                std::string ext;
                auto dot = relStr.rfind('.');
                if (dot != std::string::npos) {
                    ext = relStr.substr(dot);
                    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
                }

                auto ft = fs::last_write_time(entry.path());
                int64_t mtime = 0;
                try {
                    auto s = std::chrono::duration_cast<std::chrono::seconds>(
                        ft.time_since_epoch()).count();
                    #ifdef _WIN32
                    mtime = static_cast<int64_t>(s - 11644473600LL);
                    #else
                    mtime = static_cast<int64_t>(s);
                    #endif
                } catch (...) { mtime = 0; }

                auto md = readFileMetadata(entry.path());
                std::string owner = md.owner;
                uint64_t fsize = 0;
                try { fsize = fs::file_size(entry.path()); } catch (...) {}

                if (!filter.matches(relStr, ext, fsize, mtime, owner)) {
                    if (progress) progress(processed, total, "[filtered] " + relStr);
                    ++processed;
                    continue;
                }
            }

            // Hard link detection (Windows)
            {
                uint64_t fileIndex = 0;
                if (getHardLinkInfo(entry.path(), fileIndex)) {
                    if (progress) progress(processed, total, relStr);
                    fe = readFile(base.string(), relStr);
                    fe.fileType = FileType::HardLink;
                    fe.hardLinkId = fileIndex;
                    attachMetadata(fe, entry.path());
                } else {
                    if (progress) progress(processed, total, relStr);
                    fe = readFile(base.string(), relStr);
                    attachMetadata(fe, entry.path());
                }
            }
            ++processed;
        }
        else if (fs::is_fifo(status)) {
            fe = FileEntry(relStr, FileType::Fifo, 0, std::vector<char>{});
        }
        else if (fs::is_character_file(status) || fs::is_block_file(status)) {
            fe = FileEntry(relStr, FileType::Device, 0, std::vector<char>{});
        }
        else {
            continue;
        }

        fs::path parent = relative.parent_path();
        while (!parent.empty()) {
            std::string parentStr = parent.string();
            std::replace(parentStr.begin(), parentStr.end(), '\\', '/');
            if (addedDirs.find(parentStr) == addedDirs.end()) {
                FileEntry dirEntry(parentStr, FileType::Directory, 0, std::vector<char>{});
                seen[parentStr] = entries.size();
                entries.push_back(std::move(dirEntry));
                addedDirs.insert(parentStr);
            }
            parent = parent.parent_path();
        }

        // Dedup
        auto it = seen.find(relStr);
        if (it != seen.end()) {
            if (fe.fileType == FileType::Regular &&
                entries[it->second].fileType != FileType::Regular) {
                entries[it->second] = std::move(fe);
            }
            continue;
        }

        seen[relStr] = entries.size();
        entries.push_back(std::move(fe));
    }

    if (progress) progress(total, total, "Done.");
    return entries;
}

// ---- listFiles: collect file info without reading content ----
std::vector<FileTraverser::FileInfo> FileTraverser::listFiles(
    const std::string& sourceDir,
    ProgressCallback progress,
    const BackupFilter& filter) {

    fs::path base = fs::u8path(sourceDir);
    if (!fs::exists(base) || !fs::is_directory(base)) {
        throw std::runtime_error("Source directory does not exist: " + sourceDir);
    }

    // Fast count for progress
    size_t total = 0;
    auto opts = fs::directory_options::skip_permission_denied;
    for (auto it = fs::recursive_directory_iterator(base, opts);
         it != fs::recursive_directory_iterator(); ++it) {
        if (fs::is_symlink(it->symlink_status())) continue;
        if (fs::is_regular_file(it->symlink_status())) ++total;
    }
    if (progress) progress(0, total, "Scanning...");

    std::vector<FileInfo> files;
    std::unordered_set<std::string> addedDirs;
    size_t processed = 0;

    for (const auto& entry : fs::recursive_directory_iterator(base, opts)) {
        fs::path relative = fs::relative(entry.path(), base);
        std::string relStr = pathToUtf8(relative);
        auto status = entry.symlink_status();

        if (fs::is_directory(status)) {
            if (addedDirs.find(relStr) == addedDirs.end()) {
                FileInfo fi;
                fi.relativePath = relStr;
                fi.fileType = FileType::Directory;
                files.push_back(std::move(fi));
                addedDirs.insert(relStr);
            }
            continue;
        }

        if (fs::is_symlink(status)) {
            std::error_code ec;
            fs::path target = fs::read_symlink(entry.path(), ec);
            FileInfo fi;
            fi.relativePath = relStr;
            fi.fileType = FileType::Symlink;
            fi.symlinkTarget = ec ? "" : pathToUtf8(target);
            files.push_back(std::move(fi));
            continue;
        }

        if (fs::is_regular_file(status)) {
            // Apply filter
            if (filter.isActive()) {
                std::string ext;
                auto dot = relStr.rfind('.');
                if (dot != std::string::npos) {
                    ext = relStr.substr(dot);
                    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
                }
                auto md = readFileMetadata(entry.path());
                auto ft = fs::last_write_time(entry.path());
                int64_t mtime = 0;
                try {
                    auto s = std::chrono::duration_cast<std::chrono::seconds>(
                        ft.time_since_epoch()).count();
                    #ifdef _WIN32
                    mtime = static_cast<int64_t>(s - 11644473600LL);
                    #else
                    mtime = static_cast<int64_t>(s);
                    #endif
                } catch (...) { mtime = 0; }
                uint64_t fsize = fs::file_size(entry.path());
                if (!filter.matches(relStr, ext, fsize, mtime, md.owner))
                    continue;
            }

            if (progress) progress(processed, total, relStr);

            FileInfo fi;
            fi.relativePath = relStr;
            fi.fileType = FileType::Regular;
            fi.fileSize = fs::file_size(entry.path());
            fi.metadata = readFileMetadata(entry.path());

            // Hard link detection (Windows)
            {
                uint64_t fileIndex = 0;
                if (getHardLinkInfo(entry.path(), fileIndex)) {
                    fi.fileType = FileType::HardLink;
                    fi.hardLinkId = fileIndex;
                }
            }

            files.push_back(std::move(fi));
            ++processed;
        }
    }

    if (progress) progress(total, total, "Done.");
    return files;
}

FileEntry FileTraverser::readFile(const std::string& baseDir,
                                   const std::string& relativePath) {
    fs::path fullPath = fs::u8path(baseDir) / fs::u8path(relativePath);
    uint64_t size = fs::file_size(fullPath);

    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + pathToUtf8(fullPath));
    }

    std::vector<char> buffer(size);
    if (size > 0) {
        file.read(buffer.data(), size);
        if (!file) {
            throw std::runtime_error("Failed to read file: " + pathToUtf8(fullPath));
        }
    }

    return FileEntry(relativePath, size, std::move(buffer));
}

} // namespace datasoftware
