#ifndef DATASOFTWARE_FILETRAVERSER_H
#define DATASOFTWARE_FILETRAVERSER_H

#include <string>
#include <vector>
#include <functional>
#include "FileEntry.h"
#include "BackupEngine.h" // for ProgressCallback

namespace datasoftware {

class FileTraverser {
public:
    /// Traverse and read all files (no progress)
    static std::vector<FileEntry> traverse(const std::string& sourceDir);

    /// Traverse with progress callback
    static std::vector<FileEntry> traverse(const std::string& sourceDir,
                                           ProgressCallback progress);

private:
    static FileEntry readFile(const std::string& baseDir,
                              const std::string& relativePath);
};

} // namespace datasoftware

#endif // DATASOFTWARE_FILETRAVERSER_H