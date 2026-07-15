# 数据备份软件 (Data Backup Software)

基于 C++ 的跨平台数据备份工具，提供目录备份还原、文件压缩/加密、硬链接/软链接支持，附带 C++ Qt GUI 和 Python PyQt5 GUI。

---

## 功能特性

### 备份与还原
- 目录全量备份（保留目录结构、文件时间戳、属性、所有者）
- 按文件列表备份（选择多个文件）
- 支持硬链接检测与还原
- 支持软链接检测与还原
- 支持命名管道(FIFO)和设备文件(Device)
- 进度回调 + 加密打包

### 压缩与解压
- RLE、LZ77、Huffman 三种压缩算法
- 自动检测压缩算法（魔数 DSRL/DSLZ/DSHF）
- 目录压缩：先打包为备份归档，再压缩
- 文件压缩：直接压缩，输出 原文件名.扩展名
- 解压时自动识别归档格式(DATASW)并还原为目录或文件

### 加密与解密
- AES-256-CBC 加密
- 目录加密：先打包为备份归档，再加密
- 文件加密：直接加密，输出 原文件名.enc
- 解密时自动识别归档格式并还原

### 过滤器
- 按扩展名包含/排除
- 按文件名子串匹配
- 按路径包含/排除（glob 模式）
- 按文件大小范围（B/KB/MB 单位可选）
- 按修改时间范围
- 按文件所有者用户名

### 文件类型支持
- 普通文件 (Regular)
- 目录 (Directory)
- 软链接 (Symlink)
- 硬链接 (HardLink)
- 命名管道 (FIFO)
- 设备文件 (Device)

---

## 编译

### 依赖
- C++17 编译器（MSVC / MinGW GCC）
- CMake >= 3.10
- Qt5 (Widgets + Core)
- Python 3.12+ (PyQt5, PyInstaller)

### C++ CLI + GUI

```powershell
cd build
cmake ..
cmake --build . --target dsbackup --config Release      # CLI
cmake --build . --target dsbackup_gui --config Release   # C++ GUI
cmake --build . --target dsbackup_test --config Release  # 测试
```

### Python GUI

```powershell
cd gui_python
.\venv\Scripts\pyinstaller.exe DataBackup.spec --clean
```

打包后在 `dist\DataBackup.exe`。

---

## 使用说明

### CLI 命令行

```powershell
# 备份与还原
dsbackup backup D:\mydata D:\backup.dat          # 备份目录
dsbackup restore D:\backup.dat D:\restored        # 还原
dsbackup pack D:\a.txt D:\b.txt D:\packed.dat     # 打包多个文件
dsbackup unpack D:\packed.dat D:\out              # 解包

# 压缩与解压
dsbackup compress D:\mydir D:\mydir.rle 0         # 压缩目录(RLE)
dsbackup compress D:\doc.txt D:\doc.txt.rle 0     # 压缩文件
dsbackup decompress D:\mydir.rle D:\out 0         # 解压

# 加密与解密
dsbackup encrypt D:\mydir D:\mydir.enc mypassword # 加密目录
dsbackup encrypt D:\doc.txt D:\doc.txt.enc mypwd  # 加密文件
dsbackup decrypt D:\mydir.enc D:\out mypassword   # 解密
# 算法参数: 0=RLE, 1=LZ77, 2=Huffman
```

> 解压/解密时输出若为已存在的目录，文件会被自动还原到该目录中。
> 解压/解密时输出若为不存在的路径或文件路径，则作为单文件处理。

### 图形界面

**C++ GUI** (`build\dsbackup_gui.exe`) 或 **Python GUI** (`dist\DataBackup.exe`)：

| 标签页 | 功能 |
|--------|------|
| 备份与恢复 | 选择目录/文件 → 备份 → 还原 |
| 压缩 | 选文件/文件夹 → 选算法 → 输出压缩文件 |
| 解压 | 选压缩文件 → 选输出目录 → 自动解压(自动检测算法) |
| 加密 | 选文件/文件夹 → 输入密码 → 输出加密文件 |
| 解密 | 选加密文件 → 输入密码 → 选输出目录 → 自动解密 |

---

## 项目结构

```
datasoftware/
├── src/                    # C++ 源码
│   ├── main.cpp            # CLI 入口
│   ├── main_gui.cpp        # C++ GUI 入口
│   ├── gui/
│   │   ├── MainWindow.cpp  # C++ GUI 主窗口
│   │   └── MainWindow.h
│   ├── BackupEngine.cpp    # 备份/还原引擎
│   ├── FileTraverser.cpp   # 文件遍历(硬链接检测)
│   ├── FileEntry.cpp       # 文件条目
│   ├── ArchiveWriter.cpp   # 归档写入
│   ├── ArchiveReader.cpp   # 归档读取
│   ├── Compressor.cpp      # 压缩(RLE/LZ77/Huffman)
│   ├── Crypto.cpp          # 加密(AES-256-CBC)
│   └── BackupFilter.cpp    # 过滤器
├── include/datasoftware/   # 头文件
├── test/
│   └── BackupEngineTest.cpp # 58 个测试用例
├── gui_python/             # Python GUI
│   ├── main.py
│   └── gui/
│       ├── main_window.py  # 主窗口
│       ├── widgets.py      # 自定义控件
│       ├── workers.py      # 后台线程(subprocess调用CLI)
│       └── styles.py       # 主题样式
├── build/                  # C++ 构建输出
└── CMakeLists.txt
```

---

## 测试

```powershell
cd build
.\dsbackup_test.exe
```

覆盖以下模块（58 个测试用例）：
- 备份/还原完整流程
- 文件类型（硬链接、软链接、FIFO、设备文件）
- 过滤器（路径、扩展名、大小、时间、用户名）
- 压缩/解压（RLE、LZ77、Huffman 内存/文件级）
- 加密/解密（AES-256-CBC 轮转）
- 元数据（时间戳、属性）
- 边界条件（空目录、大文件、10MB、深嵌套、特殊字符路径、500 文件数）

---

## 技术细节

### 归档格式 (v3)
```
[DATASW] magic (6 bytes)
[reserved] (2 bytes)
[version] uint32_t
[entryCount] uint32_t
对于每个条目:
  [pathLength] uint32_t
  [path] UTF-8 string
  [fileType] uint8_t (Regular/Symlink/HardLink/Fifo/Device/Directory)
  [文件类型载荷] (文件内容/链接目标/设备号等)
  [metadata] (createTime/modTime/accessTime/attributes/owner/group)
```

### 压缩格式
```
[DSRL / DSLZ / DSHF] magic (4 bytes)
[压缩数据]
```

### 加密格式
```
[DSENC] magic (5 bytes)
[version] uint32_t
[salt] 16 bytes
[IV] 16 bytes
[encrypted data] variable
[HMAC-SHA256] 32 bytes
```

---

## License

内部项目。
