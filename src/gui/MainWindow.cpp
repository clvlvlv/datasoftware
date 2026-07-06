#include "MainWindow.h"
#include "datasoftware/BackupEngine.h"
#include "datasoftware/FileTraverser.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QFrame>
#include <QDir>
#include <QElapsedTimer>

namespace datasoftware {

// === BackupWorker ===

void BackupWorker::run() {
    QElapsedTimer timer;
    timer.start();

    auto cb = [this](size_t current, size_t total, const std::string& msg) {
        emit progressUpdated(static_cast<quint64>(current),
                             static_cast<quint64>(total),
                             QString::fromStdString(msg));
    };

    try {
        size_t count = 0;
        if (m_mode == Backup) {
            if (m_backupType == FileList) {
                std::vector<std::string> paths;
                for (const auto& f : m_fileList)
                    paths.push_back(f.toStdString());
                count = BackupEngine::backupFiles(paths, m_dst, cb);
            } else {
                count = BackupEngine::backup(m_src, m_dst, cb);
            }
        } else {
            count = BackupEngine::restore(m_src, m_dst, cb);
        }
        qint64 elapsed = timer.elapsed();
        QString timeStr;
        if (elapsed < 1000)
            timeStr = QString("%1 ms").arg(elapsed);
        else if (elapsed < 60000)
            timeStr = QString("%1 s").arg(elapsed / 1000.0, 0, 'f', 1);
        else
            timeStr = QString("%1 min %2 s")
                .arg(elapsed / 60000)
                .arg((elapsed % 60000) / 1000.0, 0, 'f', 0);

        QString msg = (m_mode == Backup)
            ? QString("Backup complete! %1 files (%2).").arg(count).arg(timeStr)
            : QString("Restore complete! %1 files (%2).").arg(count).arg(timeStr);
        emit operationFinished(true, msg, count);
    } catch (const std::exception& e) {
        qint64 elapsed = timer.elapsed();
        emit operationFinished(false, QString("Error after %1 ms: %2")
                                   .arg(elapsed).arg(e.what()), 0);
    }
}

// === MainWindow ===

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Data Backup Software");
    setMinimumSize(620, 520);
    resize(640, 580);
    setupUI();
}

void MainWindow::setupUI() {
    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // --- Backup Group ---
    auto* backupGroup = new QGroupBox("Backup");
    auto* backupLayout = new QVBoxLayout(backupGroup);

    // Source selector: buttons row
    m_sourceLabel = new QLabel("No files selected.");
    m_sourceLabel->setStyleSheet("font-weight: bold;");
    backupLayout->addWidget(m_sourceLabel);

    auto* srcBtnRow = new QHBoxLayout();
    auto* addFolderBtn = new QPushButton("Add Folder...");
    connect(addFolderBtn, &QPushButton::clicked, this, &MainWindow::onAddFolder);
    srcBtnRow->addWidget(addFolderBtn);

    auto* addFilesBtn = new QPushButton("Add Files...");
    connect(addFilesBtn, &QPushButton::clicked, this, &MainWindow::onAddFiles);
    srcBtnRow->addWidget(addFilesBtn);

    auto* clearBtn = new QPushButton("Clear");
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::onClearFiles);
    srcBtnRow->addWidget(clearBtn);
    srcBtnRow->addStretch();
    backupLayout->addLayout(srcBtnRow);

    // File preview tree
    m_filePreview = new QTreeWidget();
    m_filePreview->setHeaderLabels({"Name", "Type", "Size"});
    m_filePreview->setRootIsDecorated(false);
    m_filePreview->setAlternatingRowColors(true);
    m_filePreview->setMinimumHeight(120);
    m_filePreview->setMaximumHeight(200);
    m_filePreview->header()->setStretchLastSection(false);
    m_filePreview->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_filePreview->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_filePreview->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    backupLayout->addWidget(m_filePreview);

    m_previewSummary = new QLabel("");
    m_previewSummary->setStyleSheet("color: #666;");
    backupLayout->addWidget(m_previewSummary);

    // Archive file
    auto* archiveRow = new QHBoxLayout();
    m_archiveFileEdit = new QLineEdit();
    m_archiveFileEdit->setPlaceholderText("Select archive file path...");
    auto* browseArchiveBtn = new QPushButton("Browse...");
    browseArchiveBtn->setFixedWidth(90);
    connect(browseArchiveBtn, &QPushButton::clicked, this, &MainWindow::onBrowseArchive);
    archiveRow->addWidget(m_archiveFileEdit);
    archiveRow->addWidget(browseArchiveBtn);
    backupLayout->addLayout(archiveRow);

    m_backupBtn = new QPushButton("Run Backup");
    m_backupBtn->setMinimumHeight(32);
    connect(m_backupBtn, &QPushButton::clicked, this, &MainWindow::onBackup);
    backupLayout->addWidget(m_backupBtn);

    mainLayout->addWidget(backupGroup);

    // --- Restore Group ---
    auto* restoreGroup = new QGroupBox("Restore");
    auto* restoreForm = new QFormLayout(restoreGroup);

    auto* restArchiveRow = new QHBoxLayout();
    m_restoreArchiveEdit = new QLineEdit();
    m_restoreArchiveEdit->setPlaceholderText("Select archive file to restore...");
    auto* browseRestArchiveBtn = new QPushButton("Browse...");
    browseRestArchiveBtn->setFixedWidth(90);
    connect(browseRestArchiveBtn, &QPushButton::clicked, this, &MainWindow::onBrowseRestoreArchive);
    restArchiveRow->addWidget(m_restoreArchiveEdit);
    restArchiveRow->addWidget(browseRestArchiveBtn);
    restoreForm->addRow("Archive File:", restArchiveRow);

    auto* restoreRow = new QHBoxLayout();
    m_restoreDirEdit = new QLineEdit();
    m_restoreDirEdit->setPlaceholderText("Select restore destination...");
    auto* browseRestoreBtn = new QPushButton("Browse...");
    browseRestoreBtn->setFixedWidth(90);
    connect(browseRestoreBtn, &QPushButton::clicked, this, &MainWindow::onBrowseRestore);
    restoreRow->addWidget(m_restoreDirEdit);
    restoreRow->addWidget(browseRestoreBtn);
    restoreForm->addRow("Restore Dir:", restoreRow);

    m_restoreBtn = new QPushButton("Run Restore");
    m_restoreBtn->setMinimumHeight(32);
    connect(m_restoreBtn, &QPushButton::clicked, this, &MainWindow::onRestore);
    restoreForm->addRow("", m_restoreBtn);

    mainLayout->addWidget(restoreGroup);

    // --- Separator ---
    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(sep);

    // --- Status Group ---
    auto* statusGroup = new QGroupBox("Status");
    auto* statusLayout = new QVBoxLayout(statusGroup);

    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    statusLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Ready.");
    statusLayout->addWidget(m_statusLabel);

    mainLayout->addWidget(statusGroup);

    setCentralWidget(central);

    refreshFilePreview();
}

