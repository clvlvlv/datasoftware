#ifndef DATASOFTWARE_ARCHIVEWRITER_H
#define DATASOFTWARE_ARCHIVEWRITER_H

#include <string>
#include <vector>
#include <fstream>
#include "FileEntry.h"

namespace datasoftware {

class ArchiveWriter {
public:
    static constexpr uint32_t ARCHIVE_VERSION = 3;
    static constexpr size_t STREAM_CHUNK = 4 * 1024 * 1024; // 4MB chunks

    // Write entries from in-memory data (small files)
    static void write(const std::string& archivePath,
                      const std::vector<FileEntry>& entries);

    // Write archive header (for streaming backup)
    static void writeHeader(std::ofstream& out, uint32_t entryCount);

    // Write a single file entry from disk to an already-open archive stream
    static void writeEntryStream(std::ofstream& out,
                                  const std::string& sourcePath,
                                  const std::string& archiveRelativePath,
                                  const FileMetadata& metadata);

    // Write a single file directly from disk (streaming, for large files)
    // Opens archive, writes header, streams file content, closes archive
    static void writeFile(const std::string& archivePath,
                          const std::string& sourcePath,
                          const std::string& archiveRelativePath,
                          const FileMetadata& metadata);

private:
    static void writeEntry(std::ofstream& out, const FileEntry& entry);
    static void writeTypePayload(std::ofstream& out, const FileEntry& entry);
    static void writeMetadata(std::ofstream& out, const FileMetadata& md);
};

} // namespace datasoftware

#endif // DATASOFTWARE_ARCHIVEWRITER_H