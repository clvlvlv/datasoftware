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

namespace fs = std::filesystem;

namespace datasoftware {

// ---- read file metadata using Windows API ----
static FileMetadata readFileMetadata(const fs::path& fullPath) {
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

    return md;
}

// ---- attach metadata to a FileEntry ----
static void attachMetadata(FileEntry& fe, const fs::path& fullPath) {
    fe.metadata = readFileMetadata(fullPath);
}

// ---- helper: count files (fast) ----
static size_t countFiles(const std::string& sourceDir) {
    fs::path base(sourceDir);
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
    fs::path base(sourceDir);
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
        std::string relStr = relative.string();
        std::replace(relStr.begin(), relStr.end(), '\\', '/');
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

            if (progress) progress(processed, total, relStr);
            fe = readFile(base.string(), relStr);
            attachMetadata(fe, entry.path());
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

FileEntry FileTraverser::readFile(const std::string& baseDir,
                                   const std::string& relativePath) {
    fs::path fullPath = fs::path(baseDir) / relativePath;
    uint64_t size = fs::file_size(fullPath);

    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + fullPath.string());
    }

    std::vector<char> buffer(size);
    if (size > 0) {
        file.read(buffer.data(), size);
        if (!file) {
            throw std::runtime_error("Failed to read file: " + fullPath.string());
        }
    }

    return FileEntry(relativePath, size, std::move(buffer));
}

} // namespace datasoftware