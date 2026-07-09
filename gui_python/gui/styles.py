"""
样式管理模块
"""

from PyQt5.QtWidgets import QApplication
from PyQt5.QtCore import QFile, QTextStream


class StyleManager:
    """样式管理器"""

    _instance = None
    _style = ""

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            cls._instance._load_style()
        return cls._instance

    def _load_style(self):
        """加载 QSS 样式"""
        try:
            with open("resources/styles.qss", "r", encoding="utf-8") as f:
                self._style = f.read()
        except FileNotFoundError:
            self._style = self._get_fallback_style()

    def _get_fallback_style(self):
        """获取后备样式"""
        return """
            QWidget { background-color: #1e1e2e; color: #cdd6f4; font-family: "Segoe UI", sans-serif; }
            QPushButton { background-color: #45475a; color: #cdd6f4; border: none; border-radius: 6px; padding: 8px 16px; }
            QPushButton:hover { background-color: #585b70; }
            QLineEdit { background-color: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 6px; padding: 6px; }
            QProgressBar { background-color: #313244; border-radius: 6px; height: 10px; }
            QProgressBar::chunk { background-color: #89b4fa; border-radius: 6px; }
            QTabWidget::pane { border: 1px solid #313244; border-radius: 8px; }
            QTabBar::tab { background-color: #313244; color: #a6adc8; padding: 8px 16px; border-radius: 6px 6px 0 0; }
            QTabBar::tab:selected { background-color: #45475a; color: #cdd6f4; }
        """

    @property
    def style(self):
        return self._style

    def apply(self, target):
        """应用样式到控件或 QApplication"""
        if isinstance(target, QApplication):
            target.setStyleSheet(self._style)
        else:
            target.setStyleSheet(self._style)