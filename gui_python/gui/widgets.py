"""
自定义控件模块
"""

from PyQt5.QtWidgets import (
    QWidget, QLabel, QPushButton, QLineEdit, QProgressBar,
    QTreeWidget, QTreeWidgetItem, QHBoxLayout, QVBoxLayout,
    QFrame, QComboBox, QSpinBox, QDateTimeEdit, QGroupBox,
    QTextEdit, QGraphicsOpacityEffect
)
from PyQt5.QtCore import Qt, pyqtSignal, QPropertyAnimation, QEasingCurve, QDateTime
from PyQt5.QtGui import QFont


class ModernButton(QPushButton):
    """现代化按钮"""

    def __init__(self, text="", primary=False, success=False, danger=False, parent=None):
        super().__init__(text, parent)
        if primary:
            self.setObjectName("primaryBtn")
        elif success:
            self.setObjectName("successBtn")
        elif danger:
            self.setObjectName("dangerBtn")

        self.setCursor(Qt.PointingHandCursor)
        self._setup_animation()

    def _setup_animation(self):
        """设置透明度悬停动画（替代 geometry 动画，避免布局冲突）"""
        self._opacity_effect = QGraphicsOpacityEffect(self)
        self.setGraphicsEffect(self._opacity_effect)
        self._opacity_effect.setOpacity(1.0)

        self._anim = QPropertyAnimation(self._opacity_effect, b"opacity")
        self._anim.setDuration(120)
        self._anim.setEasingCurve(QEasingCurve.OutCubic)

    def enterEvent(self, event):
        self._anim.stop()
        self._anim.setStartValue(self._opacity_effect.opacity())
        self._anim.setEndValue(0.85)
        self._anim.start()
        super().enterEvent(event)

    def leaveEvent(self, event):
        self._anim.stop()
        self._anim.setStartValue(self._opacity_effect.opacity())
        self._anim.setEndValue(1.0)
        self._anim.start()
        super().leaveEvent(event)


class ModernLineEdit(QLineEdit):
    """现代化输入框"""

    def __init__(self, placeholder="", parent=None):
        super().__init__(parent)
        self.setPlaceholderText(placeholder)
        self.setAttribute(Qt.WA_MacShowFocusRect, False)
        self.setMinimumWidth(120)


class ModernProgressBar(QProgressBar):
    """现代化进度条"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setRange(0, 100)
        self.setTextVisible(False)
        self.setFixedHeight(10)

    def setProgress(self, current, total):
        if total > 0:
            self.setValue(int(current / total * 100))
        else:
            self.setValue(0)


class FilePreviewTree(QTreeWidget):
    """文件预览树"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setHeaderLabels(["名称", "类型", "大小"])
        self.setRootIsDecorated(False)
        self.setAlternatingRowColors(True)
        self.setIndentation(20)
        self.header().setStretchLastSection(False)
        self.header().setSectionResizeMode(0, self.header().Stretch)
        self.header().setSectionResizeMode(1, self.header().ResizeToContents)
        self.header().setSectionResizeMode(2, self.header().ResizeToContents)

    def add_file(self, name, file_type, size):
        item = QTreeWidgetItem()
        item.setText(0, name)
        item.setText(1, file_type)
        item.setText(2, self._format_size(size))
        self.addTopLevelItem(item)

    def _format_size(self, size):
        if size >= 1024**3:
            return f"{size/1024**3:.2f} GB"
        elif size >= 1024**2:
            return f"{size/1024**2:.2f} MB"
        elif size >= 1024:
            return f"{size/1024:.1f} KB"
        return f"{size} B"

    def clear_items(self):
        self.clear()


class SectionHeader(QFrame):
    """分区标题"""

    def __init__(self, title="", parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.NoFrame)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 8, 0, 8)

        self._label = QLabel(title)
        self._label.setObjectName("titleLabel")
        self._label.setStyleSheet("font-size: 14px; font-weight: bold;")

        line = QFrame()
        line.setFrameShape(QFrame.HLine)
        line.setStyleSheet("background-color: #45475a; max-height: 1px;")

        layout.addWidget(self._label)
        layout.addWidget(line, 1)

    def set_text(self, text):
        self._label.setText(text)


class LogPanel(QFrame):
    """操作日志面板 — 显示带时间戳和颜色的运行反馈"""

    def __init__(self, title="📋 操作日志", parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.NoFrame)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(4)

        header = QHBoxLayout()
        title_label = QLabel(title)
        title_label.setStyleSheet("font-size: 12px; font-weight: bold; color: #a6adc8;")
        header.addWidget(title_label)
        header.addStretch()

        clear_btn = QPushButton("清空")
        clear_btn.setFixedSize(50, 22)
        clear_btn.setCursor(Qt.PointingHandCursor)
        clear_btn.clicked.connect(self.clear)
        header.addWidget(clear_btn)
        outer.addLayout(header)

        self._text = QTextEdit()
        self._text.setReadOnly(True)
        self._text.setMinimumHeight(100)
        self._text.setMaximumHeight(180)
        self._text.setStyleSheet(
            "QTextEdit { background-color: #181825; color: #cdd6f4; "
            "border: 1px solid #313244; border-radius: 6px; "
            "font-family: 'Consolas', 'Courier New', monospace; font-size: 12px; "
            "padding: 6px; }"
        )
        outer.addWidget(self._text)

    def log(self, message, level="info"):
        """写入日志。level: info / success / error / warning / progress"""
        colors = {
            "info":     "#89b4fa",
            "success":  "#a6e3a1",
            "error":    "#f38ba8",
            "warning":  "#fab387",
            "progress": "#a6adc8",
        }
        icons = {
            "info":     "▶",
            "success":  "✅",
            "error":    "❌",
            "warning":  "⚠️",
            "progress": "📦",
        }
        color = colors.get(level, "#cdd6f4")
        icon = icons.get(level, "•")
        timestamp = QDateTime.currentDateTime().toString("hh:mm:ss")
        safe_msg = message.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        self._text.append(
            f'<span style="color:#585b70;">[{timestamp}]</span> '
            f'<span style="color:{color};">{icon} {safe_msg}</span>'
        )
        scrollbar = self._text.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())

    def clear(self):
        self._text.clear()