#ifndef DATASOFTWARE_ARCHIVEWRITER_H
#define DATASOFTWARE_ARCHIVEWRITER_H

#include <string>
#include <vector>
#include <fstream>
#include "FileEntry.h"

namespace datasoftware {

class ArchiveWriter {
public:
    static constexpr uint32_t ARCHIVE_VERSION = 2;

    static void write(const std::string& archivePath,
                      const std::vector<FileEntry>& entries);

private:
    static void writeHeader(std::ofstream& out, uint32_t entryCount);
    static void writeEntry(std::ofstream& out, const FileEntry& entry);
    static void writeTypePayload(std::ofstream& out, const FileEntry& entry);
};

} // namespace datasoftware

#endif // DATASOFTWARE_ARCHIVEWRITER_H