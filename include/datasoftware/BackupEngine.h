#ifndef DATASOFTWARE_BACKUPENGINE_H
#define DATASOFTWARE_BACKUPENGINE_H

#include <string>
#include <vector>
#include <functional>
#include "FileEntry.h"
#include "BackupFilter.h"

namespace datasoftware {

/**
 * @brief 进度回调协议定义 (Progress Callback Protocol)
 * @param current 当前已处理的文件数量
 * @param total 待处理的文件总数
 * @param msg 实时状态消息（如正在处理的文件名）
 * @note 该协议用于驱动 GUI 进度条更新及 CLI 结构化输出 ([PROGRESS] 标记)
 */
using ProgressCallback = std::function<void(size_t, size_t, const std::string&)>;

/**
 * @class BackupEngine
 * @brief 智能备份引擎 —— 系统的业务逻辑中枢
 * 
 * @details 该模块负责协调 FileTraverser（数据采集）、BackupFilter（条件过滤）
 *          和 ArchiveWriter（持久化存储）。支持全量目录备份与选择性文件打包。
 *          采用“根目录名前缀”策略，确保恢复时能自动重建原始文件夹层级结构。
 */
class BackupEngine {
public:
    /**
     * @brief 全量目录备份入口
     * @param sourceDir 源目录绝对路径
     * @param archivePath 目标归档文件路径
     * @param progress 异步进度回调函数
     * @return 成功备份的文件数量
     */
    static size_t backup(const std::string& sourceDir,
                         const std::string& archivePath,
                         ProgressCallback progress = nullptr);

    /**
     * @brief 带过滤器的高级备份
     * @param filter 9维度过滤条件（扩展名、大小、时间等）
     */
    static size_t backup(const std::string& sourceDir,
                         const std::string& archivePath,
                         const BackupFilter& filter,
                         ProgressCallback progress = nullptr);

    /**
     * @brief 选择性文件打包（扁平化存储）
     * @details 仅保留文件名作为相对路径，适用于快速传输场景。
     */
    static size_t backupFiles(const std::vector<std::string>& filePaths,
                              const std::string& archivePath,
                              ProgressCallback progress = nullptr);

    /**
     * @brief 流式备份接口
     * @details 避免将所有文件内容加载到内存中，通过 4MB 分块读写支持 GB 级大文件。
     */
    static size_t backupStream(const std::string& sourceDir,
                               const std::string& archivePath,
                               ProgressCallback progress = nullptr,
                               const BackupFilter& filter = BackupFilter{});

    /**
     * @brief 流式恢复接口
     * @details 从归档中逐个提取条目并还原元数据（FILETIME 时间戳 + 属性位掩码）。
     */
    static size_t restoreStream(const std::string& archivePath,
                                const std::string& restoreDir,
                                ProgressCallback progress = nullptr);

    static size_t restore(const std::string& archivePath,
                          const std::string& restoreDir,
                          ProgressCallback progress = nullptr);
};

} // namespace datasoftware

#endif // DATASOFTWARE_BACKUPENGINE_H
