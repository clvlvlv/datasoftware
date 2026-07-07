#ifndef DATASOFTWARE_FILEENTRY_H
#define DATASOFTWARE_FILEENTRY_H

#include <string>
#include <vector>
#include <cstdint>

namespace datasoftware {

enum class FileType : uint8_t {
    Regular   = 0,
    Symlink   = 1,
    HardLink  = 2,
    Fifo      = 3,
    Device    = 4,
    Directory = 5
};

/// File metadata — timestamps use Windows FILETIME format
/// (100-ns intervals since 1601-01-01 UTC, 0 = unknown)
struct FileMetadata {
    int64_t  createTime  = 0;
    int64_t  modTime     = 0;
    int64_t  accessTime  = 0;
    uint32_t attributes  = 0;  // platform-specific
    std::string owner;
    std::string group;

    bool isEmpty() const {
        return createTime == 0 && modTime == 0 && accessTime == 0 &&
               attributes == 0 && owner.empty() && group.empty();
    }
};

struct FileEntry {
    std::string  relativePath;
    FileType     fileType     = FileType::Regular;
    uint64_t     fileSize     = 0;
    std::vector<char> data;
    FileMetadata metadata;

    // Symlink
    std::string  symlinkTarget;

    // Hard link (inode-like identifier from the source filesystem)
    uint64_t     hardLinkId   = 0;

    // Device file (major/minor numbers, Unix-specific)
    uint32_t     deviceMajor  = 0;
    uint32_t     deviceMinor  = 0;

    FileEntry() = default;

    // Regular file constructor
    FileEntry(std::string path, uint64_t size, std::vector<char> content)
        : relativePath(std::move(path))
        , fileType(FileType::Regular)
        , fileSize(size)
        , data(std::move(content)) {}

    // Full constructor
    FileEntry(std::string path, FileType type, uint64_t size,
              std::vector<char> content, std::string target = "",
              uint64_t hlinkId = 0, uint32_t devMajor = 0, uint32_t devMinor = 0)
        : relativePath(std::move(path))
        , fileType(type)
        , fileSize(size)
        , data(std::move(content))
        , symlinkTarget(std::move(target))
        , hardLinkId(hlinkId)
        , deviceMajor(devMajor)
        , deviceMinor(devMinor) {}
};

} // namespace datasoftware

#endif // DATASOFTWARE_FILEENTRY_H