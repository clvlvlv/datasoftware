#include "datasoftware/BackupEngine.h"
#include "datasoftware/FileTraverser.h"
#include "datasoftware/ArchiveWriter.h"
#include "datasoftware/ArchiveReader.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>

namespace fs = std::filesystem;

namespace datasoftware {

// Helper: convert filesystem path to UTF-8 std::string
static std::string pathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    #if defined(__cpp_lib_char8_t) || (defined(_MSC_VER) && _MSVC_LANG >= 202002)
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
    #else
    return u8;
    #endif
}

/**
 * @brief 全量目录备份入口（带过滤器）
 * @details 实现分层架构的协同工作：
 *          - 数据采集层：通过 FileTraverser::traverse() 递归扫描源目录，
 *            收集所有文件/目录的元数据（大小、时间戳、属性等）。
 *          - 业务逻辑层：对采集的数据进行预处理（如添加根目录前缀以保持结构），
 *            并统计非目录项数量用于进度计算。
 *          - 数据访问层：委托 ArchiveWriter::write() 将内存中的 FileEntry 列表
 *            序列化并写入到指定的归档文件中。
 * 
 * @param sourceDir 源目录路径（UTF-8编码）
 * @param archivePath 输出归档文件路径
 * @param filter 备份过滤器，用于排除特定文件或目录
 * @param progress 进度回调函数，用于UI更新
 * @return 成功备份的文件数量（不包含目录）
 */
size_t BackupEngine::backup(const std::string& sourceDir,
                            const std::string& archivePath,
                            const BackupFilter& filter,
                            ProgressCallback progress) {
    if (progress) progress(0, 1, "Scanning directory...");
    
    // 【数据采集层】调用 FileTraverser 执行深度遍历，获取完整的文件清单
    std::vector<FileEntry> entries = FileTraverser::traverse(sourceDir, progress, filter);

    // 【路径结构保持】在相对路径前添加源目录名，确保恢复时能重建原始目录树
    // 避免不同源目录备份到同一归档时发生路径冲突
    {
        std::string rootName = pathToUtf8(fs::path(sourceDir).filename());
        if (!rootName.empty() && rootName != ".") {
            for (auto& e : entries) {
                e.relativePath = rootName + "/" + e.relativePath;
            }
        }
    }

    // 【业务逻辑层】统计待备份的文件总数（不含目录），用于精确的进度报告
    size_t fileCount = 0;
    for (const auto& entry : entries) {
        if (entry.fileType != FileType::Directory) {
            fileCount++;
        }
    }

    // 【数据访问层】触发归档写入操作，将内存中的文件条目持久化到磁盘
    if (progress) progress(0, fileCount, "Writing archive...");
    ArchiveWriter::write(archivePath, entries);

    if (progress) progress(fileCount, fileCount, "Backup complete!");
    return fileCount;
}

/**
 * @brief 简化版全量备份入口
 * @details 调用重载版本，使用默认的无过滤备份策略
 */
size_t BackupEngine::backup(const std::string& sourceDir,
                            const std::string& archivePath,
                            ProgressCallback progress) {
    return backup(sourceDir, archivePath, BackupFilter{}, progress);
}

size_t BackupEngine::backupFiles(const std::vector<std::string>& filePaths,
                                  const std::string& archivePath,
                                  ProgressCallback progress) {
    size_t total = filePaths.size();
    if (progress) progress(0, total, "Reading files...");

    std::vector<FileEntry> entries;
    entries.reserve(total);

    for (size_t i = 0; i < total; ++i) {
        const auto& fullPath = filePaths[i];
        fs::path p = fs::u8path(fullPath);
        std::string fileName;
        try { fileName = pathToUtf8(p.filename()); } catch (...) { fileName = p.filename().string(); }

        if (progress) progress(i, total, fileName);

        uint64_t size = fs::file_size(p);
        std::ifstream file(p, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + fileName);
        }
        std::vector<char> buf(size);
        if (size > 0) file.read(buf.data(), size);
        file.close();

        FileEntry fe(fileName, size, std::move(buf));

        // Read metadata using Windows API
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &info)) {
            fe.metadata.createTime = (static_cast<int64_t>(info.ftCreationTime.dwHighDateTime) << 32)
                                    | static_cast<int64_t>(info.ftCreationTime.dwLowDateTime);
            fe.metadata.modTime = (static_cast<int64_t>(info.ftLastWriteTime.dwHighDateTime) << 32)
                                 | static_cast<int64_t>(info.ftLastWriteTime.dwLowDateTime);
            fe.metadata.accessTime = (static_cast<int64_t>(info.ftLastAccessTime.dwHighDateTime) << 32)
                                    | static_cast<int64_t>(info.ftLastAccessTime.dwLowDateTime);
            fe.metadata.attributes = info.dwFileAttributes;
        }

        entries.push_back(std::move(fe));
    }

    if (progress) progress(total, total, "Writing archive...");
    ArchiveWriter::write(archivePath, entries);

    if (progress) progress(total, total, "Backup complete!");
    return total;
}

