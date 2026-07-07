#ifndef DATASOFTWARE_BACKUPENGINE_H
#define DATASOFTWARE_BACKUPENGINE_H

#include <string>
#include <vector>
#include <functional>
#include "FileEntry.h"
#include "BackupFilter.h"

namespace datasoftware {

/// Progress callback: (current, total, currentFileMessage)
using ProgressCallback = std::function<void(size_t, size_t, const std::string&)>;

class BackupEngine {
public:
    // Backup an entire directory (preserves relative paths)
    static size_t backup(const std::string& sourceDir,
                         const std::string& archivePath,
                         ProgressCallback progress = nullptr);

    // Backup directory with custom filter
    static size_t backup(const std::string& sourceDir,
                         const std::string& archivePath,
                         const BackupFilter& filter,
                         ProgressCallback progress = nullptr);

    // Backup specific files (stores with filename in archive root)
    static size_t backupFiles(const std::vector<std::string>& filePaths,
                              const std::string& archivePath,
                              ProgressCallback progress = nullptr);

    static size_t restore(const std::string& archivePath,
                          const std::string& restoreDir,
                          ProgressCallback progress = nullptr);
};

} // namespace datasoftware

#endif // DATASOFTWARE_BACKUPENGINE_H