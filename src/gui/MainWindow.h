#ifndef DATASOFTWARE_MAINWINDOW_H
#define DATASOFTWARE_MAINWINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QThread>
#include <QTreeWidget>
#include <QHeaderView>
#include <QStringList>
#include <QComboBox>
#include <QTabWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <QDateTimeEdit>

#include <datasoftware/Compressor.h>
#include <datasoftware/BackupFilter.h>

namespace datasoftware {

// ===== Worker for backup/restore =====
class BackupWorker : public QThread {
    Q_OBJECT
public:
    enum Mode { Backup, Restore };
    enum BackupType { Directory, FileList };

    BackupWorker(const std::string& src, const std::string& dst)
        : m_src(src), m_dst(dst), m_backupType(Directory) {}

    BackupWorker(const QStringList& files, const std::string& dst)
        : m_fileList(files), m_dst(dst), m_backupType(FileList) {}

    void setMode(Mode m) { m_mode = m; }
    void setFilter(const BackupFilter& f) { m_filter = f; }
    void setPassword(const std::string& pwd) { m_password = pwd; }

signals:
    void progressUpdated(quint64 current, quint64 total, const QString& currentFile);
    void operationFinished(bool success, const QString& message, quint64 count);

protected:
    void run() override;

private:
    std::string m_src;
    QStringList m_fileList;
    std::string m_dst;
    Mode m_mode = Backup;
    BackupType m_backupType = Directory;
    BackupFilter m_filter;
    std::string m_password;
};

// ===== Worker for compress/decompress =====
class CompressWorker : public QThread {
    Q_OBJECT
public:
    enum Action { Compress, Decompress };

    CompressWorker(const std::string& input, const std::string& output,
                   CompressAlgo algo, Action action)
        : m_input(input), m_output(output), m_algo(algo), m_action(action) {}

signals:
    void operationFinished(bool success, const QString& message);

protected:
    void run() override;

private:
    std::string m_input;
    std::string m_output;
    CompressAlgo m_algo;
    Action m_action;
};

// ===== Main Window =====
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    // Backup/Restore
    void onAddFolder();
    void onAddFiles();
    void onClearFiles();
    void onBrowseArchive();
    void onBrowseRestore();
    void onBrowseRestoreArchive();
    void onBackup();
    void onRestore();
    void onProgress(quint64 current, quint64 total, const QString& currentFile);
    void onOperationFinished(bool success, const QString& message, quint64 count);

    // Filters
    void onToggleFilters();
    void onApplyFilters();
    void onClearFilters();

    // Encryption (standalone)
    void onBrowseEncInput();
    void onBrowseEncOutput();
    void onEncryptFile();
    void onDecryptFile();
    void onEncFinished(bool success, const QString& message);

    // Compression
    void onBrowseCompInput();
    void onBrowseCompOutput();
    void onCompress();
    void onDecompress();
    void onCompFinished(bool success, const QString& message);

private:
    void setupUI();
    void setInputsEnabled(bool enabled);
    void startBackup();
    void startRestore();
    void refreshFilePreview();
    BackupFilter collectFilter() const;

    // ---- Data state ----
    QString     m_sourceDir;
    QStringList m_selectedFiles;
    BackupFilter m_currentFilter;

    // ---- Backup UI ----
    QLineEdit*    m_archiveFileEdit;
    QLineEdit*    m_restoreArchiveEdit;
    QLineEdit*    m_restoreDirEdit;
    QProgressBar* m_backupProgress;
    QLabel*       m_backupStatus;
    QPushButton*  m_backupBtn;
    QPushButton*  m_restoreBtn;
    QTreeWidget*  m_filePreview;
    QLabel*       m_previewSummary;
    QLabel*       m_sourceLabel;
    QLabel*       m_filterLabel;

    // Filter controls
    QWidget*      m_filterPanel;
    QLineEdit*    m_filtExtEdit;
    QLineEdit*    m_filtNameEdit;
    QLineEdit*    m_filtPathIncEdit;
    QLineEdit*    m_filtPathExcEdit;
    QSpinBox*     m_filtMinSize;
    QSpinBox*     m_filtMaxSize;
    QDateTimeEdit* m_filtTimeFrom;
    QDateTimeEdit* m_filtTimeTo;
    QLineEdit*    m_filtUserEdit;

    // ---- Compression UI ----
    QComboBox*    m_algoCombo;
    QLineEdit*    m_compInputEdit;
    QLineEdit*    m_compOutputEdit;
    QPushButton*  m_compressBtn;
    QPushButton*  m_decompressBtn;
    QProgressBar* m_compProgress;
    QLabel*       m_compStatus;

    // ---- Encryption UI ----
    QLineEdit*    m_backupPwdEdit;     // password for backup
    QLineEdit*    m_restorePwdEdit;    // password for restore
    QLineEdit*    m_encInputEdit;
    QLineEdit*    m_encOutputEdit;
    QLineEdit*    m_encPwdEdit;
    QPushButton*  m_encryptBtn;
    QPushButton*  m_decryptBtn;
    QProgressBar* m_encProgress;
    QLabel*       m_encStatus;
};

} // namespace datasoftware

#endif // DATASOFTWARE_MAINWINDOW_H