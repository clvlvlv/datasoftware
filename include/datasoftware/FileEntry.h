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

struct FileEntry {
    std::string  relativePath;
    FileType     fileType     = FileType::Regular;
    uint64_t     fileSize     = 0;
    std::vector<char> data;

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