// ---- restore file metadata using Windows API ----

// ---- enable SE_RESTORE_NAME privilege for owner restoration ----
static bool enableRestorePrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken))
        return false;
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 2;
    if (!LookupPrivilegeValueW(nullptr,
                             L"SeTakeOwnershipPrivilege", &tp.Privileges[1].Luid)) { tp.Privileges[1].Luid.LowPart = 0; }
    if (!LookupPrivilegeValueW(nullptr, L"SeRestorePrivilege", &tp.Privileges[0].Luid)) {
        CloseHandle(hToken); return false;
    }
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    tp.Privileges[1].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
    CloseHandle(hToken);
    return ok && GetLastError() == ERROR_SUCCESS;
}

/**
 * @brief 恢复文件的元数据（时间戳、属性、所有者）
 * @details 此函数是实现"精确还原"的关键，利用 Windows 特有的高级API：
 *          - 使用 CreateFileW + FILE_FLAG_BACKUP_SEMANTICS 打开文件句柄，
 *            绕过正常的安全检查，允许设置受保护的属性。
 *          - 通过 SetFileTime() 精确恢复创建、最后访问、最后修改时间至100纳秒精度。
 *          - 通过 SetFileAttributesW() 恢复只读、隐藏、系统等文件属性。
 *          - 通过 SetNamedSecurityInfoW() 更改文件所有者，这需要先启用
 *            SE_RESTORE_NAME 和 SE_TAKE_OWNERSHIP_NAME 特权（由enableRestorePrivilege保证）。
 * 
 * @note 此操作可能失败，特别是更改所有者时，通常需要管理员权限。
 *       失败时会静默忽略，不影响主要文件内容的恢复。
 * 
 * @param filePath 目标文件或目录的完整路径
 * @param md 包含要恢复的元数据的对象
 */
