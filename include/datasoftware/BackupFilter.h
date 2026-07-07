#ifndef DATASOFTWARE_BACKUPFILTER_H
#define DATASOFTWARE_BACKUPFILTER_H

#include <string>
#include <vector>
#include <cstdint>

namespace datasoftware {

/// Filter criteria for custom backup — only files matching ALL active filters
/// are included in the backup.
struct BackupFilter {
    // ---- Path ----
    std::vector<std::string> includePaths;   // glob patterns, e.g. "docs/**"
    std::vector<std::string> excludePaths;   // e.g. "tmp/**", "*.log"

    // ---- Type (extension) ----
    std::vector<std::string> includeExts;    // e.g. ".txt", ".jpg", ".pdf"
    std::vector<std::string> excludeExts;    // e.g. ".tmp", ".bak"

    // ---- Name ----
    std::string namePattern;                 // substring match (case-insensitive)

    // ---- Time (modification time, unix timestamp seconds) ----
    int64_t timeFrom = 0;                    // 0 = no limit
    int64_t timeTo   = 0;                    // 0 = no limit

    // ---- Size (bytes) ----
    uint64_t minSize = 0;                    // 0 = no limit
    uint64_t maxSize = 0;                    // 0 = no limit

    // ---- User ----
    std::string userName;                    // owner name (substring match)

    /// Returns true if any filter is active
    bool isActive() const;

    /// Returns true if a file passes ALL active filters
    bool matches(const std::string& relativePath,
                 const std::string& extension,
                 uint64_t fileSize,
                 int64_t modTime,
                 const std::string& owner) const;

    /// Return a human-readable summary of the active filters
    std::string summary() const;

    /// Clear all filters
    void clear();
};

} // namespace datasoftware

#endif // DATASOFTWARE_BACKUPFILTER_H