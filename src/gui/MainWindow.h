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

namespace datasoftware {

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
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
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

private:
    void setupUI();
    void setInputsEnabled(bool enabled);
    void startBackup();
    void startRestore();
    void refreshFilePreview();

    // File/data state
    QString     m_sourceDir;        // directory mode path
    QStringList m_selectedFiles;    // individual files

    // UI
    QLineEdit*    m_archiveFileEdit;
    QLineEdit*    m_restoreArchiveEdit;
    QLineEdit*    m_restoreDirEdit;
    QProgressBar* m_progressBar;
    QLabel*       m_statusLabel;
    QPushButton*  m_backupBtn;
    QPushButton*  m_restoreBtn;
    QTreeWidget*  m_filePreview;
    QLabel*       m_previewSummary;
    QLabel*       m_sourceLabel;
};

} // namespace datasoftware

#endif // DATASOFTWARE_MAINWINDOW_H