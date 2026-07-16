#ifndef DATASOFTWARE_BACKUPFILTER_H
#define DATASOFTWARE_BACKUPFILTER_H

#include <string>
#include <vector>
#include <cstdint>

namespace datasoftware {

/**
 * @struct BackupFilter
 * @brief 多维度备份过滤条件集合
 * 
 * @details 所有激活的过滤条件之间采用 **AND 逻辑** 组合。只有当文件同时满足
 *          所有非空条件时，才会被纳入备份范围。
 *          
 * 过滤维度包括以下9个方面：
 * 1. 包含路径 (includePaths) - 必须匹配至少一个包含路径模式
 * 2. 排除路径 (excludePaths) - 不能匹配任何排除路径模式
 * 3. 包含扩展名 (includeExts) - 必须匹配至少一个包含扩展名
 * 4. 排除扩展名 (excludeExts) - 不能匹配任何排除扩展名
 * 5. 名称模式 (namePattern) - 文件名必须包含指定子串（不区分大小写）
 * 6. 最小修改时间 (timeFrom) - 修改时间必须大于等于此值（0表示无限制）
 * 7. 最大修改时间 (timeTo) - 修改时间必须小于等于此值（0表示无限制）
 * 8. 最小文件大小 (minSize) - 文件大小必须大于等于此值（0表示无限制）
 * 9. 最大文件大小 (maxSize) - 文件大小必须小于等于此值（0表示无限制）
 * 10. 所有者用户名 (userName) - 文件所有者用户名必须包含指定子串
 */
struct BackupFilter {
    // ---- 路径过滤 (支持 glob 通配符) ----
    std::vector<std::string> includePaths;   // e.g. "docs/**"
    std::vector<std::string> excludePaths;   // e.g. "tmp/**", "*.log"

    // ---- 类型过滤 (基于扩展名) ----
    std::vector<std::string> includeExts;    // e.g. ".txt", ".jpg"
    std::vector<std::string> excludeExts;    // e.g. ".tmp", ".bak"

    // ---- 名称过滤 (子串匹配，不区分大小写) ----
    std::string namePattern;

    // ---- 时间过滤 (Unix 时间戳，秒级) ----
    int64_t timeFrom = 0;                    // 0 表示无下限
    int64_t timeTo   = 0;                    // 0 表示无上限

    // ---- 大小过滤 (字节单位) ----
    uint64_t minSize = 0;                    // 0 表示无下限
    uint64_t maxSize = 0;                    // 0 表示无上限

    // ---- 所有者过滤 ----
    std::string userName;                    // 用户名子串匹配

    /**
     * @brief 检查是否有任何过滤条件被激活
     * @return 如果至少有一个过滤条件被设置则返回true，否则返回false
     */
    bool isActive() const;

    /**
     * @brief 检查文件是否匹配所有激活的过滤条件
     * @param relativePath 相对路径
     * @param extension 扩展名
     * @param fileSize 文件大小（字节）
     * @param modTime 修改时间（Unix时间戳，秒）
     * @param owner 所有者用户名
     * @return 如果文件通过所有激活的过滤条件则返回true
     * 
     * @note 所有过滤条件之间是AND关系。只有当文件满足所有非空条件时才返回true。
     */
    bool matches(const std::string& relativePath,
                 const std::string& extension,
                 uint64_t fileSize,
                 int64_t modTime,
                 const std::string& owner) const;

    /**
     * @brief 获取活动过滤器的人类可读摘要
     * @return 包含所有激活过滤条件的描述字符串
     */
    std::string summary() const;

    /**
     * @brief 清除所有过滤条件
     * @note 将所有字段重置为默认值（空或0）
     */
    void clear();
};

} // namespace datasoftware

#endif // DATASOFTWARE_BACKUPFILTER_H