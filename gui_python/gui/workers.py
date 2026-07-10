"""
后台工作线程模块 —— 通过 subprocess 调用 C++ CLI 可执行文件
"""

import os
import re
import subprocess
from PyQt5.QtCore import QThread, pyqtSignal


def find_executable():
    """查找 C++ dsbackup 可执行文件"""
    exe = os.environ.get("DATASOFTWARE_EXE")
    if exe and os.path.isfile(exe):
        return exe

    current = os.path.abspath(__file__)
    gui_dir = os.path.dirname(current)
    gui_python_dir = os.path.dirname(gui_dir)
    project_root = os.path.dirname(gui_python_dir)

    candidates = [
        os.path.join(project_root, "build", "dsbackup.exe"),
        os.path.join(project_root, "build", "Release", "dsbackup.exe"),
        os.path.join(project_root, "build", "Debug", "dsbackup.exe"),
        os.path.join(project_root, "dsbackup.exe"),
        os.path.join(gui_python_dir, "dsbackup.exe"),
    ]

    for path in candidates:
        if os.path.isfile(path):
            return path

    return None


class BackupWorker(QThread):
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
            self.finished.emit(False, "找不到 datasoftware 可执行文件。", 0)
            return

        try:
            if self.backup_type == "folder" and self.source:
                cmd = [exe, "backup", self.source, self.dest]
                self.log_message.emit(f"备份: {self.source} → {self.dest}", "info")
            else:
                cmd = [exe, "pack"] + self.files + [self.dest]
                self.log_message.emit(f"打包 {len(self.files)} 个文件 → {self.dest}", "info")

            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT
            )

            total_count = 0
            backup_done_ok = False
            for line in proc.stdout:
                try:
                    line = line.decode('utf-8', errors='replace').strip()
                except:
                    line = line.decode('gbk', errors='replace').strip()
                
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
                        backup_done_ok = True
                        break
                    else:
                        self.log_message.emit(f"失败: {message}", "error")
                        self.finished.emit(False, message, 0)
                        return

                if line:
                    self.log_message.emit(line, "progress")

            # Backup CLI finished, now handle encryption if needed
            proc.wait()
            if backup_done_ok and self.password:
                self.log_message.emit("正在加密备份文件...", "info")
                enc_tmp = self.dest + ".enc"
                enc_proc = subprocess.Popen(
                    [exe, "encrypt", self.dest, enc_tmp, self.password],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT
                )
                enc_ok = False
                for eline in enc_proc.stdout:
                    eline = eline.decode("utf-8", errors="replace").strip()
                    dm = re.match(r"\[DONE\]\s+(OK|ERR)\s+(.*)", eline)
                    if dm:
                        if dm.group(1) == "OK":
                            enc_ok = True
                        break
                enc_proc.wait()
                if enc_ok:
                    os.replace(enc_tmp, self.dest)
                    self.log_message.emit("备份文件已加密", "success")
                    self.finished.emit(True, "备份文件已加密", total_count)
                else:
                    try: os.remove(enc_tmp)
                    except: pass
                    self.log_message.emit("备份文件加密失败", "error")
                    self.finished.emit(False, "备份文件加密失败", 0)
            elif backup_done_ok:
                self.finished.emit(True, "备份完成", total_count)
            elif not backup_done_ok:
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
            self.finished.emit(False, "找不到 datasoftware 可执行文件。", 0)
            return

        dec_tmp = None
        try:
            if self.password:
                dec_tmp = self.source + ".dec"
                self.log_message.emit("正在解密备份文件...", "info")
                dec_proc = subprocess.Popen(
                    [exe, "decrypt", self.source, dec_tmp, self.password],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT
                )
                dec_ok = False
                for eline in dec_proc.stdout:
                    eline = eline.decode("utf-8", errors="replace").strip()
                    dm = re.match(r"\[DONE\]\s+(OK|ERR)\s+(.*)", eline)
                    if dm:
                        if dm.group(1) == "OK":
                            dec_ok = True
                        break
                dec_proc.wait()
                if dec_ok:
                    self.source = dec_tmp
                    self.log_message.emit("解密成功，开始恢复...", "success")
                else:
                    self.log_message.emit("备份文件无需解密或密码错误，直接尝试恢复...", "warning")
                    try: os.remove(dec_tmp)
                    except: pass
                    dec_tmp = None

            cmd = [exe, "restore", self.source, self.dest]
            self.log_message.emit(f"恢复: {self.source} → {self.dest}", "info")

            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT
            )

            total_count = 0
            for line in proc.stdout:
                try:
                    line = line.decode('utf-8', errors='replace').strip()
                except:
                    line = line.decode('gbk', errors='replace').strip()
                
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
                if dec_tmp:
                    try: os.remove(dec_tmp)
                    except: pass
                msg = f"进程退出，返回码: {proc.returncode}"
                self.log_message.emit(msg, "error")
                self.finished.emit(False, msg, 0)
            else:
                if dec_tmp:
                    try: os.remove(dec_tmp)
                    except: pass
                self.finished.emit(True, "恢复完成", total_count)

        except Exception as e:
            if dec_tmp:
                try: os.remove(dec_tmp)
                except: pass
            msg = f"恢复异常: {str(e)}"
            self.log_message.emit(msg, "error")
            self.finished.emit(False, msg, 0)


class CompressWorker(QThread):
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
            self.finished.emit(False, "找不到 datasoftware 可执行文件。")
            return

        try:
            if self.action == "compress":
                cmd = [exe, "compress", self.input_file, self.output_file, str(self.algo)]
                action_name = "压缩"
            else:
                cmd = [exe, "decompress", self.input_file, self.output_file, str(self.algo)]
                action_name = "解压"

            self.log_message.emit(f"{action_name}: {self.input_file} → {self.output_file}", "info")
            self.progress.emit(0, 1, f"正在{action_name}...")

            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT
            )

            for line in proc.stdout:
                try:
                    line = line.decode('utf-8', errors='replace').strip()
                except:
                    line = line.decode('gbk', errors='replace').strip()
                
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
            self.finished.emit(False, "找不到 datasoftware 可执行文件。")
            return

        try:
            if self.action == "encrypt":
                cmd = [exe, "encrypt", self.input_file, self.output_file, self.password]
                action_name = "加密"
            else:
                cmd = [exe, "decrypt", self.input_file, self.output_file, self.password]
                action_name = "解密"

            self.log_message.emit(f"{action_name}: {self.input_file} → {self.output_file}", "info")
            self.progress.emit(0, 1, f"正在{action_name}...")

            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT
            )

            for line in proc.stdout:
                try:
                    line = line.decode('utf-8', errors='replace').strip()
                except:
                    line = line.decode('gbk', errors='replace').strip()
                
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
