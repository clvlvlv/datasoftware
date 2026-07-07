#include "datasoftware/ArchiveWriter.h"
#include <cstring>
#include <stdexcept>

namespace datasoftware {

void ArchiveWriter::write(const std::string& archivePath,
                          const std::vector<FileEntry>& entries) {
    std::ofstream out(archivePath, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("Cannot create archive file: " + archivePath);
    }

    writeHeader(out, static_cast<uint32_t>(entries.size()));

    for (const auto& entry : entries) {
        writeEntry(out, entry);
    }

    out.close();
    if (!out) {
        throw std::runtime_error("Failed to write archive: " + archivePath);
    }
}

void ArchiveWriter::writeHeader(std::ofstream& out, uint32_t entryCount) {
    const char magic[] = "DATASW";
    out.write(magic, 6);

    uint16_t reserved = 0;
    out.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));

    uint32_t version = ARCHIVE_VERSION;
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));

    out.write(reinterpret_cast<const char*>(&entryCount), sizeof(entryCount));
}

void ArchiveWriter::writeEntry(std::ofstream& out, const FileEntry& entry) {
    // PathLength + Path
    uint32_t pathLen = static_cast<uint32_t>(entry.relativePath.size());
    out.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
    out.write(entry.relativePath.data(), pathLen);

    // FileType
    uint8_t type = static_cast<uint8_t>(entry.fileType);
    out.write(reinterpret_cast<const char*>(&type), sizeof(type));

    // Type-specific payload
    writeTypePayload(out, entry);

    // Metadata (v3+)
    writeMetadata(out, entry.metadata);
}

void ArchiveWriter::writeMetadata(std::ofstream& out, const FileMetadata& md) {
    out.write(reinterpret_cast<const char*>(&md.createTime), sizeof(md.createTime));
    out.write(reinterpret_cast<const char*>(&md.modTime), sizeof(md.modTime));
    out.write(reinterpret_cast<const char*>(&md.accessTime), sizeof(md.accessTime));
    out.write(reinterpret_cast<const char*>(&md.attributes), sizeof(md.attributes));

    uint16_t ownerLen = static_cast<uint16_t>(md.owner.size());
    out.write(reinterpret_cast<const char*>(&ownerLen), sizeof(ownerLen));
    if (ownerLen > 0) out.write(md.owner.data(), ownerLen);

    uint16_t groupLen = static_cast<uint16_t>(md.group.size());
    out.write(reinterpret_cast<const char*>(&groupLen), sizeof(groupLen));
    if (groupLen > 0) out.write(md.group.data(), groupLen);
}

void ArchiveWriter::writeTypePayload(std::ofstream& out, const FileEntry& entry) {
    switch (entry.fileType) {
    case FileType::Regular:
    case FileType::HardLink: {
        // FileSize + FileContent
        uint64_t size = entry.fileSize;
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
        if (size > 0) {
            out.write(entry.data.data(), size);
        }
        // Hard link ID (extra for HardLink)
        if (entry.fileType == FileType::HardLink) {
            uint64_t hId = entry.hardLinkId;
            out.write(reinterpret_cast<const char*>(&hId), sizeof(hId));
        }
        break;
    }
    case FileType::Symlink: {
        // TargetLength + SymlinkTarget
        uint32_t targetLen = static_cast<uint32_t>(entry.symlinkTarget.size());
        out.write(reinterpret_cast<const char*>(&targetLen), sizeof(targetLen));
        out.write(entry.symlinkTarget.data(), targetLen);
        break;
    }
    case FileType::Device: {
        // DeviceMajor + DeviceMinor
        out.write(reinterpret_cast<const char*>(&entry.deviceMajor), sizeof(entry.deviceMajor));
        out.write(reinterpret_cast<const char*>(&entry.deviceMinor), sizeof(entry.deviceMinor));
        break;
    }
    case FileType::Fifo:
    case FileType::Directory:
        // No extra payload
        break;
    }
}

} // namespace datasoftware