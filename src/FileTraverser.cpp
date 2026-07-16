#include "datasoftware/FileTraverser.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <chrono>
#include <cctype>
#include <ctime>
#include <unordered_set>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>

namespace fs = std::filesystem;

namespace datasoftware {

/**
 * @brief 附加文件元数据到 FileEntry
 * @param fe 待填充的文件条目引用
 * @param fullPath 文件的绝对物理路径
 * @note 使用 GetFileAttributesExW 替代 stat，以获取 Windows 特有的 100ns 精度时间戳。
 */
void FileTraverser::attachMetadata(FileEntry& fe, const std::filesystem::path& fullPath) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    // 使用 W 系列 API 确保对中文等非 ASCII 路径的正确支持
    if (GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &info)) {
        // 打包 FILETIME：将 64 位整数存入 metadata 结构
        fe.metadata.createTime = (static_cast<int64_t>(info.ftCreationTime.dwHighDateTime) << 32)
                                | info.ftCreationTime.dwLowDateTime;
        fe.metadata.modTime = (static_cast<int64_t>(info.ftLastWriteTime.dwHighDateTime) << 32)
                             | info.ftLastWriteTime.dwLowDateTime;
        fe.metadata.accessTime = (static_cast<int64_t>(info.ftLastAccessTime.dwHighDateTime) << 32)
                                | info.ftLastAccessTime.dwLowDateTime;
        
        // 记录属性位掩码（Bitmask），用于恢复时的权限与状态还原
        fe.metadata.attributes = info.dwFileAttributes;
    }
}

/**
 * @brief 递归遍历目录树
 * @details 采用深度优先搜索 (DFS)，识别 Regular, Symlink, Directory 等多种文件类型。
 */
std::vector<FileEntry> FileTraverser::traverse(const std::string& sourceDir) {
    std::vector<FileEntry> result;
    fs::path base = fs::u8path(sourceDir);
    
    for (const auto& entry : fs::recursive_directory_iterator(base)) {
        FileEntry fe;
        fe.relativePath = fs::relative(entry.path(), base).u8string();
        
        // 根据文件系统状态识别文件类型
        if (entry.is_symlink()) fe.fileType = FileType::Symlink;
        else if (entry.is_directory()) fe.fileType = FileType::Directory;
        else fe.fileType = FileType::Regular;

        // 采集元数据并读取内容（针对小文件优化）
        attachMetadata(fe, entry.path());
        if (fe.fileType == FileType::Regular) {
            fe = readFile(sourceDir, fe.relativePath);
        }
        result.push_back(std::move(fe));
    }
    return result;
}

} // namespace datasoftware
