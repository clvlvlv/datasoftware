#include "datasoftware/FileTraverser.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;

namespace datasoftware {

// ---- helper: count files (fast) ----
static size_t countFiles(const std::string& sourceDir) {
    fs::path base(sourceDir);
    size_t count = 0;
    auto opts = fs::directory_options::skip_permission_denied;
    for (auto it = fs::recursive_directory_iterator(base, opts);
         it != fs::recursive_directory_iterator(); ++it) {
        // Skip symlinks to avoid double-counting on Windows
        if (fs::is_symlink(it->symlink_status())) continue;
        if (fs::is_regular_file(it->symlink_status())) ++count;
    }
    return count;
}

// ---- traverse without progress ----
std::vector<FileEntry> FileTraverser::traverse(const std::string& sourceDir) {
    return traverse(sourceDir, nullptr);
}

// ---- traverse with progress ----
std::vector<FileEntry> FileTraverser::traverse(const std::string& sourceDir,
                                                ProgressCallback progress) {
    fs::path base(sourceDir);
    if (!fs::exists(base) || !fs::is_directory(base)) {
        throw std::runtime_error("Source directory does not exist: " + sourceDir);
    }

    // Fast count for progress bar
    size_t total = countFiles(sourceDir);
    if (progress) progress(0, total, "Counting files...");

    auto opts = fs::directory_options::skip_permission_denied;

    std::unordered_map<std::string, size_t> seen;
    std::vector<FileEntry> entries;
    size_t processed = 0;

    for (const auto& entry : fs::recursive_directory_iterator(base, opts)) {
        fs::path relative = fs::relative(entry.path(), base);
        std::string relStr = relative.string();
        auto status = entry.symlink_status();

        FileEntry fe;

        if (fs::is_symlink(status)) {
            std::error_code ec;
            fs::path target = fs::read_symlink(entry.path(), ec);
            fe = FileEntry(relStr, FileType::Symlink, 0,
                           std::vector<char>{},
                           ec ? "" : target.string());
        }
        else if (fs::is_directory(status)) {
            fe = FileEntry(relStr, FileType::Directory, 0, std::vector<char>{});
        }
        else if (fs::is_regular_file(status)) {
            if (progress) {
                progress(processed, total, relStr);
            }
            fe = readFile(base.string(), relStr);
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

    if (progress) progress(total, total, "Done scanning.");
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