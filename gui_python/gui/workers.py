"""
后台工作线程模块 — 通过 subprocess 调用 C++ CLI 可执行文件
"""

import os
import re
import subprocess
from PyQt5.QtCore import QThread, pyqtSignal, QTimer


def find_executable():
    """查找 C++ dsbackup 可执行文件"""
    exe = os.environ.get("DATASOFTWARE_EXE")
    if exe and os.path.isfile(exe):
        return exe

    base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    candidates = [
        os.path.join(base, "..", "dsbackup.exe"),
        os.path.join(base, "..", "build", "dsbackup.exe"),
        os.path.join(base, "..", "build", "Release", "dsbackup.exe"),
        os.path.join(base, "..", "build", "Debug", "dsbackup.exe"),
        os.path.join(base, "..", "cmake-build-debug", "dsbackup.exe"),
        os.path.join(base, "..", "cmake-build-release", "dsbackup.exe"),
        os.path.join(base, "dsbackup.exe"),
    ]
    for path in candidates:
        normalized = os.path.normpath(path)
        if os.path.isfile(normalized):
            return normalized

    return None


class BackupWorker(QThread):
    """备份工作线程 — 调用 C++ CLI backup/pack 命令"""

    progress = pyqtSignal(int, int, str)
    log_message = pyqtSignal(str, str)
    finished = pyqtSignal(bool, str, int)

    def __init__(self, source, dest, password="", backup_type="folder", files=None, filters=None):
        super().__init__()
        self.source = source
        self.dest = dest
        self.password = password
        self.backup_type = backup_type
        self.files = files or []
        self.filters = filters or {}

    def run(self):
        exe = find_executable()
        if not exe:
            self.finished.emit(False, "找不到 datasoftware 可执行文件。请编译 C++ 项目或设置 DATASOFTWARE_EXE 环境变量。", 0)
            return

        try:
            if self.backup_type == "folder" and self.source:
                cmd = [exe, "backup", self.source, self.dest]
            else:
                cmd = [exe, "pack"] + self.files + [self.dest]

            self.log_message.emit(f"执行命令: {' '.join(cmd[:4])}{'...' if len(cmd) > 4 else ''}", "info")

            proc = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace"
            )

            total_count = 0
            for line in proc.stdout:
                line = line.strip()
                match = re.match(r"\[PROGRESS\]\s+(\d+)\s+(\d+)\s+(.*)", line)
                if match:
                    current = int(match.group(1))
                    total = int(match.group(2))
                    msg = match.group(3)
                    total_count = total
                    self.progress.emit(current, total, msg)
                    self.log_message.emit(f"{msg} ({current}/{total})", "progress")
                    continue

                done_match = re.match(r"\[DONE\]\s+(OK|ERR)\s+(.*)", line)
                if done_match:
                    ok = done_match.group(1) == "OK"
                    message = done_match.group(2)
                    if ok:
                        self.log_message.emit(message, "success")
                        self.finished.emit(True, message, total_count)
                    else:
                        self.log_message.emit(f"失败: {message}", "error")
                        self.finished.emit(False, message, 0)
                    return

                if line:
                    self.log_message.emit(line, "progress")

            proc.wait()
            if proc.returncode != 0:
                msg = f"进程退出，返回码: {proc.returncode}"
                self.log_message.emit(msg, "error")
                self.finished.emit(False, msg, 0)
            else:
                self.finished.emit(True, "备份完成", total_count)

        except Exception as e:
            msg = f"备份异常: {str(e)}"
            self.log_message.emit(msg, "error")
            self.finished.emit(False, msg, 0)


class RestoreWorker(QThread):
    """恢复工作线程 — 调用 C++ CLI restore 命令"""

    progress = pyqtSignal(int, int, str)
    log_message = pyqtSignal(str, str)
    finished = pyqtSignal(bool, str, int)

    def __init__(self, source, dest, password=""):
        super().__init__()
        self.source = source
        self.dest = dest
        self.password = password

    def run(self):
        exe = find_executable()
        if not exe:
            self.finished.emit(False, "找不到 datasoftware 可执行文件。请编译 C++ 项目或设置 DATASOFTWARE_EXE 环境变量。", 0)
            return

        try:
            cmd = [exe, "restore", self.source, self.dest]
            self.log_message.emit(f"执行命令: {' '.join(cmd)}", "info")

            proc = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace"
            )

            total_count = 0
            for line in proc.stdout:
                line = line.strip()
                match = re.match(r"\[PROGRESS\]\s+(\d+)\s+(\d+)\s+(.*)", line)
                if match:
                    current = int(match.group(1))
                    total = int(match.group(2))
                    msg = match.group(3)
                    total_count = total
                    self.progress.emit(current, total, msg)
                    self.log_message.emit(f"{msg} ({current}/{total})", "progress")
                    continue

                done_match = re.match(r"\[DONE\]\s+(OK|ERR)\s+(.*)", line)
                if done_match:
                    ok = done_match.group(1) == "OK"
                    message = done_match.group(2)
                    if ok:
                        self.log_message.emit(message, "success")
                        self.finished.emit(True, message, total_count)
                    else:
                        self.log_message.emit(f"失败: {message}", "error")
                        self.finished.emit(False, message, 0)
                    return

                if line:
                    self.log_message.emit(line, "progress")

            proc.wait()
            if proc.returncode != 0:
                msg = f"进程退出，返回码: {proc.returncode}"
                self.log_message.emit(msg, "error")
                self.finished.emit(False, msg, 0)
            else:
                self.finished.emit(True, "恢复完成", total_count)

        except Exception as e:
            msg = f"恢复异常: {str(e)}"
            self.log_message.emit(msg, "error")
            self.finished.emit(False, msg, 0)


