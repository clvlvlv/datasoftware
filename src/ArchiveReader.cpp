#include "datasoftware/ArchiveReader.h"
#include "datasoftware/ArchiveWriter.h"
#include <cstring>
#include <stdexcept>

namespace datasoftware {

std::vector<FileEntry> ArchiveReader::read(const std::string& archivePath) {
    std::ifstream in(archivePath, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Cannot open archive file: " + archivePath);
    }

    uint32_t entryCount = 0;
    uint32_t version = 0;
    readHeader(in, entryCount, version);

    std::vector<FileEntry> entries;
    entries.reserve(entryCount);

    for (uint32_t i = 0; i < entryCount; ++i) {
        if (version == 1) {
            // v1: just path + size + data (no file type)
            uint32_t pathLen;
            in.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
            std::string path(pathLen, '\0');
            in.read(&path[0], pathLen);
            uint64_t fileSize;
            in.read(reinterpret_cast<char*>(&fileSize), sizeof(fileSize));
            std::vector<char> data(fileSize);
            if (fileSize > 0) in.read(data.data(), fileSize);
            entries.emplace_back(std::move(path), fileSize, std::move(data));
        } else {
            // v2/v3: path + type + payload + (optional metadata in v3)
            auto fe = readEntryCommon(in);
            if (version >= 3) {
                readMetadata(in, fe.metadata);
            }
            entries.push_back(std::move(fe));
        }
    }

    return entries;
}

void ArchiveReader::readHeader(std::ifstream& in, uint32_t& entryCount,
                                uint32_t& version) {
    char magic[6];
    in.read(magic, 6);
    if (std::memcmp(magic, "DATASW", 6) != 0) {
        throw std::runtime_error("Invalid archive format (magic mismatch)");
    }

    uint16_t reserved;
    in.read(reinterpret_cast<char*>(&reserved), sizeof(reserved));

    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version < 1 || version > ArchiveWriter::ARCHIVE_VERSION) {
        throw std::runtime_error("Unsupported archive version: "
                                 + std::to_string(version));
    }

    in.read(reinterpret_cast<char*>(&entryCount), sizeof(entryCount));
}

FileEntry ArchiveReader::readEntryCommon(std::ifstream& in) {
    uint32_t pathLen;
    in.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
    std::string path(pathLen, '\0');
    in.read(&path[0], pathLen);

    uint8_t typeVal;
    in.read(reinterpret_cast<char*>(&typeVal), sizeof(typeVal));
    FileType type = static_cast<FileType>(typeVal);

    switch (type) {
    case FileType::Regular: {
        uint64_t fileSize;
        in.read(reinterpret_cast<char*>(&fileSize), sizeof(fileSize));
        std::vector<char> data(fileSize);
        if (fileSize > 0) in.read(data.data(), fileSize);
        return FileEntry(std::move(path), fileSize, std::move(data));
    }
    case FileType::Symlink: {
        uint32_t targetLen;
        in.read(reinterpret_cast<char*>(&targetLen), sizeof(targetLen));
        std::string target(targetLen, '\0');
        in.read(&target[0], targetLen);
        return FileEntry(std::move(path), FileType::Symlink, 0,
                         std::vector<char>{}, std::move(target));
    }
    case FileType::HardLink: {
        uint64_t fileSize;
        in.read(reinterpret_cast<char*>(&fileSize), sizeof(fileSize));
        std::vector<char> data(fileSize);
        if (fileSize > 0) in.read(data.data(), fileSize);
        uint64_t hId;
        in.read(reinterpret_cast<char*>(&hId), sizeof(hId));
        return FileEntry(std::move(path), FileType::HardLink, fileSize,
                         std::move(data), "", hId);
    }
    case FileType::Device: {
        uint32_t major, minor;
        in.read(reinterpret_cast<char*>(&major), sizeof(major));
        in.read(reinterpret_cast<char*>(&minor), sizeof(minor));
        return FileEntry(std::move(path), FileType::Device, 0,
                         std::vector<char>{}, "", 0, major, minor);
    }
    case FileType::Fifo:
        return FileEntry(std::move(path), FileType::Fifo, 0,
                         std::vector<char>{});
    case FileType::Directory:
        return FileEntry(std::move(path), FileType::Directory, 0,
                         std::vector<char>{});
    default:
        throw std::runtime_error("Unknown file type in archive: "
                                 + std::to_string(typeVal));
    }
}

void ArchiveReader::readMetadata(std::ifstream& in, FileMetadata& md) {
    in.read(reinterpret_cast<char*>(&md.createTime), sizeof(md.createTime));
    in.read(reinterpret_cast<char*>(&md.modTime), sizeof(md.modTime));
    in.read(reinterpret_cast<char*>(&md.accessTime), sizeof(md.accessTime));
    in.read(reinterpret_cast<char*>(&md.attributes), sizeof(md.attributes));

    uint16_t ownerLen;
    in.read(reinterpret_cast<char*>(&ownerLen), sizeof(ownerLen));
    md.owner.resize(ownerLen);
    if (ownerLen > 0) in.read(&md.owner[0], ownerLen);

    uint16_t groupLen;
    in.read(reinterpret_cast<char*>(&groupLen), sizeof(groupLen));
    md.group.resize(groupLen);
    if (groupLen > 0) in.read(&md.group[0], groupLen);
}

} // namespace datasoftware