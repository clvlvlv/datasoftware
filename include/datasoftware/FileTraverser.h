#ifndef DATASOFTWARE_FILETRAVERSER_H
#define DATASOFTWARE_FILETRAVERSER_H

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include "FileEntry.h"
#include "BackupEngine.h"       // for ProgressCallback
#include "BackupFilter.h"

namespace datasoftware {

/**
 * @class FileTraverser
 * @brief 文件系统递归扫描器与元数据采集器
 * 
 * @details 利用 std::filesystem 进行深度优先搜索 (DFS)，识别 Regular, Symlink, 
 *          HardLink 等多种文件类型。通过 GetFileAttributesExW 采集 Windows 
 *          特有的 100ns 精度 FILETIME 时间戳。
 */
class FileTraverser {
public:
    /// @brief 轻量级文件信息结构（不含内容），用于流式备份以节省内存
    struct FileInfo {
        std::string relativePath;
        FileType    fileType     = FileType::Regular;
        uint64_t    fileSize     = 0;
        FileMetadata metadata;   // 包含 createTime, modTime, attributes 等
        std::string symlinkTarget;
        uint64_t    hardLinkId   = 0;
        uint32_t    deviceMajor  = 0;
        uint32_t    deviceMinor  = 0;
    };

    /// @brief 基础遍历：读取所有文件内容到内存（适用于小文件集）
    static std::vector<FileEntry> traverse(const std::string& sourceDir);

    /// @brief 带进度反馈的遍历
    static std::vector<FileEntry> traverse(const std::string& sourceDir,
                                           ProgressCallback progress);

    /// @brief 带过滤器的高级遍历
    static std::vector<FileEntry> traverse(const std::string& sourceDir,
                                           ProgressCallback progress,
                                           const BackupFilter& filter);

    /// @brief 仅列出文件信息而不读取内容（流式处理的核心）
    static std::vector<FileInfo> listFiles(const std::string& sourceDir,
                                           ProgressCallback progress = nullptr,
                                           const BackupFilter& filter = BackupFilter{});

private:
    static FileEntry readFile(const std::string& baseDir,
                              const std::string& relativePath);
    
    /**
     * @brief 附加高精度元数据
     * @note 使用 W 系列 API 确保对中文等非 ASCII 路径的正确支持
     */
    static void attachMetadata(FileEntry& fe, const std::filesystem::path& fullPath);
    static FileMetadata readFileMetadata(const std::filesystem::path& fullPath);
};

} // namespace datasoftware

#endif // DATASOFTWARE_FILETRAVERSER_H
