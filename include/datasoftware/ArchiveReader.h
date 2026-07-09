#ifndef DATASOFTWARE_ARCHIVEREADER_H
#define DATASOFTWARE_ARCHIVEREADER_H

#include <string>
#include <vector>
#include "FileEntry.h"

namespace datasoftware {

class ArchiveReader {
public:
    static constexpr size_t STREAM_CHUNK = 4 * 1024 * 1024; // 4MB chunks

    // Read all entries into memory (small files)
    static std::vector<FileEntry> read(const std::string& archivePath);

    // Extract a single entry to a file (streaming, for large files)
    static bool extractNext(std::ifstream& in, const std::string& outputDir,
                            uint32_t version);

    // Read archive header (for streaming restore)
    static void readHeader(std::ifstream& in, uint32_t& entryCount, uint32_t& version);

private:
    static FileEntry readEntryV1(std::ifstream& in);
    static FileEntry readEntryV2(std::ifstream& in);
    static void readMetadata(std::ifstream& in, FileMetadata& md);
};

} // namespace datasoftware

#endif // DATASOFTWARE_ARCHIVEREADER_H