static void restoreFileMetadata(const fs::path& filePath, const FileMetadata& md) {
    if (md.isEmpty()) return;

    // 获取文件句柄，FILE_FLAG_BACKUP_SEMANTICS 允许对目录执行此操作
    HANDLE hFile = CreateFileW(
        filePath.c_str(),
        FILE_WRITE_ATTRIBUTES,                    // 只需修改属性的权限
        FILE_SHARE_READ | FILE_SHARE_WRITE,      // 允许其他进程同时读写
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,               // 关键标志：支持目录和绕过安全检查
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) return;

    // 【时间戳还原】将存储的64位整数转换回Windows FILETIME结构（100纳秒为单位）
    if (md.createTime != 0 || md.modTime != 0 || md.accessTime != 0) {
        FILETIME create = {0}, access = {0}, write = {0};
        
        // 拆分64位时间戳为高低32位
        create.dwLowDateTime  = static_cast<DWORD>(md.createTime & 0xFFFFFFFF);
        create.dwHighDateTime = static_cast<DWORD>(md.createTime >> 32);
        access.dwLowDateTime  = static_cast<DWORD>(md.accessTime & 0xFFFFFFFF);
        access.dwHighDateTime = static_cast<DWORD>(md.accessTime >> 32);
        write.dwLowDateTime   = static_cast<DWORD>(md.modTime & 0xFFFFFFFF);
        write.dwHighDateTime  = static_cast<DWORD>(md.modTime >> 32);
        
        // 原子性地设置三个时间戳
        SetFileTime(hFile, &create, &access, &write);
    }

    CloseHandle(hFile); // 及时释放句柄

    // 【文件属性还原】直接应用从备份中读取的原始属性位掩码
    if (md.attributes != 0) {
        SetFileAttributesW(filePath.c_str(), md.attributes);
    }

    // 【所有权还原】高权限操作，尝试将文件所有者恢复为原始用户
    if (!md.owner.empty()) {
        std::wstring wOwner(md.owner.begin(), md.owner.end());
        BYTE sidBuf[SECURITY_MAX_SID_SIZE];     // 存储SID（安全标识符）的缓冲区
        DWORD sidLen = SECURITY_MAX_SID_SIZE;
        wchar_t domain[256];                    // 存储域名
        DWORD domainLen = 256;
        SID_NAME_USE peUse;                     // 接收账户类型的枚举值
        
        // 将用户名字符串解析为SID
        if (LookupAccountNameW(nullptr, wOwner.c_str(), (PSID)sidBuf, &sidLen,
                               domain, &domainLen, &peUse)) {
            std::wstring wPath = filePath.wstring();
            enableRestorePrivilege();           // 启用必要的特权
            // 设置新的所有者安全信息
            SetNamedSecurityInfoW(&wPath[0], SE_FILE_OBJECT,
                                  OWNER_SECURITY_INFORMATION,
                                  (PSID)sidBuf, nullptr, nullptr, nullptr);
        }
        // 注意：SetNamedSecurityInfoW 失败不会抛出异常，通常因权限不足
    }
}

// ---- streaming backup (avoids loading files into memory) ----
/**
 * @brief 流式备份（内存高效模式）
 * @details 设计用于处理超大文件或内存受限环境：
 *          - 第一阶段：仅列出文件路径和元数据（listFiles），不加载内容到内存。
 *          - 第二阶段：逐个打开文件流，直接从磁盘读取并写入归档，避免了
 *            将整个文件内容驻留在RAM中。这是一种典型的生产者-消费者模式。
 *          - 架构优势：极大地降低了内存占用峰值，代价是略微增加I/O操作。
 * 
 * @param sourceDir 源目录
 * @param archivePath 输出归档路径
 * @param progress 进度回调
 * @param filter 文件过滤器
 * @return 成功处理的文件数量
 */
size_t BackupEngine::backupStream(const std::string& sourceDir,
                                   const std::string& archivePath,
                                   ProgressCallback progress,
                                   const BackupFilter& filter) {
    // 【元数据采集】仅获取文件列表和头部信息，为流式处理做准备
    auto files = FileTraverser::listFiles(sourceDir, progress, filter);
    size_t total = files.size();
    if (progress) progress(0, total, "Writing archive...");

    // 【流式输出】打开归档文件进行二进制写入
    std::ofstream out(archivePath, std::ios::binary);
    if (!out.is_open())
        throw std::runtime_error("Cannot create archive: " + archivePath);

    // 写入归档头，声明条目总数
    ArchiveWriter::writeHeader(out, static_cast<uint32_t>(total));

    // 【核心循环】对每个文件执行流式复制
    for (size_t i = 0; i < total; ++i) {
        const auto& fi = files[i];
        if (progress) progress(i, total, fi.relativePath);

        // 构造完整源路径
        fs::path fullPath = fs::u8path(sourceDir) / fs::u8path(fi.relativePath);
        // 直接从源文件流式读取，并写入归档流，中间不经过大块内存缓冲
        ArchiveWriter::writeEntryStream(out, fullPath.string(),
                                         fi.relativePath, fi.metadata);
    }

    out.close();
    if (progress) progress(total, total, "Backup complete!");
    return total;
}

// ---- streaming restore (avoids loading files into memory) ----
/**
 * @brief 流式恢复（内存高效模式）
 * @details 与backupStream对应，实现低内存消耗的恢复：
 *          - 一次性读取归档头，获知总条目数。
 *          - 循环调用 extractNext()，每次从输入流中解析一个条目的头部，
 *            然后直接将其内容流式写入目标文件系统，处理完即释放内存。
 *          - 整个过程只需要常量级别的内存（O(1)），非常适合嵌入式或资源紧张环境。
 * 
 * @param archivePath 输入归档文件路径
 * @param restoreDir 恢复目标目录
 * @param progress 进度回调
 * @return 成功恢复的条目数量
 */
