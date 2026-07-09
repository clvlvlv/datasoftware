#ifndef DATASOFTWARE_FILETRAVERSER_H
#define DATASOFTWARE_FILETRAVERSER_H

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include "FileEntry.h"
#include "BackupEngine.h"       // for ProgressCallback
#include "BackupFilter.h"

namespace datasoftware {

class FileTraverser {
public:
    /// File info without content (for streaming backup, avoids loading into memory)
    struct FileInfo {
        std::string relativePath;
        FileType    fileType     = FileType::Regular;
        uint64_t    fileSize     = 0;
        FileMetadata metadata;
        std::string symlinkTarget;
        uint64_t    hardLinkId   = 0;
        uint32_t    deviceMajor  = 0;
        uint32_t    deviceMinor  = 0;
    };

    /// Traverse and read all files (no progress, no filter)
    static std::vector<FileEntry> traverse(const std::string& sourceDir);

    /// Traverse with progress callback (no filter)
    static std::vector<FileEntry> traverse(const std::string& sourceDir,
                                           ProgressCallback progress);

    /// Traverse with filter + progress
    static std::vector<FileEntry> traverse(const std::string& sourceDir,
                                           ProgressCallback progress,
                                           const BackupFilter& filter);

    /// List files without reading content (for streaming backup)
    static std::vector<FileInfo> listFiles(const std::string& sourceDir,
                                           ProgressCallback progress = nullptr,
                                           const BackupFilter& filter = BackupFilter{});

private:
    static FileEntry readFile(const std::string& baseDir,
                              const std::string& relativePath);
    static void attachMetadata(FileEntry& fe, const std::filesystem::path& fullPath);
    static FileMetadata readFileMetadata(const std::filesystem::path& fullPath);
};

} // namespace datasoftware

#endif // DATASOFTWARE_FILETRAVERSER_H