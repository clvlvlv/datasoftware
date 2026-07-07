#include "datasoftware/BackupFilter.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace datasoftware {

// ---- helpers ----

static bool globMatch(const std::string& str, const std::string& pattern) {
    // Simple glob: '*' matches any sequence, '?' matches one char
    size_t si = 0, pi = 0;
    size_t starPos = std::string::npos, matchPos = 0;

    while (si < str.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == str[si])) {
            ++si; ++pi;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            starPos = pi++;
            matchPos = si;
        } else if (starPos != std::string::npos) {
            pi = starPos + 1;
            si = ++matchPos;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

static bool subStrMatch(const std::string& str, const std::string& pattern) {
    if (pattern.empty()) return true;
    auto it = std::search(str.begin(), str.end(),
                          pattern.begin(), pattern.end(),
                          [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    return it != str.end();
}

static std::string getExtension(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    // Lowercase extension for comparison
    std::string ext = path.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    return ext;
}

static bool listMatch(const std::vector<std::string>& patterns,
                      const std::string& value) {
    if (patterns.empty()) return true;
    for (const auto& p : patterns) {
        if (globMatch(value, p)) return true;
    }
    return false;
}

// ---- BackupFilter ----

bool BackupFilter::isActive() const {
    return !includePaths.empty() || !excludePaths.empty() ||
           !includeExts.empty()   || !excludeExts.empty() ||
           !namePattern.empty()   ||
           timeFrom != 0 || timeTo != 0 ||
           minSize != 0  || maxSize != 0 ||
           !userName.empty();
}

bool BackupFilter::matches(const std::string& relativePath,
                            const std::string& extension,
                            uint64_t fileSize,
                            int64_t modTime,
                            const std::string& owner) const {
    // Path include
    if (!includePaths.empty() && !listMatch(includePaths, relativePath))
        return false;

    // Path exclude
    if (!excludePaths.empty() && listMatch(excludePaths, relativePath))
        return false;

    // Extension include
    if (!includeExts.empty()) {
        bool found = false;
        for (const auto& ext : includeExts) {
            std::string extLc = ext;
            for (auto& c : extLc) c = static_cast<char>(std::tolower(c));
            if (extension == extLc) { found = true; break; }
        }
        if (!found) return false;
    }

    // Extension exclude
    if (!excludeExts.empty()) {
        for (const auto& ext : excludeExts) {
            std::string extLc = ext;
            for (auto& c : extLc) c = static_cast<char>(std::tolower(c));
            if (extension == extLc) return false;
        }
    }

    // Name pattern
    if (!namePattern.empty() && !subStrMatch(relativePath, namePattern))
        return false;

    // Time range
    if (timeFrom != 0 && modTime < timeFrom) return false;
    if (timeTo != 0 && modTime > timeTo) return false;

    // Size range
    if (minSize != 0 && fileSize < minSize) return false;
    if (maxSize != 0 && fileSize > maxSize) return false;

    // User name
    if (!userName.empty() && !subStrMatch(owner, userName))
        return false;

    return true;
}

std::string BackupFilter::summary() const {
    std::ostringstream ss;
    bool first = true;
    auto add = [&](const std::string& s) {
        if (!first) ss << ", ";
        ss << s; first = false;
    };
    if (!includePaths.empty()) add("path in");
    if (!excludePaths.empty()) add("path out");
    if (!includeExts.empty())  add("ext in");
    if (!excludeExts.empty())  add("ext out");
    if (!namePattern.empty())  add("name");
    if (timeFrom != 0)         add("modified after");
    if (timeTo != 0)           add("modified before");
    if (minSize != 0)          add("min size");
    if (maxSize != 0)          add("max size");
    if (!userName.empty())     add("user");
    return first ? "no filters" : ss.str();
}

void BackupFilter::clear() {
    includePaths.clear();
    excludePaths.clear();
    includeExts.clear();
    excludeExts.clear();
    namePattern.clear();
    timeFrom = timeTo = 0;
    minSize = maxSize = 0;
    userName.clear();
}

} // namespace datasoftware