size_t BackupEngine::restoreStream(const std::string& archivePath,
                                    const std::string& restoreDir,
                                    ProgressCallback progress) {
    // 【流式输入】打开归档文件进行二进制读取
    std::ifstream in(archivePath, std::ios::binary);
    if (!in.is_open())
        throw std::runtime_error("Cannot open archive: " + archivePath);

    uint32_t entryCount = 0;
    uint32_t version = 0;
    // 读取归档头以确定后续解析逻辑
    ArchiveReader::readHeader(in, entryCount, version);

    if (progress) progress(0, entryCount, "Restoring...");

    // 确保恢复目录存在
    fs::path restorePath(restoreDir);
    fs::create_directories(restorePath);

    // 【核心循环】逐个提取并恢复条目
    for (uint32_t i = 0; i < entryCount; ++i) {
        if (progress) progress(i, entryCount, "");
        // 从流中提取下一个条目并直接写入磁盘
        ArchiveReader::extractNext(in, restoreDir, version);
    }

    in.close();
    if (progress) progress(entryCount, entryCount, "Restore complete!");
    return entryCount;
}

size_t BackupEngine::restore(const std::string& archivePath,
                             const std::string& restoreDir,
                             ProgressCallback progress) {
    std::vector<FileEntry> entries = ArchiveReader::read(archivePath);
    
    size_t total = 0;
    for (const auto& entry : entries) {
        if (entry.fileType != FileType::Directory) {
            total++;
        }
    }

    if (progress) progress(0, total, "Restoring files...");

    fs::path restorePath(restoreDir);
    fs::create_directories(restorePath);

    size_t restoredCount = 0;
    for (const auto& entry : entries) {
        if (entry.fileType == FileType::Directory) {
            fs::path filePath = restorePath / fs::u8path(entry.relativePath);
            fs::create_directories(filePath);
            restoreFileMetadata(filePath, entry.metadata);
            continue;
        }

        if (progress) progress(restoredCount, total, entry.relativePath);

        fs::path filePath = restorePath / fs::u8path(entry.relativePath);
        fs::create_directories(filePath.parent_path());

        switch (entry.fileType) {
        case FileType::Regular: {
            std::ofstream out(filePath, std::ios::binary);
            if (!out.is_open()) {
                throw std::runtime_error("Cannot create file: " + filePath.string());
            }
            if (entry.fileSize > 0) {
                out.write(entry.data.data(), entry.fileSize);
            }
            out.close();
            break;
        }
        case FileType::Symlink: {
            std::error_code ec;
            fs::remove(filePath, ec);
            fs::create_symlink(entry.symlinkTarget, filePath, ec);
            break;
        }
        case FileType::HardLink: {
            std::ofstream out(filePath, std::ios::binary);
            if (!out.is_open()) {
                throw std::runtime_error("Cannot create file: " + filePath.string());
            }
            if (entry.fileSize > 0) {
                out.write(entry.data.data(), entry.fileSize);
            }
            out.close();
            break;
        }
        case FileType::Fifo: {
            std::error_code ec;
            fs::remove(filePath, ec);
            #ifdef _WIN32
            std::ofstream out(filePath, std::ios::binary);
            out.close();
            #else
            fs::create_fifo(filePath, ec);
            #endif
            break;
        }
        case FileType::Device: {
            #ifndef _WIN32
            std::error_code ec;
            dev_t dev = makedev(entry.deviceMajor, entry.deviceMinor);
            fs::create_device(filePath, fs::status(filePath).permissions(),
                              dev, ec);
            #endif
            break;
        }
        default:
            break;
        }

        // Restore metadata after writing the file
        restoreFileMetadata(filePath, entry.metadata);
        restoredCount++;
    }

    if (progress) progress(total, total, "Restore complete!");
    return restoredCount;
}

} // namespace datasoftware