// === Source selection ===

void MainWindow::onAddFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Source Directory");
    if (dir.isEmpty()) return;

    m_sourceDir = dir;
    m_selectedFiles.clear();
    refreshFilePreview();
}

void MainWindow::onAddFiles() {
    QStringList files = QFileDialog::getOpenFileNames(this, "Select Files to Backup");
    if (files.isEmpty()) return;

    // Switching from directory mode -> file mode clears dir
    if (!m_sourceDir.isEmpty()) {
        m_sourceDir.clear();
    }
    m_selectedFiles.append(files);
    refreshFilePreview();
}

void MainWindow::onClearFiles() {
    m_sourceDir.clear();
    m_selectedFiles.clear();
    refreshFilePreview();
}

void MainWindow::refreshFilePreview() {
    m_filePreview->clear();

    int fileCount = 0;
    uint64_t totalSize = 0;

    if (!m_sourceDir.isEmpty()) {
        // Directory mode
        QDir dir(m_sourceDir);
        m_sourceLabel->setText(QString("Source: %1").arg(m_sourceDir));

        try {
            auto entries = FileTraverser::traverse(m_sourceDir.toStdString());
            for (const auto& entry : entries) {
                if (entry.fileType == FileType::Directory) continue;
                auto* item = new QTreeWidgetItem();
                item->setText(0, QString::fromStdString(entry.relativePath));
                item->setText(1, "File");

                double sz = static_cast<double>(entry.fileSize);
                item->setText(2, sz >= 1048576.0 ? QString("%1 MB").arg(sz/1048576.0,0,'f',2)
                             : sz >= 1024.0 ? QString("%1 KB").arg(sz/1024.0,0,'f',1)
                             : QString("%1 B").arg(entry.fileSize));
                m_filePreview->addTopLevelItem(item);
                fileCount++;
                totalSize += entry.fileSize;
            }
        } catch (const std::exception&) {
            m_previewSummary->setText("(cannot read directory)");
        }

    } else if (!m_selectedFiles.isEmpty()) {
        // File list mode
        m_sourceLabel->setText(QString("Files: %1 selected").arg(m_selectedFiles.size()));

        for (const auto& filePath : m_selectedFiles) {
            QFileInfo fi(filePath);
            auto* item = new QTreeWidgetItem();
            item->setText(0, fi.fileName());
            item->setText(1, "File");

            qint64 sz = fi.size();
            item->setText(2, sz >= 1048576 ? QString("%1 MB").arg(sz/1048576.0,0,'f',2)
                         : sz >= 1024 ? QString("%1 KB").arg(sz/1024.0,0,'f',1)
                         : QString("%1 B").arg(sz));
            item->setData(0, Qt::UserRole, filePath); // store full path
            m_filePreview->addTopLevelItem(item);
            fileCount++;
            totalSize += static_cast<uint64_t>(sz);
        }

    } else {
        m_sourceLabel->setText("No files selected.");
    }

    // Summary
    if (fileCount > 0) {
        double totalMB = static_cast<double>(totalSize) / 1048576.0;
        if (totalSize >= 1073741824ull)
            m_previewSummary->setText(QString("%1 files, %2 GB total")
                .arg(fileCount).arg(totalMB/1024.0, 0, 'f', 2));
        else
            m_previewSummary->setText(QString("%1 files, %2 MB total")
                .arg(fileCount).arg(totalMB, 0, 'f', 2));
    } else {
        m_previewSummary->clear();
    }
}