class CompressWorker(QThread):
    """压缩工作线程 — 调用 C++ CLI compress/decompress 命令"""

    progress = pyqtSignal(int, int, str)
    log_message = pyqtSignal(str, str)
    finished = pyqtSignal(bool, str)

    def __init__(self, input_file, output_file, algo, action="compress"):
        super().__init__()
        self.input_file = input_file
        self.output_file = output_file
        self.algo = algo
        self.action = action

    def run(self):
        exe = find_executable()
        if not exe:
            self.finished.emit(False, "找不到 datasoftware 可执行文件。请编译 C++ 项目或设置 DATASOFTWARE_EXE 环境变量。")
            return

        try:
            if self.action == "compress":
                cmd = [exe, "compress", self.input_file, self.output_file, str(self.algo)]
                action_name = "压缩"
            else:
                cmd = [exe, "decompress", self.input_file, self.output_file, str(self.algo)]
                action_name = "解压"

            self.log_message.emit(f"执行命令: {' '.join(cmd)}", "info")
            self.progress.emit(0, 1, f"正在{action_name}...")

            proc = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace"
            )

            for line in proc.stdout:
                line = line.strip()
                done_match = re.match(r"\[DONE\]\s+(OK|ERR)\s+(.*)", line)
                if done_match:
                    ok = done_match.group(1) == "OK"
                    message = done_match.group(2)
                    if ok:
                        self.progress.emit(1, 1, f"{action_name}完成")
                        self.log_message.emit(message, "success")
                        self.finished.emit(True, message)
                    else:
                        self.log_message.emit(f"{action_name}失败: {message}", "error")
                        self.finished.emit(False, message)
                    return

                if line:
                    self.log_message.emit(line, "progress")

            proc.wait()
            if proc.returncode != 0:
                msg = f"{action_name}失败，返回码: {proc.returncode}"
                self.log_message.emit(msg, "error")
                self.finished.emit(False, msg)
            else:
                self.finished.emit(True, f"{action_name}完成")

        except Exception as e:
            msg = f"{self.action}异常: {str(e)}"
            self.log_message.emit(msg, "error")
            self.finished.emit(False, msg)


class EncryptWorker(QThread):
    """加密工作线程 — 调用 C++ CLI encrypt/decrypt 命令"""

    progress = pyqtSignal(int, int, str)
    log_message = pyqtSignal(str, str)
    finished = pyqtSignal(bool, str)

    def __init__(self, input_file, output_file, password, action="encrypt"):
        super().__init__()
        self.input_file = input_file
        self.output_file = output_file
        self.password = password
        self.action = action

    def run(self):
        exe = find_executable()
        if not exe:
            self.finished.emit(False, "找不到 datasoftware 可执行文件。请编译 C++ 项目或设置 DATASOFTWARE_EXE 环境变量。")
            return

        try:
            if self.action == "encrypt":
                cmd = [exe, "encrypt", self.input_file, self.output_file, self.password]
                action_name = "加密"
            else:
                cmd = [exe, "decrypt", self.input_file, self.output_file, self.password]
                action_name = "解密"

            self.log_message.emit(f"执行命令: {cmd[0]} {cmd[1]} {cmd[2]} {cmd[3]} ****", "info")
            self.progress.emit(0, 1, f"正在{action_name}...")

            proc = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace"
            )

            for line in proc.stdout:
                line = line.strip()
                done_match = re.match(r"\[DONE\]\s+(OK|ERR)\s+(.*)", line)
                if done_match:
                    ok = done_match.group(1) == "OK"
                    message = done_match.group(2)
                    if ok:
                        self.progress.emit(1, 1, f"{action_name}完成")
                        self.log_message.emit(message, "success")
                        self.finished.emit(True, message)
                    else:
                        self.log_message.emit(f"{action_name}失败: {message}", "error")
                        self.finished.emit(False, message)
                    return

                if line:
                    self.log_message.emit(line, "progress")

            proc.wait()
            if proc.returncode != 0:
                msg = f"{action_name}失败，返回码: {proc.returncode}"
                self.log_message.emit(msg, "error")
                self.finished.emit(False, msg)
            else:
                self.finished.emit(True, f"{action_name}完成")

        except Exception as e:
            msg = f"{self.action}异常: {str(e)}"
            self.log_message.emit(msg, "error")
            self.finished.emit(False, msg)