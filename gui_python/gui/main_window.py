"""
主窗口模块
"""

import os
import time
import ctypes
from PyQt5.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QFormLayout,
    QTabWidget, QGroupBox, QLabel, QLineEdit, QPushButton,
    QProgressBar, QFileDialog, QMessageBox, QTreeWidgetItem,
    QComboBox, QSpinBox, QDateTimeEdit, QFrame, QScrollArea
)
from PyQt5.QtCore import Qt, QThreadPool, QDateTime, QDate
from PyQt5.QtGui import QIcon

from .styles import StyleManager
from .widgets import (
    ModernButton, ModernLineEdit, ModernProgressBar,
    FilePreviewTree, SectionHeader, LogPanel
)
from .workers import BackupWorker, RestoreWorker, CompressWorker, EncryptWorker


class MainWindow(QMainWindow):
    """主窗口"""

    def __init__(self):
        super().__init__()
        self.setWindowTitle("📦 数据备份软件")
        self.setMinimumSize(900, 750)
        self.resize(1000, 800)

        StyleManager().apply(self)

        self._selected_files = []
        self._source_dir = ""
        self._operation_start_time = 0

        self._setup_ui()

    def _setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(12, 12, 12, 12)
        main_layout.setSpacing(8)

        self.tabs = QTabWidget()
        main_layout.addWidget(self.tabs)

        self._setup_backup_tab()
        self._setup_compression_tab()
        self._setup_decompress_tab()
        self._setup_decrypt_tab()
        self._setup_encryption_tab()

    def _elapsed_str(self):
        """返回从操作开始到现在的耗时字符串"""
        elapsed = time.time() - self._operation_start_time
        if elapsed < 1:
            return f"{elapsed*1000:.0f} ms"
        return f"{elapsed:.1f} 秒"

    # ================================================================
    #  备份与恢复标签页
    # ================================================================

    def _setup_backup_tab(self):
        tab = QWidget()
        outer_layout = QVBoxLayout(tab)
        outer_layout.setContentsMargins(0, 0, 0, 0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)

        container = QWidget()
        layout = QVBoxLayout(container)
        layout.setSpacing(10)
        layout.setContentsMargins(14, 14, 14, 14)

        # ---- 来源选择 ----
        source_group = QGroupBox("📂 选择备份来源")
        source_layout = QVBoxLayout(source_group)
        source_layout.addWidget(QLabel("请选择要备份的文件或文件夹："))

        btn_row = QHBoxLayout()
        self.source_label = QLabel("未选择任何文件")
        self.source_label.setStyleSheet("font-weight: bold; color: #89b4fa;")
        btn_row.addWidget(self.source_label, 1)

        add_folder_btn = ModernButton("📁 添加文件夹")
        add_folder_btn.clicked.connect(self._on_add_folder)
        btn_row.addWidget(add_folder_btn)

        add_files_btn = ModernButton("📄 添加文件")
        add_files_btn.clicked.connect(self._on_add_files)
        btn_row.addWidget(add_files_btn)

        clear_btn = ModernButton("🗑️ 清空", danger=True)
        clear_btn.clicked.connect(self._on_clear_files)
        btn_row.addWidget(clear_btn)

        source_layout.addLayout(btn_row)
        layout.addWidget(source_group)

        # ---- 文件预览 ----
        preview_group = QGroupBox("📋 文件列表")
        preview_layout = QVBoxLayout(preview_group)
        self.file_preview = FilePreviewTree()
        self.file_preview.setMinimumHeight(140)
        self.file_preview.setMaximumHeight(220)
        preview_layout.addWidget(self.file_preview)

        self.preview_summary = QLabel("")
        self.preview_summary.setStyleSheet("color: #a6adc8;")
        preview_layout.addWidget(self.preview_summary)
        layout.addWidget(preview_group)

        # ---- 过滤器 ----
        filter_group = QGroupBox("🔍 过滤器")
        filter_outer = QVBoxLayout(filter_group)

        filter_btn_row = QHBoxLayout()
        filter_toggle_btn = ModernButton("⚙️ 展开/收起过滤器")
        filter_toggle_btn.setCheckable(True)
        filter_toggle_btn.clicked.connect(self._on_toggle_filters)
        filter_btn_row.addWidget(filter_toggle_btn)
        clear_filter_btn = ModernButton("🗑️ 清空过滤器", danger=True)
        clear_filter_btn.clicked.connect(self._on_clear_filters)
        filter_btn_row.addWidget(clear_filter_btn)
        filter_btn_row.addStretch()
        filter_outer.addLayout(filter_btn_row)

        self.filter_panel = QWidget()
        filter_layout = QFormLayout(self.filter_panel)
        filter_layout.setContentsMargins(0, 8, 0, 4)
        filter_layout.setHorizontalSpacing(12)
        filter_layout.setVerticalSpacing(6)

        self.filter_ext = ModernLineEdit(".txt, .jpg, .pdf")
        filter_layout.addRow("扩展名:", self.filter_ext)

        self.filter_name = ModernLineEdit("文件名子串")
        filter_layout.addRow("文件名:", self.filter_name)

        self.filter_path_inc = ModernLineEdit("docs/**, **/data/*.csv")
        filter_layout.addRow("包含路径:", self.filter_path_inc)

        self.filter_path_exc = ModernLineEdit("tmp/**, *.log")
        filter_layout.addRow("排除路径:", self.filter_path_exc)

        size_row = QHBoxLayout()
        self.filter_min_size = QSpinBox()
        self.filter_min_size.setRange(0, 99999999)
        self.filter_min_size.setSpecialValueText("无限制")
        self.filter_max_size = QSpinBox()
        self.filter_max_size.setRange(0, 99999999)
        self.filter_max_size.setSpecialValueText("无限制")
        size_row.addWidget(QLabel("最小:"))
        size_row.addWidget(self.filter_min_size)
        size_row.addWidget(QLabel("最大:"))
        size_row.addWidget(self.filter_max_size)
        self.filter_size_unit = QComboBox()
        self.filter_size_unit.addItems(["B", "KB", "MB"])
        size_row.addWidget(self.filter_size_unit)
        size_row.addStretch()
        filter_layout.addRow("大小:", size_row)

        self.filter_user = ModernLineEdit("用户名")
        filter_layout.addRow("用户:", self.filter_user)

        apply_filter_btn = ModernButton("✅ 应用过滤器", primary=True)
        apply_filter_btn.clicked.connect(self._on_apply_filters)
        filter_layout.addRow("", apply_filter_btn)

        self.filter_panel.setVisible(False)
        filter_outer.addWidget(self.filter_panel)

        self.filter_label = QLabel("未设置过滤器")
        self.filter_label.setObjectName("filterLabel")
        filter_outer.addWidget(self.filter_label)

        layout.addWidget(filter_group)

        # ---- 加密 + 目标 并排 ----
        settings_row = QHBoxLayout()

        enc_group = QGroupBox("🔐 加密设置")
        enc_form = QFormLayout(enc_group)
        enc_form.setHorizontalSpacing(8)
        self.backup_password = ModernLineEdit("留空则不加密")
        self.backup_password.setEchoMode(QLineEdit.Password)
        enc_form.addRow("密码:", self.backup_password)
        settings_row.addWidget(enc_group)

        dest_group = QGroupBox("💾 备份保存位置")
        dest_layout = QVBoxLayout(dest_group)
        dest_row = QHBoxLayout()
        self.archive_path = ModernLineEdit("选择备份文件保存位置...")
        browse_archive_btn = ModernButton("📂 浏览")
        browse_archive_btn.clicked.connect(self._on_browse_archive)
        dest_row.addWidget(self.archive_path, 1)
        dest_row.addWidget(browse_archive_btn)
        dest_layout.addLayout(dest_row)
        settings_row.addWidget(dest_group)

        layout.addLayout(settings_row)

        # ---- 操作按钮 ----
        action_row = QHBoxLayout()
        self.backup_btn = ModernButton("🚀 开始备份", primary=True)
        self.backup_btn.clicked.connect(self._on_backup)
        self.backup_btn.setMinimumHeight(38)
        self.backup_btn.setMinimumWidth(160)
        action_row.addWidget(self.backup_btn)
        action_row.addStretch()
        layout.addLayout(action_row)

        # ---- 分隔线 ----
        line = QFrame()
        line.setFrameShape(QFrame.HLine)
        line.setStyleSheet("background-color: #45475a; max-height: 1px;")
        layout.addWidget(line)

        # ---- 恢复区域 ----
        restore_group = QGroupBox("🔄 恢复数据")
        restore_layout = QVBoxLayout(restore_group)
        restore_layout.setSpacing(8)

        restore_archive_row = QHBoxLayout()
        restore_archive_row.addWidget(QLabel("备份文件:"))
        self.restore_archive = ModernLineEdit("选择要恢复的备份文件...")
        browse_restore_btn = ModernButton("📂 浏览")
        browse_restore_btn.clicked.connect(self._on_browse_restore_archive)
        restore_archive_row.addWidget(self.restore_archive, 1)
        restore_archive_row.addWidget(browse_restore_btn)
        restore_layout.addLayout(restore_archive_row)

        restore_dir_row = QHBoxLayout()
        restore_dir_row.addWidget(QLabel("目标目录:"))
        self.restore_dir = ModernLineEdit("选择恢复目标目录...")
        browse_restore_dir_btn = ModernButton("📂 浏览")
        browse_restore_dir_btn.clicked.connect(self._on_browse_restore_dir)
        restore_dir_row.addWidget(self.restore_dir, 1)
        restore_dir_row.addWidget(browse_restore_dir_btn)
        restore_layout.addLayout(restore_dir_row)

        restore_pwd_row = QHBoxLayout()
        restore_pwd_row.addWidget(QLabel("密码:"))
        self.restore_password = ModernLineEdit("加密备份需要密码")
        self.restore_password.setEchoMode(QLineEdit.Password)
        restore_pwd_row.addWidget(self.restore_password, 1)
        restore_layout.addLayout(restore_pwd_row)

        restore_btn = ModernButton("🔄 开始恢复", success=True)
        restore_btn.clicked.connect(self._on_restore)
        restore_btn.setMinimumHeight(38)
        restore_btn.setMinimumWidth(160)
        restore_layout.addWidget(restore_btn)

        layout.addWidget(restore_group)

        # ---- 状态 ----
        status_group = QGroupBox("📊 状态")
        status_layout = QVBoxLayout(status_group)
        self.progress_bar = ModernProgressBar()
        status_layout.addWidget(self.progress_bar)
        self.status_label = QLabel("就绪")
        self.status_label.setObjectName("statusLabel")
        status_layout.addWidget(self.status_label)
        layout.addWidget(status_group)

        # ---- 日志面板 ----
        self.backup_log = LogPanel("📋 备份/恢复日志")
        layout.addWidget(self.backup_log)

        layout.addStretch()

        scroll.setWidget(container)
        outer_layout.addWidget(scroll)

        self.tabs.addTab(tab, "📦 备份与恢复")

    # ================================================================
    #  压缩标签页
    # ================================================================

    def _setup_compression_tab(self):
        tab = QWidget()
        outer_layout = QVBoxLayout(tab)
        outer_layout.setContentsMargins(0, 0, 0, 0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)

        container = QWidget()
        layout = QVBoxLayout(container)
        layout.setSpacing(10)
        layout.setContentsMargins(14, 14, 14, 14)

        algo_group = QGroupBox("⚙️ 压缩设置")
        algo_layout = QHBoxLayout(algo_group)
        algo_layout.addWidget(QLabel("压缩算法:"))
        self.algo_combo = QComboBox()
        self.algo_combo.addItems(["RLE (行程编码)", "LZ77 (滑动窗口)", "Huffman (哈夫曼)"])
        algo_layout.addWidget(self.algo_combo)
        algo_layout.addStretch()
        layout.addWidget(algo_group)

        io_group = QGroupBox("📁 文件设置")
        io_layout = QVBoxLayout(io_group)
        io_layout.setSpacing(8)

        input_row = QHBoxLayout()
        input_row.addWidget(QLabel("输入文件:"))
        self.comp_input = ModernLineEdit("选择要压缩的文件...")
        input_row.addWidget(self.comp_input, 1)
        browse_input_btn = ModernButton("📄 文件")
        browse_input_btn.clicked.connect(self._on_browse_comp_input)
        input_row.addWidget(browse_input_btn)
        browse_comp_folder_btn = ModernButton("📁 文件夹")
        browse_comp_folder_btn.clicked.connect(self._on_browse_comp_folder)
        input_row.addWidget(browse_comp_folder_btn)
        io_layout.addLayout(input_row)

        output_row = QHBoxLayout()
        output_row.addWidget(QLabel("输出文件:"))
        self.comp_output = ModernLineEdit("选择输出位置...")
        output_row.addWidget(self.comp_output, 1)
        browse_output_btn = ModernButton("📂 浏览")
        browse_output_btn.clicked.connect(self._on_browse_comp_output)
        output_row.addWidget(browse_output_btn)
        io_layout.addLayout(output_row)

        layout.addWidget(io_group)

        comp_btn_layout = QHBoxLayout()
        self.compress_btn = ModernButton("⬇️ 压缩", primary=True)
        self.compress_btn.clicked.connect(self._on_compress)
        self.compress_btn.setMinimumHeight(38)
        self.compress_btn.setMinimumWidth(140)
        comp_btn_layout.addWidget(self.compress_btn)

        layout.addLayout(comp_btn_layout)

        status_group = QGroupBox("📊 状态")
        status_layout = QVBoxLayout(status_group)
        self.comp_progress = ModernProgressBar()
        self.comp_progress.setVisible(False)
        status_layout.addWidget(self.comp_progress)
        self.comp_status = QLabel("就绪")
        self.comp_status.setObjectName("statusLabel")
        status_layout.addWidget(self.comp_status)
        layout.addWidget(status_group)

        self.comp_log = LogPanel("📋 压缩日志")
        layout.addWidget(self.comp_log)

        layout.addStretch()

        scroll.setWidget(container)
        outer_layout.addWidget(scroll)

        self.tabs.addTab(tab, "🗜️ 压缩")

    # ================================================================
    #  加密标签页
    # ================================================================


    # ================================================================
    #  解压标签页
    # ================================================================

    def _setup_decompress_tab(self):
        tab = QWidget()
        outer_layout = QVBoxLayout(tab)
        outer_layout.setContentsMargins(0, 0, 0, 0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)

        container = QWidget()
        layout = QVBoxLayout(container)
        layout.setSpacing(12)
        layout.setContentsMargins(14, 14, 14, 14)

        io_group = QGroupBox("📂 选择解压文件")
        io_layout = QVBoxLayout(io_group)
        io_layout.setSpacing(10)

        input_row = QHBoxLayout()
        input_row.addWidget(QLabel("压缩文件:"))
        self.decomp_input = ModernLineEdit("选择需要解压的文件...")
        input_row.addWidget(self.decomp_input, 1)
        decomp_browse_btn = ModernButton("📄 浏览")
        decomp_browse_btn.clicked.connect(self._on_browse_decomp_input)
        input_row.addWidget(decomp_browse_btn)
        io_layout.addLayout(input_row)

        output_row = QHBoxLayout()
        output_row.addWidget(QLabel("解压到:"))
        self.decomp_output = ModernLineEdit("选择解压目标目录...")
        output_row.addWidget(self.decomp_output, 1)
        decomp_out_folder_btn = ModernButton("📁 浏览")
        decomp_out_folder_btn.clicked.connect(self._on_browse_decomp_output)
        output_row.addWidget(decomp_out_folder_btn)
        io_layout.addLayout(output_row)

        layout.addWidget(io_group)

        self.decompress_btn = ModernButton("📦 开始解压", success=True)
        self.decompress_btn.setMinimumHeight(38)
        self.decompress_btn.clicked.connect(self._on_decompress)
        layout.addWidget(self.decompress_btn)

        status_group = QGroupBox("📳 状态")
        status_layout = QVBoxLayout(status_group)
        self.decomp_progress = ModernProgressBar()
        status_layout.addWidget(self.decomp_progress)
        self.decomp_status = QLabel("就绪")
        self.decomp_status.setObjectName("statusLabel")
        status_layout.addWidget(self.decomp_status)
        layout.addWidget(status_group)

        self.decomp_log = LogPanel("📰 解压日志")
        layout.addWidget(self.decomp_log)

        layout.addStretch()

        scroll.setWidget(container)
        outer_layout.addWidget(scroll)

        self.tabs.addTab(tab, "🖰️ 解压")


    def _setup_decrypt_tab(self):
        tab = QWidget()
        outer_layout = QVBoxLayout(tab)
        outer_layout.setContentsMargins(0, 0, 0, 0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)

        container = QWidget()
        layout = QVBoxLayout(container)
        layout.setSpacing(12)
        layout.setContentsMargins(14, 14, 14, 14)

        io_group = QGroupBox("🔐 选择解密文件")
        io_layout = QVBoxLayout(io_group)
        io_layout.setSpacing(10)

        input_row = QHBoxLayout()
        input_row.addWidget(QLabel("加密文件:"))
        self.decrypt_input = ModernLineEdit("选择需要解密的文件...")
        input_row.addWidget(self.decrypt_input, 1)
        decrypt_browse_btn = ModernButton("📄 浏览")
        decrypt_browse_btn.clicked.connect(self._on_browse_decrypt_input)
        input_row.addWidget(decrypt_browse_btn)
        io_layout.addLayout(input_row)

        pwd_row = QHBoxLayout()
        pwd_row.addWidget(QLabel("密码:"))
        self.decrypt_password = ModernLineEdit("输入密码...")
        self.decrypt_password.setEchoMode(QLineEdit.Password)
        pwd_row.addWidget(self.decrypt_password, 1)
        io_layout.addLayout(pwd_row)

        output_row = QHBoxLayout()
        output_row.addWidget(QLabel("解密到:"))
        self.decrypt_output = ModernLineEdit("选择解密目标目录...")
        output_row.addWidget(self.decrypt_output, 1)
        decrypt_folder_btn = ModernButton("📁 浏览")
        decrypt_folder_btn.clicked.connect(self._on_browse_decrypt_output)
        output_row.addWidget(decrypt_folder_btn)
        io_layout.addLayout(output_row)

        layout.addWidget(io_group)

        self.decrypt_btn = ModernButton("🔐 开始解密", success=True)
        self.decrypt_btn.setMinimumHeight(38)
        self.decrypt_btn.clicked.connect(self._on_decrypt)
        layout.addWidget(self.decrypt_btn)

        status_group = QGroupBox("📳 状态")
        status_layout = QVBoxLayout(status_group)
        self.decrypt_progress = ModernProgressBar()
        status_layout.addWidget(self.decrypt_progress)
        self.decrypt_status = QLabel("就绪")
        self.decrypt_status.setObjectName("statusLabel")
        status_layout.addWidget(self.decrypt_status)
        layout.addWidget(status_group)

        self.decrypt_log = LogPanel("📰 解密日志")
        layout.addWidget(self.decrypt_log)

        layout.addStretch()

        scroll.setWidget(container)
        outer_layout.addWidget(scroll)

        self.tabs.addTab(tab, "🔐 解密")

    def _setup_encryption_tab(self):
        tab = QWidget()
        outer_layout = QVBoxLayout(tab)
        outer_layout.setContentsMargins(0, 0, 0, 0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)

        container = QWidget()
        layout = QVBoxLayout(container)
        layout.setSpacing(10)
        layout.setContentsMargins(14, 14, 14, 14)

        io_group = QGroupBox("📁 文件设置")
        io_layout = QVBoxLayout(io_group)
        io_layout.setSpacing(8)

        input_row = QHBoxLayout()
        input_row.addWidget(QLabel("输入文件:"))
        self.enc_input = ModernLineEdit("选择要加密/解密的文件...")
        input_row.addWidget(self.enc_input, 1)
        browse_enc_input_btn = ModernButton("📄 文件")
        browse_enc_input_btn.clicked.connect(self._on_browse_enc_input)
        input_row.addWidget(browse_enc_input_btn)
        browse_enc_folder_btn = ModernButton("📁 文件夹")
        browse_enc_folder_btn.clicked.connect(self._on_browse_enc_folder)
        input_row.addWidget(browse_enc_folder_btn)
        io_layout.addLayout(input_row)

        output_row = QHBoxLayout()
        output_row.addWidget(QLabel("输出文件:"))
        self.enc_output = ModernLineEdit("选择输出位置...")
        output_row.addWidget(self.enc_output, 1)
        browse_enc_output_btn = ModernButton("📂 浏览")
        browse_enc_output_btn.clicked.connect(self._on_browse_enc_output)
        output_row.addWidget(browse_enc_output_btn)
        io_layout.addLayout(output_row)

        layout.addWidget(io_group)

        pwd_group = QGroupBox("🔑 密码设置")
        pwd_layout = QHBoxLayout(pwd_group)
        pwd_layout.addWidget(QLabel("密码:"))
        self.enc_password = ModernLineEdit("请输入密码")
        self.enc_password.setEchoMode(QLineEdit.Password)
        pwd_layout.addWidget(self.enc_password, 1)
        layout.addWidget(pwd_group)

        enc_btn_layout = QHBoxLayout()
        self.encrypt_btn = ModernButton("🔒 加密", primary=True)
        self.encrypt_btn.clicked.connect(self._on_encrypt)
        self.encrypt_btn.setMinimumHeight(38)
        self.encrypt_btn.setMinimumWidth(140)
        enc_btn_layout.addWidget(self.encrypt_btn)

        layout.addLayout(enc_btn_layout)

        status_group = QGroupBox("📊 状态")
        status_layout = QVBoxLayout(status_group)
        self.enc_progress = ModernProgressBar()
        self.enc_progress.setVisible(False)
        status_layout.addWidget(self.enc_progress)
        self.enc_status = QLabel("就绪")
        self.enc_status.setObjectName("statusLabel")
        status_layout.addWidget(self.enc_status)
        layout.addWidget(status_group)

        self.enc_log = LogPanel("📋 加密日志")
        layout.addWidget(self.enc_log)

        layout.addStretch()

        scroll.setWidget(container)
        outer_layout.addWidget(scroll)

        self.tabs.addTab(tab, "🔐 加密")

    # ================================================================
    #  备份相关槽函数
    # ================================================================

    def _on_add_folder(self):
        dir_path = QFileDialog.getExistingDirectory(self, "选择源目录")
        if dir_path:
            self._source_dir = dir_path
            self._selected_files = []
            self._refresh_preview()
            self.backup_log.log(f"已选择目录: {dir_path}", "info")

    def _on_add_files(self):
        files, _ = QFileDialog.getOpenFileNames(self, "选择文件")
        if files:
            self._source_dir = ""
            self._selected_files.extend(files)
            self._refresh_preview()
            self.backup_log.log(f"已添加 {len(files)} 个文件", "info")

    def _on_clear_files(self):
        self._source_dir = ""
        self._selected_files = []
        self._refresh_preview()
        self.backup_log.log("已清空文件列表", "warning")

    def _refresh_preview(self):
        self.file_preview.clear_items()

        total_size = 0
        if self._source_dir:
            self.source_label.setText(f"📁 目录: {self._source_dir}")
            for root, dirs, files in os.walk(self._source_dir):
                    for file in files[:100]:
                        file_path = os.path.join(root, file)
                        if not self._should_show_file(file_path):
                            continue
                        rel_path = os.path.relpath(file_path, self._source_dir)
                        size = os.path.getsize(file_path) if os.path.exists(file_path) else 0
                        self.file_preview.add_file(rel_path, self._detect_file_type(file_path), size)
                        total_size += size
        elif self._selected_files:
            self.source_label.setText(f"📄 已选择 {len(self._selected_files)} 个文件")
            for path in self._selected_files:
                name = os.path.basename(path)
                size = os.path.getsize(path) if os.path.exists(path) else 0
                self.file_preview.add_file(name, self._detect_file_type(path), size)
                total_size += size
        else:
            self.source_label.setText("未选择任何文件")

        if total_size > 0:
            self.preview_summary.setText(
                f"总计: {self.file_preview.topLevelItemCount()} 个项目, "
                f"{self._format_size(total_size)}"
            )
        else:
            self.preview_summary.setText("")

    def _detect_file_type(self, file_path):
        """检测文件的真实类型"""
        if os.path.islink(file_path):
            return "Symlink"
        try:
            INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
            kernel32 = ctypes.windll.kernel32
            h_file = kernel32.CreateFileW(file_path, 0x80000000, 3, None, 3, 0x02000000, None)
            if h_file != INVALID_HANDLE_VALUE:
                info = (ctypes.c_ubyte * 52)()
                if kernel32.GetFileInformationByHandle(h_file, ctypes.byref(info)):
                    nlink = ctypes.c_uint32.from_buffer(info, 40).value
                    if nlink > 1:
                        kernel32.CloseHandle(h_file)
                        return "HardLink"
                kernel32.CloseHandle(h_file)
        except Exception:
            pass
        return "文件"


    def _get_file_owner(self, file_path):
        """获取文件所有者名称（Windows API）"""
        try:
            advapi32 = ctypes.windll.advapi32
            psd = ctypes.c_void_p()
            ret = advapi32.GetNamedSecurityInfoW(
                file_path, 1, 1, ctypes.byref(psd), None, None, None
            )
            if ret != 0 or not psd.value:
                return ""
            pOwner = ctypes.c_void_p()
            if not advapi32.GetSecurityDescriptorOwner(psd, ctypes.byref(pOwner), None):
                return ""
            lpName = ctypes.create_unicode_buffer(256)
            cchName = ctypes.c_uint32(256)
            lpDomain = ctypes.create_unicode_buffer(256)
            cchDomain = ctypes.c_uint32(256)
            peUse = ctypes.c_int()
            if advapi32.LookupAccountSidW(None, pOwner, lpName, ctypes.byref(cchName),
                                           lpDomain, ctypes.byref(cchDomain), ctypes.byref(peUse)):
                return lpName.value
        except Exception:
            pass
        return ""

    def _get_filter_exts(self):
        raw = self.filter_ext.text().strip()
        if not raw:
            return None
        exts = []
        for e in raw.split(","):
            e = e.strip().lower()
            if e and not e.startswith("."):
                e = "." + e
            if e:
                exts.append(e)
        return exts if exts else None

    def _should_show_file(self, file_path):
        filter_exts = self._get_filter_exts()
        if filter_exts:
            _, ext = os.path.splitext(file_path)
            if ext.lower() not in filter_exts:
                return False

        name_filter = self.filter_name.text().strip().lower()
        if name_filter and name_filter not in os.path.basename(file_path).lower():
            return False

        path_inc = self.filter_path_inc.text().strip().lower()
        if path_inc and path_inc not in file_path.lower():
            return False

        path_exc = self.filter_path_exc.text().strip().lower()
        if path_exc and path_exc in file_path.lower():
            return False

        # 大小过滤
        min_val = self.filter_min_size.value()
        max_val = self.filter_max_size.value()
        if min_val > 0 or max_val > 0:
            try:
                size = os.path.getsize(file_path)
                idx = self.filter_size_unit.currentIndex()
                mult = [1, 1024, 1024 * 1024][idx]
                size_in_unit = size / mult
                if min_val > 0 and size_in_unit < min_val:
                    return False
                if max_val > 0 and size_in_unit > max_val:
                    return False
            except OSError:
                pass

        # 用户名过滤
        user_filter = self.filter_user.text().strip().lower()
        if user_filter:
            owner = self._get_file_owner(file_path).lower()
            if user_filter not in owner:
                return False

        return True


    def _format_size(self, size):
        if size >= 1024**3:
            return f"{size/1024**3:.2f} GB"
        elif size >= 1024**2:
            return f"{size/1024**2:.2f} MB"
        elif size >= 1024:
            return f"{size/1024:.1f} KB"
        return f"{size} B"

    def _on_toggle_filters(self, checked):
        self.filter_panel.setVisible(checked)

    def _on_apply_filters(self):
        filters = []
        if self.filter_ext.text().strip():
            filters.append(f"扩展名: {self.filter_ext.text()}")
        if self.filter_name.text().strip():
            filters.append(f"文件名: {self.filter_name.text()}")
        if self.filter_path_inc.text().strip():
            filters.append(f"包含路径: {self.filter_path_inc.text()}")
        if self.filter_path_exc.text().strip():
            filters.append(f"排除路径: {self.filter_path_exc.text()}")
        if self.filter_min_size.value() > 0 or self.filter_max_size.value() > 0:
            unit = self.filter_size_unit.currentText()
            min_s = self.filter_min_size.value()
            max_s = self.filter_max_size.value()
            parts = []
            if min_s > 0: parts.append(f"最小 {min_s}{unit}")
            if max_s > 0: parts.append(f"最大 {max_s}{unit}")
            filters.append("大小: " + " ".join(parts))
        if self.filter_user.text().strip():
            filters.append(f"用户: {self.filter_user.text()}")

        if filters:
            self.filter_label.setText(" | ".join(filters))
            self.backup_log.log(f"已应用过滤器: {' | '.join(filters)}", "info")
        else:
            self.filter_label.setText("未设置过滤器")

        self._refresh_preview()


    def _on_clear_filters(self):
        """清空所有过滤条件"""
        self.filter_ext.clear()
        self.filter_name.clear()
        self.filter_path_inc.clear()
        self.filter_path_exc.clear()
        self.filter_min_size.setValue(0)
        self.filter_max_size.setValue(0)
        self.filter_user.clear()
        self.filter_label.setText("未设置过滤器")
        self.filter_panel.setVisible(False)
        self.backup_log.log("已清空所有过滤器", "warning")
        self._refresh_preview()

    def _on_browse_archive(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "选择备份文件", "", "备份文件 (*.dat);;所有文件 (*)"
        )
        if path:
            self.archive_path.setText(path)
            self.backup_log.log(f"备份目标: {path}", "info")

    def _on_browse_restore_archive(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "选择备份文件", "", "备份文件 (*.dat);;所有文件 (*)"
        )
        if path:
            self.restore_archive.setText(path)

    def _on_browse_restore_dir(self):
        path = QFileDialog.getExistingDirectory(self, "选择恢复目标目录")
        if path:
            self.restore_dir.setText(path)

    def _has_active_filters(self):
        if self.filter_ext.text().strip():
            return True
        if self.filter_name.text().strip():
            return True
        if self.filter_path_inc.text().strip():
            return True
        if self.filter_path_exc.text().strip():
            return True
        if self.filter_min_size.value() > 0 or self.filter_max_size.value() > 0:
            return True
        if self.filter_user.text().strip():
            return True
        return False

    def _on_backup(self):
        dest = self.archive_path.text().strip()
        if not dest:
            QMessageBox.warning(self, "提示", "请选择备份文件保存位置")
            return

        if not self._source_dir and not self._selected_files:
            QMessageBox.warning(self, "提示", "请选择要备份的文件或文件夹")
            return

        self._operation_start_time = time.time()
        self.backup_btn.setEnabled(False)
        self.progress_bar.setValue(0)
        self.status_label.setText("正在备份...")
        self.backup_log.log(f"开始备份 → {dest}", "info")

        source = self._source_dir or ""
        files_to_backup = self._selected_files
        backup_type = "files" if self._selected_files else "folder"

        if source and self._has_active_filters():
            backup_type = "files"
            files_to_backup = []
            try:
                for root, dirs, files in os.walk(source):
                    for file in files:
                        file_path = os.path.join(root, file)
                        if self._should_show_file(file_path):
                            files_to_backup.append(file_path)
            except Exception as e:
                self.backup_log.log(f"扫描目录失败: {e}", "error")
                self.backup_btn.setEnabled(True)
                return

        self.worker = BackupWorker(
            source,
            dest,
            self.backup_password.text(),
            backup_type,
            files_to_backup
        )
        self.worker.progress.connect(self._on_backup_progress)
        self.worker.log_message.connect(self.backup_log.log)
        self.worker.finished.connect(self._on_backup_finished)
        self.worker.start()

    def _on_backup_progress(self, current, total, message):
        self.progress_bar.setProgress(current, total)
        self.status_label.setText(f"正在备份... ({current}/{total})")

    def _on_backup_finished(self, success, message, count):
        elapsed = self._elapsed_str()
        self.backup_btn.setEnabled(True)
        if success:
            self.progress_bar.setValue(100)
            self.status_label.setText(f"备份完成 — {count} 个文件，耗时 {elapsed}")
            self.backup_log.log(f"备份完成！共 {count} 个文件，耗时 {elapsed}", "success")
            QMessageBox.information(self, "备份成功", f"{message}\n共 {count} 个文件，耗时 {elapsed}")
        else:
            self.progress_bar.setValue(0)
            self.status_label.setText(f"备份失败 — 耗时 {elapsed}")
            self.backup_log.log(f"备份失败: {message}，耗时 {elapsed}", "error")
            QMessageBox.critical(self, "备份失败", message)

    def _on_restore(self):
        source = self.restore_archive.text().strip()
        dest = self.restore_dir.text().strip()

        if not source:
            QMessageBox.warning(self, "提示", "请选择要恢复的备份文件")
            return
        if not dest:
            QMessageBox.warning(self, "提示", "请选择恢复目标目录")
            return

        self._operation_start_time = time.time()
        self.status_label.setText("正在恢复...")
        self.progress_bar.setValue(0)
        self.backup_log.log(f"开始恢复: {source} → {dest}", "info")

        self.restore_worker = RestoreWorker(source, dest, self.restore_password.text())
        self.restore_worker.progress.connect(self._on_restore_progress)
        self.restore_worker.log_message.connect(self.backup_log.log)
        self.restore_worker.finished.connect(self._on_restore_finished)
        self.restore_worker.start()

    def _on_restore_progress(self, current, total, message):
        self.progress_bar.setProgress(current, total)
        self.status_label.setText(f"正在恢复... ({current}/{total})")

    def _on_restore_finished(self, success, message, count):
        elapsed = self._elapsed_str()
        if success:
            self.progress_bar.setValue(100)
            self.status_label.setText(f"恢复完成 — {count} 个文件，耗时 {elapsed}")
            self.backup_log.log(f"恢复完成！共 {count} 个文件，耗时 {elapsed}", "success")
            QMessageBox.information(self, "恢复成功", f"{message}\n共 {count} 个文件，耗时 {elapsed}")
        else:
            self.progress_bar.setValue(0)
            self.status_label.setText(f"恢复失败 — 耗时 {elapsed}")
            self.backup_log.log(f"恢复失败: {message}，耗时 {elapsed}", "error")
            QMessageBox.critical(self, "恢复失败", message)

    # ================================================================
    #  压缩相关槽函数
    # ================================================================

    def _on_browse_comp_input(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择输入文件", "", "所有文件 (*);;压缩文件 (*.rle *.lz77 *.huff)")
        if path:
            self.comp_input.setText(path)

    def _on_browse_comp_output_folder(self):
        path = QFileDialog.getExistingDirectory(self, "选择输出目录")
        if path:
            self.comp_output.setText(path)

    def _on_browse_comp_output(self):
        algo_exts = [".rle", ".lz77", ".huff"]
        ext = algo_exts[self.algo_combo.currentIndex()]
        default_name = f"输出{ext}"
        input_file = self.comp_input.text().strip()
        if input_file and os.path.isfile(input_file):
            base = os.path.basename(input_file)
            if base:
                default_name = base + ext
        path, _ = QFileDialog.getSaveFileName(self, "选择输出文件", default_name, f"{ext.upper()}压缩文件 (*{ext});;所有文件 (*)")
        if path:
            self.comp_output.setText(path)


    def _on_browse_decomp_input(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择需要解压的文件",
                                              "", "所有文件 (*);;压缩文件 (*.rle *.lz77 *.huff)")
        if path:
            self.decomp_input.setText(path)

    def _on_browse_decomp_output(self):
        path = QFileDialog.getExistingDirectory(self, "选择解压目标目录")
        if path:
            self.decomp_output.setText(path)

    def _on_decompress(self):
        input_file = self.decomp_input.text().strip()
        output_dir = self.decomp_output.text().strip()
        if not input_file:
            QMessageBox.warning(self, "提示", "请选择需要解压的文件")
            return
        if not output_dir:
            QMessageBox.warning(self, "提示", "请选择解压目标目录")
            return

        self.decompress_btn.setEnabled(False)
        self.decomp_progress.setProgress(0, 1)
        self.decomp_status.setText("正在解压...")
        self.decomp_log.log(f"开始解压: {input_file} → {output_dir}", "info")

        self.decomp_worker = CompressWorker(input_file, output_dir, 0, "decompress")
        self.decomp_worker.log_message.connect(self.decomp_log.log)
        # Update progress from worker
        self.decomp_worker.progress.connect(self._on_decomp_progress)
        self.decomp_worker.finished.connect(self._on_decomp_finished)
        self.decomp_worker.start()

    def _on_decomp_progress(self, current, total, message):
        self.decomp_progress.setProgress(current, total)
        if message:
            self.decomp_status.setText(message)

    def _on_decomp_finished(self, success, message):
        self.decompress_btn.setEnabled(True)
        if success:
            self.decomp_progress.setValue(100)
            self.decomp_status.setText("解压完成")
            self.decomp_log.log(message, "success")
            QMessageBox.information(self, "解压成功", message)
        else:
            self.decomp_progress.setValue(0)
            self.decomp_status.setText(f"解压失败: {message}")
            self.decomp_log.log(message, "error")
            QMessageBox.critical(self, "解压失败", message)

    def _on_browse_comp_folder(self):
        path = QFileDialog.getExistingDirectory(self, "选择要压缩的文件夹")
        if path:
            self.comp_input.setText(path)

    def _on_compress(self):
        self._do_compression("compress")

    def _do_compression(self, action):
        input_file = self.comp_input.text().strip()
        output_file = self.comp_output.text().strip()

        if not input_file:
            QMessageBox.warning(self, "提示", "请选择输入文件")
            return
        if not output_file:
            QMessageBox.warning(self, "提示", "请选择输出文件")
            return

        self._operation_start_time = time.time()
        action_name = "压缩" if action == "compress" else "解压"
        self.compress_btn.setEnabled(False)
        self.decompress_btn.setEnabled(False)
        self.comp_progress.setVisible(True)
        self.comp_progress.setValue(0)
        self.comp_status.setText(f"正在{action_name}...")
        self.comp_log.log(f"开始{action_name}: {input_file} → {output_file}", "info")

        algo = self.algo_combo.currentIndex()
        self.comp_worker = CompressWorker(input_file, output_file, algo, action)
        self.comp_worker.log_message.connect(self.comp_log.log)
        self.comp_worker.finished.connect(self._on_compression_finished)
        self.comp_worker.start()

    def _on_compression_finished(self, success, message):
        elapsed = self._elapsed_str()
        self.compress_btn.setEnabled(True)
        self.decompress_btn.setEnabled(True)
        self.comp_progress.setVisible(False)

        if success:
            self.comp_progress.setValue(100)
            self.comp_status.setText(f"完成 — 耗时 {elapsed}")
            self.comp_log.log(f"{message}，耗时 {elapsed}", "success")
            QMessageBox.information(self, "操作成功", f"{message}\n耗时 {elapsed}")
        else:
            self.comp_status.setText(f"失败 — 耗时 {elapsed}")
            self.comp_log.log(f"操作失败: {message}，耗时 {elapsed}", "error")
            QMessageBox.critical(self, "操作失败", message)

    # ================================================================
    #  加密相关槽函数
    # ================================================================


    def _on_browse_decrypt_input(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择需要解密的文件",
                                              "", "所有文件 (*);;加密文件 (*.enc)")
        if path:
            self.decrypt_input.setText(path)

    def _on_browse_decrypt_output(self):
        path = QFileDialog.getExistingDirectory(self, "选择解密目标目录")
        if path:
            self.decrypt_output.setText(path)

    def _on_decrypt(self):
        input_file = self.decrypt_input.text().strip()
        password = self.decrypt_password.text()
        output_dir = self.decrypt_output.text().strip()
        if not input_file:
            QMessageBox.warning(self, "提示", "请选择需要解密的文件")
            return
        if not password:
            QMessageBox.warning(self, "提示", "请输入密码")
            return
        if not output_dir:
            QMessageBox.warning(self, "提示", "请选择解密目标目录")
            return

        self.decrypt_btn.setEnabled(False)
        self.decrypt_progress.setProgress(0, 1)
        self.decrypt_status.setText("正在解密...")
        self.decrypt_log.log(f"开始解密: {input_file} → {output_dir}", "info")

        self.decrypt_worker = EncryptWorker(input_file, output_dir, password, "decrypt")
        self.decrypt_worker.log_message.connect(self.decrypt_log.log)
        self.decrypt_worker.progress.connect(self._on_decrypt_progress)
        self.decrypt_worker.finished.connect(self._on_decrypt_finished)
        self.decrypt_worker.start()

    def _on_decrypt_progress(self, current, total, message):
        self.decrypt_progress.setProgress(current, total)
        if message:
            self.decrypt_status.setText(message)

    def _on_decrypt_finished(self, success, message):
        self.decrypt_btn.setEnabled(True)
        if success:
            self.decrypt_progress.setValue(100)
            self.decrypt_status.setText("解密完成")
            self.decrypt_log.log(message, "success")
            QMessageBox.information(self, "解密成功", message)
        else:
            self.decrypt_progress.setValue(0)
            self.decrypt_status.setText(f"解密失败: {message}")
            self.decrypt_log.log(message, "error")
            QMessageBox.critical(self, "解密失败", message)

    def _on_browse_enc_input(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择输入文件", "", "所有文件 (*);;加密文件 (*.enc)")
        if path:
            self.enc_input.setText(path)

    def _on_browse_enc_output_folder(self):
        path = QFileDialog.getExistingDirectory(self, "选择输出目录")
        if path:
            self.enc_output.setText(path)

    def _on_browse_enc_output(self):
        default_name = "输出.enc"
        input_file = self.enc_input.text().strip()
        if input_file and os.path.isfile(input_file):
            base = os.path.basename(input_file)
            if base:
                default_name = base + ".enc"
        path, _ = QFileDialog.getSaveFileName(self, "选择输出文件", default_name, "所有文件 (*);;加密文件 (*.enc)")
        if path:
            self.enc_output.setText(path)

    def _on_browse_enc_folder(self):
        path = QFileDialog.getExistingDirectory(self, "选择要加密的文件夹")
        if path:
            self.enc_input.setText(path)

    def _on_encrypt(self):
        self._do_encryption("encrypt")


    def _do_encryption(self, action):
        input_file = self.enc_input.text().strip()
        output_file = self.enc_output.text().strip()
        password = self.enc_password.text()

        if not input_file:
            QMessageBox.warning(self, "提示", "请选择输入文件")
            return
        if not output_file:
            QMessageBox.warning(self, "提示", "请选择输出文件")
            return
        if not password:
            QMessageBox.warning(self, "提示", "请输入密码")
            return

        self._operation_start_time = time.time()
        action_name = "加密" if action == "encrypt" else "解密"
        self.encrypt_btn.setEnabled(False)
        self.decrypt_btn.setEnabled(False)
        self.enc_progress.setVisible(True)
        self.enc_progress.setValue(0)
        self.enc_status.setText(f"正在{action_name}...")
        self.enc_log.log(f"开始{action_name}: {input_file} → {output_file}", "info")

        self.enc_worker = EncryptWorker(input_file, output_file, password, action)
        self.enc_worker.log_message.connect(self.enc_log.log)
        self.enc_worker.finished.connect(self._on_encryption_finished)
        self.enc_worker.start()

    def _on_encryption_finished(self, success, message):
        elapsed = self._elapsed_str()
        self.encrypt_btn.setEnabled(True)
        self.decrypt_btn.setEnabled(True)
        self.enc_progress.setVisible(False)

        if success:
            self.enc_progress.setValue(100)
            self.enc_status.setText(f"完成 — 耗时 {elapsed}")
            self.enc_log.log(f"{message}，耗时 {elapsed}", "success")
            QMessageBox.information(self, "操作成功", f"{message}\n耗时 {elapsed}")
        else:
            self.enc_status.setText(f"失败 — 耗时 {elapsed}")
            self.enc_log.log(f"操作失败: {message}，耗时 {elapsed}", "error")
            QMessageBox.critical(self, "操作失败", message)