// === Archive / Restore browsing ===

void MainWindow::onBrowseArchive() {
    QString file = QFileDialog::getSaveFileName(this, "Select Archive File",
                                                 m_archiveFileEdit->text(),
                                                 "DAT (*.dat);;All Files (*)");
    if (!file.isEmpty()) {
        m_archiveFileEdit->setText(file);
    }
}

void MainWindow::onBrowseRestore() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Restore Directory",
                                                     m_restoreDirEdit->text());
    if (!dir.isEmpty()) {
        m_restoreDirEdit->setText(dir);
    }
}

void MainWindow::onBrowseRestoreArchive() {
    QString file = QFileDialog::getOpenFileName(this, "Select Archive File to Restore",
                                                 m_restoreArchiveEdit->text(),
                                                 "DAT (*.dat);;All Files (*)");
    if (!file.isEmpty()) {
        m_restoreArchiveEdit->setText(file);
    }
}

// === Execute operations ===

void MainWindow::onBackup() {
    if (m_sourceDir.isEmpty() && m_selectedFiles.isEmpty()) {
        QMessageBox::warning(this, "Missing Input",
                             "Please add files or a folder to backup.");
        return;
    }
    QString dst = m_archiveFileEdit->text().trimmed();
    if (dst.isEmpty()) {
        QMessageBox::warning(this, "Missing Input",
                             "Please specify an archive file path.");
        return;
    }
    startBackup();
}

void MainWindow::onRestore() {
    QString src = m_restoreArchiveEdit->text().trimmed();
    QString dst = m_restoreDirEdit->text().trimmed();
    if (src.isEmpty() || dst.isEmpty()) {
        QMessageBox::warning(this, "Missing Input",
                             "Please specify both archive file and restore directory.");
        return;
    }
    startRestore();
}

void MainWindow::startBackup() {
    setInputsEnabled(false);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
    m_statusLabel->setText("Preparing backup...");
    QApplication::processEvents();

    BackupWorker* worker = nullptr;
    std::string dst = m_archiveFileEdit->text().trimmed().toStdString();

    if (!m_selectedFiles.isEmpty()) {
        // File list mode
        worker = new BackupWorker(m_selectedFiles, dst);
    } else {
        // Directory mode
        worker = new BackupWorker(m_sourceDir.toStdString(), dst);
    }

    worker->setMode(BackupWorker::Backup);
    connect(worker, &BackupWorker::progressUpdated, this, &MainWindow::onProgress);
    connect(worker, &BackupWorker::operationFinished, this, &MainWindow::onOperationFinished);
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void MainWindow::startRestore() {
    setInputsEnabled(false);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
    m_statusLabel->setText("Preparing restore...");
    QApplication::processEvents();

    auto* worker = new BackupWorker(
        m_restoreArchiveEdit->text().trimmed().toStdString(),
        m_restoreDirEdit->text().trimmed().toStdString());
    worker->setMode(BackupWorker::Restore);
    connect(worker, &BackupWorker::progressUpdated, this, &MainWindow::onProgress);
    connect(worker, &BackupWorker::operationFinished, this, &MainWindow::onOperationFinished);
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void MainWindow::onProgress(quint64 current, quint64 total, const QString& currentFile) {
    if (total > 0) {
        m_progressBar->setRange(0, static_cast<int>(total));
        m_progressBar->setValue(static_cast<int>(current));
    }
    if (!currentFile.isEmpty()) {
        m_statusLabel->setText(currentFile);
    }
}

void MainWindow::onOperationFinished(bool success, const QString& message, quint64 /*count*/) {
    setInputsEnabled(true);
    m_progressBar->setValue(m_progressBar->maximum());
    m_statusLabel->setText(message);

    if (success) {
        QMessageBox::information(this, "Success", message);
    } else {
        QMessageBox::critical(this, "Error", message);
    }
}

void MainWindow::setInputsEnabled(bool enabled) {
    m_archiveFileEdit->setEnabled(enabled);
    m_restoreArchiveEdit->setEnabled(enabled);
    m_restoreDirEdit->setEnabled(enabled);
    m_backupBtn->setEnabled(enabled);
    m_restoreBtn->setEnabled(enabled);
    m_filePreview->setEnabled(enabled);
}

} // namespace datasoftware