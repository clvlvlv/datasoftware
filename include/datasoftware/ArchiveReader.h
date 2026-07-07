#ifndef DATASOFTWARE_ARCHIVEREADER_H
#define DATASOFTWARE_ARCHIVEREADER_H

#include <string>
#include <vector>
#include "FileEntry.h"

namespace datasoftware {

class ArchiveReader {
public:
    static std::vector<FileEntry> read(const std::string& archivePath);

private:
    static void readHeader(std::ifstream& in, uint32_t& entryCount, uint32_t& version);
    static FileEntry readEntryCommon(std::ifstream& in); // reads path+type+payload
    static void readMetadata(std::ifstream& in, FileMetadata& md);
};

} // namespace datasoftware

#endif // DATASOFTWARE_ARCHIVEREADER_H