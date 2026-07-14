#include "MainWindow.h"
#include "datasoftware/BackupEngine.h"
#include "datasoftware/FileTraverser.h"
#include "datasoftware/Crypto.h"
#include <cstring>
#include <filesystem>

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
#include <QScrollArea>
#include <QDate>
#include <QFileInfo>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace datasoftware {

// =====================================================================
//  BackupWorker
// =====================================================================

void BackupWorker::run() {
    QElapsedTimer timer;
    timer.start();

    auto cb = [this](size_t cur, size_t tot, const std::string& msg) {
        emit progressUpdated(static_cast<quint64>(cur),
                             static_cast<quint64>(tot),
                             QString::fromStdString(msg));
    };

    try {
        size_t count = 0;
        if (m_mode == Backup) {
            if (m_backupType == FileList) {
                std::vector<std::string> paths;
                for (const auto& f : m_fileList) paths.push_back(f.toStdString());
                count = BackupEngine::backupFiles(paths, m_dst, cb);
            } else if (m_filter.isActive()) {
                count = BackupEngine::backup(m_src, m_dst, m_filter, cb);
            } else {
                count = BackupEngine::backup(m_src, m_dst, cb);
            }
            // Encrypt after backup if password is set
            if (!m_password.empty() && count > 0) {
                emit progressUpdated(0, 1, "Encrypting archive...");
                std::string tmpPath = m_dst + ".tmp";
                std::error_code ec;
                std::filesystem::rename(m_dst, tmpPath, ec);
                if (ec) throw std::runtime_error("Failed to prepare archive for encryption");
                Crypto::encryptFile(tmpPath, m_dst, m_password);
            try { std::filesystem::remove(tmpPath); } catch (...) {}
            }
        } else { // Restore
            // Decrypt before restore if encrypted
            if (Crypto::isEncryptedFile(m_src)) {
                if (m_password.empty())
                    throw std::runtime_error("This archive is encrypted. Please enter a password.");
                emit progressUpdated(0, 1, "Decrypting archive...");
                std::string tmpPath = m_src + ".tmp";
                Crypto::decryptFile(m_src, tmpPath, m_password);
                std::string origSrc = m_src;
                m_src = tmpPath;
                count = BackupEngine::restore(m_src, m_dst, cb);
            try { std::filesystem::remove(m_src); } catch (...) {}
            m_src = origSrc;
            } else {
                count = BackupEngine::restore(m_src, m_dst, cb);
            }
        }
        qint64 elapsed = timer.elapsed();
        QString ts = (elapsed < 1000) ? QString("%1 ms").arg(elapsed)
                    : (elapsed < 60000) ? QString("%1 s").arg(elapsed/1000.0,0,'f',1)
                    : QString("%1 min %2 s").arg(elapsed/60000).arg((elapsed%60000)/1000);

        QString msg = (m_mode == Backup)
            ? QString("Backup complete! %1 files (%2).").arg(count).arg(ts)
            : QString("Restore complete! %1 files (%2).").arg(count).arg(ts);
        emit operationFinished(true, msg, count);
    } catch (const std::exception& e) {
        emit operationFinished(false, QString("Error after %1 ms: %2")
                               .arg(timer.elapsed()).arg(e.what()), 0);
    }
}

// =====================================================================
//  CompressWorker
// =====================================================================

void CompressWorker::run() {
    try {
        if (m_action == Compress) {
            namespace fs = std::filesystem;
            if (fs::is_directory(fs::u8path(m_input))) {
                std::string tmpArc = m_output + ".tmp_archive";
                datasoftware::BackupEngine::backup(m_input, tmpArc);
                datasoftware::Compressor::compressFile(tmpArc, m_output, m_algo);
                fs::remove(fs::u8path(tmpArc));
            } else {
                datasoftware::Compressor::compressFile(m_input, m_output, m_algo);
            }
            emit operationFinished(true, QString("Compressed: %1 -> %2")
                                   .arg(QString::fromStdString(m_input))
                                   .arg(QString::fromStdString(m_output)));
        } else {
            datasoftware::Compressor::decompressFile(m_input, m_output, m_algo);
            {
                std::ifstream _checkArc(m_output, std::ios::binary);
                if (_checkArc.is_open()) {
                    char _magic[6] = {};
                    _checkArc.read(_magic, 6);
                    _checkArc.close();
                    if (std::memcmp(_magic, "DATASW", 6) == 0) {
                        std::string _arcPath = m_output + ".tmp_arc";
                        std::filesystem::rename(std::filesystem::u8path(m_output), std::filesystem::u8path(_arcPath));
                        std::filesystem::create_directories(std::filesystem::u8path(m_output));
                        datasoftware::BackupEngine::restore(_arcPath, m_output);
                        std::filesystem::remove(std::filesystem::u8path(_arcPath));
                    }
                }
            }
            emit operationFinished(true, QString("Decompressed: %1 -> %2")
                                   .arg(QString::fromStdString(m_input))
                                   .arg(QString::fromStdString(m_output)));
        }
    } catch (const std::exception& e) {
        emit operationFinished(false, QString("Error: %1").arg(e.what()));
    }
}

// =====================================================================
//  MainWindow
// =====================================================================

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Data Backup Software");
    resize(680, 620);
    setupUI();
}

void MainWindow::setupUI() {
    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* tabs = new QTabWidget();

    // ========== TAB 1: Backup & Restore ==========
    auto* backupTab = new QWidget();
    auto* backLayout = new QVBoxLayout(backupTab);
    backLayout->setSpacing(10);
    backLayout->setContentsMargins(12, 12, 12, 12);

    // Source
    m_sourceLabel = new QLabel("No files selected.");
    m_sourceLabel->setStyleSheet("font-weight: bold;");
    backLayout->addWidget(m_sourceLabel);

    auto* srcRow = new QHBoxLayout();
    auto* addFolderBtn = new QPushButton("Add Folder...");
    connect(addFolderBtn, &QPushButton::clicked, this, &MainWindow::onAddFolder);
    srcRow->addWidget(addFolderBtn);
    auto* addFilesBtn = new QPushButton("Add Files...");
    connect(addFilesBtn, &QPushButton::clicked, this, &MainWindow::onAddFiles);
    srcRow->addWidget(addFilesBtn);
    auto* clearBtn = new QPushButton("Clear");
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::onClearFiles);
    srcRow->addWidget(clearBtn);
    srcRow->addStretch();
    backLayout->addLayout(srcRow);

    // Preview
    m_filePreview = new QTreeWidget();
    m_filePreview->setHeaderLabels({"Name", "Type", "Size"});
    m_filePreview->setRootIsDecorated(false);
    m_filePreview->setAlternatingRowColors(true);
    m_filePreview->setMinimumHeight(110);
    m_filePreview->setMaximumHeight(180);
    m_filePreview->header()->setStretchLastSection(false);
    m_filePreview->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_filePreview->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_filePreview->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    backLayout->addWidget(m_filePreview);

    m_previewSummary = new QLabel("");
    m_previewSummary->setStyleSheet("color: #666;");
    backLayout->addWidget(m_previewSummary);

    // ---- Filters ----
    auto* filterToggle = new QPushButton("Filters...");
    filterToggle->setFixedWidth(100);
    connect(filterToggle, &QPushButton::clicked, this, &MainWindow::onToggleFilters);
    auto* clearFiltBtn = new QPushButton("Clear Filters");
    connect(clearFiltBtn, &QPushButton::clicked, this, &MainWindow::onClearFilters);
    m_filterLabel = new QLabel("no filters");

    auto* filtRow = new QHBoxLayout();
    filtRow->addWidget(filterToggle);
    filtRow->addWidget(clearFiltBtn);
    filtRow->addWidget(m_filterLabel);
    filtRow->addStretch();
    backLayout->addLayout(filtRow);

    m_filterPanel = new QWidget();
    auto* fpLayout = new QFormLayout(m_filterPanel);
    fpLayout->setContentsMargins(0, 0, 0, 0);

    m_filtExtEdit = new QLineEdit();
    m_filtExtEdit->setPlaceholderText(".txt, .jpg, .pdf (comma-separated)");
    fpLayout->addRow("Extensions:", m_filtExtEdit);

    m_filtNameEdit = new QLineEdit();
    m_filtNameEdit->setPlaceholderText("substring in filename");
    fpLayout->addRow("Name:", m_filtNameEdit);

    m_filtPathIncEdit = new QLineEdit();
    m_filtPathIncEdit->setPlaceholderText("docs/**, **/data/*.csv (glob)");
    fpLayout->addRow("Include Paths:", m_filtPathIncEdit);

    m_filtPathExcEdit = new QLineEdit();
    m_filtPathExcEdit->setPlaceholderText("tmp/**, *.log, .git/**");
    fpLayout->addRow("Exclude Paths:", m_filtPathExcEdit);

    auto* sizeRow = new QHBoxLayout();
    m_filtMinSize = new QSpinBox();
    m_filtMinSize->setRange(0, 99999999);
    m_filtMinSize->setSuffix(" bytes");
    m_filtMinSize->setSpecialValueText("none");
    m_filtMaxSize = new QSpinBox();
    m_filtMaxSize->setRange(0, 99999999);
    m_filtMaxSize->setSuffix(" bytes");
    m_filtMaxSize->setSpecialValueText("none");
    sizeRow->addWidget(new QLabel("Min:"));
    sizeRow->addWidget(m_filtMinSize);
    sizeRow->addWidget(new QLabel("Max:"));
    sizeRow->addWidget(m_filtMaxSize);
    sizeRow->addStretch();
    fpLayout->addRow("Size:", sizeRow);

    auto* timeRow = new QHBoxLayout();
    m_filtTimeFrom = new QDateTimeEdit();
    m_filtTimeFrom->setCalendarPopup(true);
    m_filtTimeFrom->setSpecialValueText("no limit");
    m_filtTimeFrom->setDisplayFormat("yyyy-MM-dd hh:mm");
    m_filtTimeFrom->setDateTime(QDateTime(QDate(2000, 1, 1), QTime(0, 0)));
    m_filtTimeTo = new QDateTimeEdit();
    m_filtTimeTo->setCalendarPopup(true);
    m_filtTimeTo->setSpecialValueText("no limit");
    m_filtTimeTo->setDisplayFormat("yyyy-MM-dd hh:mm");
    m_filtTimeTo->setDateTime(QDateTime(QDate(2000, 1, 1), QTime(0, 0)));
    timeRow->addWidget(new QLabel("From:"));
    timeRow->addWidget(m_filtTimeFrom);
    timeRow->addWidget(new QLabel("To:"));
    timeRow->addWidget(m_filtTimeTo);
    timeRow->addStretch();
    fpLayout->addRow("Modified:", timeRow);

    m_filtUserEdit = new QLineEdit();
    m_filtUserEdit->setPlaceholderText("owner name (substring)");
    fpLayout->addRow("User:", m_filtUserEdit);

    auto* applyFiltBtn = new QPushButton("Apply Filters");
    connect(applyFiltBtn, &QPushButton::clicked, this, &MainWindow::onApplyFilters);
    fpLayout->addRow("", applyFiltBtn);

    m_filterPanel->setVisible(false);
    backLayout->addWidget(m_filterPanel);

    // Password for backup
    auto* pwdRow = new QHBoxLayout();
    auto* pwdLabel = new QLabel("Encryption Password:");
    m_backupPwdEdit = new QLineEdit();
    m_backupPwdEdit->setPlaceholderText("(leave empty for no encryption)");
    m_backupPwdEdit->setEchoMode(QLineEdit::Password);
    pwdRow->addWidget(pwdLabel);
    pwdRow->addWidget(m_backupPwdEdit, 1);
    backLayout->addLayout(pwdRow);

    // Separator
    backLayout->addWidget(new QFrame());

    // Archive file
    auto* archRow = new QHBoxLayout();
    m_archiveFileEdit = new QLineEdit();
    m_archiveFileEdit->setPlaceholderText("Archive file path for backup...");
    auto* browseArchBtn = new QPushButton("Browse...");
    browseArchBtn->setFixedWidth(90);
    connect(browseArchBtn, &QPushButton::clicked, this, &MainWindow::onBrowseArchive);
    archRow->addWidget(m_archiveFileEdit);
    archRow->addWidget(browseArchBtn);
    backLayout->addLayout(archRow);

    m_backupBtn = new QPushButton("Run Backup");
    m_backupBtn->setMinimumHeight(30);
    connect(m_backupBtn, &QPushButton::clicked, this, &MainWindow::onBackup);
    backLayout->addWidget(m_backupBtn);

    // Restore section
    backLayout->addWidget(new QFrame());
    auto* restoreLabel = new QLabel("Restore");
    restoreLabel->setStyleSheet("font-weight: bold;");
    backLayout->addWidget(restoreLabel);

    auto* restArchRow = new QHBoxLayout();
    m_restoreArchiveEdit = new QLineEdit();
    m_restoreArchiveEdit->setPlaceholderText("Archive file to restore...");
    auto* browseRestArchBtn = new QPushButton("Browse...");
    browseRestArchBtn->setFixedWidth(90);
    connect(browseRestArchBtn, &QPushButton::clicked, this, &MainWindow::onBrowseRestoreArchive);
    restArchRow->addWidget(m_restoreArchiveEdit);
    restArchRow->addWidget(browseRestArchBtn);
    backLayout->addLayout(restArchRow);

    auto* restDirRow = new QHBoxLayout();
    m_restoreDirEdit = new QLineEdit();
    m_restoreDirEdit->setPlaceholderText("Restore destination (type or browse)...");
    auto* browseRestDirBtn = new QPushButton("Browse...");
    browseRestDirBtn->setFixedWidth(90);
    connect(browseRestDirBtn, &QPushButton::clicked, this, &MainWindow::onBrowseRestore);
    restDirRow->addWidget(m_restoreDirEdit);
    restDirRow->addWidget(browseRestDirBtn);
    backLayout->addLayout(restDirRow);

    auto* restPwdRow = new QHBoxLayout();
    restPwdRow->addWidget(new QLabel("Password:"));
    m_restorePwdEdit = new QLineEdit();
    m_restorePwdEdit->setPlaceholderText("(password if encrypted)");
    m_restorePwdEdit->setEchoMode(QLineEdit::Password);
    restPwdRow->addWidget(m_restorePwdEdit, 1);
    backLayout->addLayout(restPwdRow);

    m_restoreBtn = new QPushButton("Run Restore");
    m_restoreBtn->setMinimumHeight(30);
    connect(m_restoreBtn, &QPushButton::clicked, this, &MainWindow::onRestore);
    backLayout->addWidget(m_restoreBtn);

    // Status
    backLayout->addWidget(new QFrame());
    auto* bsLabel = new QLabel("Status");
    bsLabel->setStyleSheet("font-weight: bold;");
    backLayout->addWidget(bsLabel);
    m_backupProgress = new QProgressBar();
    m_backupProgress->setRange(0, 100);
    m_backupProgress->setValue(0);
    backLayout->addWidget(m_backupProgress);
    m_backupStatus = new QLabel("Ready.");
    backLayout->addWidget(m_backupStatus);
    backLayout->addStretch();

    tabs->addTab(backupTab, "Backup && Restore");

    // ========== TAB 2: Compression ==========
    auto* compTab = new QWidget();
    auto* compLayout = new QVBoxLayout(compTab);
    compLayout->setSpacing(10);
    compLayout->setContentsMargins(12, 12, 12, 12);

    // Algorithm
    auto* algoRow = new QHBoxLayout();
    algoRow->addWidget(new QLabel("Algorithm:"));
    m_algoCombo = new QComboBox();
    m_algoCombo->addItem("RLE (Run-Length Encoding)", static_cast<int>(CompressAlgo::RLE));
    m_algoCombo->addItem("LZ77 (Sliding Window)",     static_cast<int>(CompressAlgo::LZ77));
    m_algoCombo->addItem("Huffman Coding",            static_cast<int>(CompressAlgo::Huffman));
    algoRow->addWidget(m_algoCombo);
    algoRow->addStretch();
    compLayout->addLayout(algoRow);

    // Input file
    auto* inRow = new QHBoxLayout();
    inRow->addWidget(new QLabel("Input:"));
    m_compInputEdit = new QLineEdit();
    m_compInputEdit->setPlaceholderText("Select input file...");
    auto* browseInBtn = new QPushButton("File...");
    browseInBtn->setFixedWidth(70);
    connect(browseInBtn, &QPushButton::clicked, this, &MainWindow::onBrowseCompInput);
    inRow->addWidget(m_compInputEdit);
    inRow->addWidget(browseInBtn);
    auto* browseCompFolderBtn = new QPushButton("Folder...");
    browseCompFolderBtn->setFixedWidth(70);
    connect(browseCompFolderBtn, &QPushButton::clicked, this, &MainWindow::onBrowseCompFolder);
    inRow->addWidget(browseCompFolderBtn);
    compLayout->addLayout(inRow);

    // Output file
    auto* outRow = new QHBoxLayout();
    outRow->addWidget(new QLabel("Output:"));
    m_compOutputEdit = new QLineEdit();
    m_compOutputEdit->setPlaceholderText("Select output file...");
    auto* browseOutBtn = new QPushButton("Browse...");
    browseOutBtn->setFixedWidth(90);
    connect(browseOutBtn, &QPushButton::clicked, this, &MainWindow::onBrowseCompOutput);
    outRow->addWidget(m_compOutputEdit);
    outRow->addWidget(browseOutBtn);
    compLayout->addLayout(outRow);

    // Buttons
    auto* btnRow = new QHBoxLayout();
    m_compressBtn = new QPushButton("Compress");
    m_compressBtn->setMinimumHeight(32);
    connect(m_compressBtn, &QPushButton::clicked, this, &MainWindow::onCompress);
    btnRow->addWidget(m_compressBtn);
    m_decompressBtn = new QPushButton("Decompress");
    m_decompressBtn->setMinimumHeight(32);
    connect(m_decompressBtn, &QPushButton::clicked, this, &MainWindow::onDecompress);
    btnRow->addWidget(m_decompressBtn);
    btnRow->addStretch();
    compLayout->addLayout(btnRow);

    // Status
    compLayout->addWidget(new QFrame());
    auto* csLabel = new QLabel("Status");
    csLabel->setStyleSheet("font-weight: bold;");
    compLayout->addWidget(csLabel);
    m_compProgress = new QProgressBar();
    m_compProgress->setRange(0, 0);
    m_compProgress->setVisible(false);
    compLayout->addWidget(m_compProgress);
    m_compStatus = new QLabel("Ready.");
    compLayout->addWidget(m_compStatus);
    compLayout->addStretch();

    tabs->addTab(compTab, "Compression");

    // ========== TAB 3: Encryption ==========
    auto* encTab = new QWidget();
    auto* encLayout = new QVBoxLayout(encTab);
    encLayout->setSpacing(10);
    encLayout->setContentsMargins(12, 12, 12, 12);

    auto* encInRow = new QHBoxLayout();
    encInRow->addWidget(new QLabel("Input:"));
    m_encInputEdit = new QLineEdit();
    m_encInputEdit->setPlaceholderText("Select file to encrypt/decrypt...");
    auto* browseEncInBtn = new QPushButton("File...");
    browseEncInBtn->setFixedWidth(70);
    connect(browseEncInBtn, &QPushButton::clicked, this, &MainWindow::onBrowseEncInput);
    encInRow->addWidget(m_encInputEdit);
    encInRow->addWidget(browseEncInBtn);
    auto* browseEncFolderBtn = new QPushButton("Folder...");
    browseEncFolderBtn->setFixedWidth(70);
    connect(browseEncFolderBtn, &QPushButton::clicked, this, &MainWindow::onBrowseEncFolder);
    encInRow->addWidget(browseEncFolderBtn);
    encInRow->addStretch();
    encLayout->addLayout(encInRow);

    auto* encOutRow = new QHBoxLayout();
    encOutRow->addWidget(new QLabel("Output:"));
    m_encOutputEdit = new QLineEdit();
    m_encOutputEdit->setPlaceholderText("Select output file...");
    auto* browseEncOutBtn = new QPushButton("Browse...");
    browseEncOutBtn->setFixedWidth(90);
    connect(browseEncOutBtn, &QPushButton::clicked, this, &MainWindow::onBrowseEncOutput);
    encOutRow->addWidget(m_encOutputEdit);
    encOutRow->addWidget(browseEncOutBtn);
    encLayout->addLayout(encOutRow);

    auto* encPwdRow = new QHBoxLayout();
    encPwdRow->addWidget(new QLabel("Password:"));
    m_encPwdEdit = new QLineEdit();
    m_encPwdEdit->setEchoMode(QLineEdit::Password);
    encPwdRow->addWidget(m_encPwdEdit, 1);
    encLayout->addLayout(encPwdRow);

    auto* encBtnRow = new QHBoxLayout();
    m_encryptBtn = new QPushButton("Encrypt File");
    m_encryptBtn->setMinimumHeight(32);
    connect(m_encryptBtn, &QPushButton::clicked, this, &MainWindow::onEncryptFile);
    encBtnRow->addWidget(m_encryptBtn);
    m_decryptBtn = new QPushButton("Decrypt File");
    m_decryptBtn->setMinimumHeight(32);
    connect(m_decryptBtn, &QPushButton::clicked, this, &MainWindow::onDecryptFile);
    encBtnRow->addWidget(m_decryptBtn);
    encBtnRow->addStretch();
    encLayout->addLayout(encBtnRow);

    encLayout->addWidget(new QFrame());
    auto* encStatusLabel = new QLabel("Status");
    encStatusLabel->setStyleSheet("font-weight: bold;");
    encLayout->addWidget(encStatusLabel);
    m_encProgress = new QProgressBar();
    m_encProgress->setRange(0, 0);
    m_encProgress->setVisible(false);
    encLayout->addWidget(m_encProgress);
    m_encStatus = new QLabel("Ready.");
    encLayout->addWidget(m_encStatus);
    encLayout->addStretch();

    tabs->addTab(encTab, "Encryption");

    // ========== TAB 5: Pack & Unpack ==========
    auto* packTab = new QWidget();
    auto* packLayout = new QVBoxLayout(packTab);
    packLayout->setSpacing(10);
    packLayout->setContentsMargins(12, 12, 12, 12);

    // --- Pack section ---
    auto* packLabel = new QLabel("Pack (bundle files into a single archive)");
    packLabel->setStyleSheet("font-weight: bold;");
    packLayout->addWidget(packLabel);

    auto* packBtnRow = new QHBoxLayout();
    auto* packAddBtn = new QPushButton("Add Files...");
    connect(packAddBtn, &QPushButton::clicked, this, &MainWindow::onPackAddFiles);
    packBtnRow->addWidget(packAddBtn);
    auto* packClearBtn = new QPushButton("Clear");
    connect(packClearBtn, &QPushButton::clicked, this, &MainWindow::onPackClear);
    packBtnRow->addWidget(packClearBtn);
    packBtnRow->addStretch();
    packLayout->addLayout(packBtnRow);

    m_packPreview = new QTreeWidget();
    m_packPreview->setHeaderLabels({"Name", "Size"});
    m_packPreview->setRootIsDecorated(false);
    m_packPreview->setAlternatingRowColors(true);
    m_packPreview->setMinimumHeight(100);
    m_packPreview->setMaximumHeight(160);
    m_packPreview->header()->setStretchLastSection(false);
    m_packPreview->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_packPreview->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    packLayout->addWidget(m_packPreview);

    m_packSummary = new QLabel("");
    m_packSummary->setStyleSheet("color: #666;");
    packLayout->addWidget(m_packSummary);

    auto* packOutRow = new QHBoxLayout();
    packOutRow->addWidget(new QLabel("Output:"));
    m_packOutputEdit = new QLineEdit();
    m_packOutputEdit->setPlaceholderText("Select output archive...");
    auto* packBrowseOutBtn = new QPushButton("Browse...");
    packBrowseOutBtn->setFixedWidth(90);
    connect(packBrowseOutBtn, &QPushButton::clicked, this, &MainWindow::onPackBrowseOutput);
    packOutRow->addWidget(m_packOutputEdit);
    packOutRow->addWidget(packBrowseOutBtn);
    packLayout->addLayout(packOutRow);

    m_packBtn = new QPushButton("Pack");
    m_packBtn->setMinimumHeight(32);
    connect(m_packBtn, &QPushButton::clicked, this, &MainWindow::onPack);
    packLayout->addWidget(m_packBtn);

    packLayout->addWidget(new QFrame());

    // --- Unpack section ---
    auto* unpackLabel = new QLabel("Unpack (extract files from an archive)");
    unpackLabel->setStyleSheet("font-weight: bold;");
    packLayout->addWidget(unpackLabel);

    auto* unpackInRow = new QHBoxLayout();
    unpackInRow->addWidget(new QLabel("Archive:"));
    m_unpackInputEdit = new QLineEdit();
    m_unpackInputEdit->setPlaceholderText("Select archive file to unpack...");
    auto* unpackBrowseInBtn = new QPushButton("Browse...");
    unpackBrowseInBtn->setFixedWidth(90);
    connect(unpackBrowseInBtn, &QPushButton::clicked, this, &MainWindow::onUnpackBrowseInput);
    unpackInRow->addWidget(m_unpackInputEdit);
    unpackInRow->addWidget(unpackBrowseInBtn);
    packLayout->addLayout(unpackInRow);

    auto* unpackOutRow = new QHBoxLayout();
    unpackOutRow->addWidget(new QLabel("Output:"));
    m_unpackOutputEdit = new QLineEdit();
    m_unpackOutputEdit->setPlaceholderText("Select output directory...");
    auto* unpackBrowseOutBtn = new QPushButton("Browse...");
    unpackBrowseOutBtn->setFixedWidth(90);
    connect(unpackBrowseOutBtn, &QPushButton::clicked, this, &MainWindow::onUnpackBrowseOutput);
    unpackOutRow->addWidget(m_unpackOutputEdit);
    unpackOutRow->addWidget(unpackBrowseOutBtn);
    packLayout->addLayout(unpackOutRow);

    m_unpackBtn = new QPushButton("Unpack");
    m_unpackBtn->setMinimumHeight(32);
    connect(m_unpackBtn, &QPushButton::clicked, this, &MainWindow::onUnpack);
    packLayout->addWidget(m_unpackBtn);

    packLayout->addWidget(new QFrame());

    // Status
    auto* packStatusLabel = new QLabel("Status");
    packStatusLabel->setStyleSheet("font-weight: bold;");
    packLayout->addWidget(packStatusLabel);
    m_packProgress = new QProgressBar();
    m_packProgress->setRange(0, 100);
    m_packProgress->setValue(0);
    packLayout->addWidget(m_packProgress);
    m_packStatus = new QLabel("Ready.");
    packLayout->addWidget(m_packStatus);
    packLayout->addStretch();

    tabs->addTab(packTab, "Pack && Unpack");
    mainLayout->addWidget(tabs);
    setCentralWidget(central);
    refreshFilePreview();
}

// =====================================================================
//  Backup - Slots
// =====================================================================

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
    if (!m_sourceDir.isEmpty()) m_sourceDir.clear();
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
        m_sourceLabel->setText(QString("Source: %1").arg(m_sourceDir));
        try {
            auto entries = (m_currentFilter.isActive())
                ? FileTraverser::traverse(m_sourceDir.toStdString(), nullptr, m_currentFilter)
                : FileTraverser::traverse(m_sourceDir.toStdString());
            for (const auto& e : entries) {
               if (e.fileType == FileType::Directory) continue;
               auto* item = new QTreeWidgetItem();
               item->setText(0, QString::fromStdString(e.relativePath));
                switch (e.fileType) {
                case FileType::Regular:  item->setText(1, "File");     break;
                case FileType::Symlink:  item->setText(1, "Symlink");  break;
                case FileType::HardLink: item->setText(1, "HardLink"); break;
                case FileType::Fifo:     item->setText(1, "FIFO");     break;
                case FileType::Device:   item->setText(1, "Device");   break;
                default:                 item->setText(1, "File");     break;
                }
                double sz = static_cast<double>(e.fileSize);
                item->setText(2, sz >= 1048576.0 ? QString("%1 MB").arg(sz/1048576.0,0,'f',2)
                             : sz >= 1024.0 ? QString("%1 KB").arg(sz/1024.0,0,'f',1)
                             : QString("%1 B").arg(e.fileSize));
                m_filePreview->addTopLevelItem(item);
                fileCount++; totalSize += e.fileSize;
            }
        } catch (...) { m_previewSummary->setText("(cannot read directory)"); }
    } else if (!m_selectedFiles.isEmpty()) {
        m_sourceLabel->setText(QString("Files: %1 selected").arg(m_selectedFiles.size()));
        for (const auto& fp : m_selectedFiles) {
            QFileInfo fi(fp);
            auto* item = new QTreeWidgetItem();
            item->setText(0, fi.fileName());
            item->setText(1, "File");
            qint64 sz = fi.size();
            item->setText(2, sz >= 1048576 ? QString("%1 MB").arg(sz/1048576.0,0,'f',2)
                         : sz >= 1024 ? QString("%1 KB").arg(sz/1024.0,0,'f',1)
                         : QString("%1 B").arg(sz));
            m_filePreview->addTopLevelItem(item);
            fileCount++; totalSize += static_cast<uint64_t>(sz);
        }
    } else {
        m_sourceLabel->setText("No files selected.");
    }

    if (fileCount > 0) {
        double mb = static_cast<double>(totalSize) / 1048576.0;
        m_previewSummary->setText(mb >= 1024.0
            ? QString("%1 files, %2 GB total").arg(fileCount).arg(mb/1024.0,0,'f',2)
            : QString("%1 files, %2 MB total").arg(fileCount).arg(mb,0,'f',2));
    } else {
        m_previewSummary->clear();
    }
}

void MainWindow::onBrowseArchive() {
    QString f = QFileDialog::getSaveFileName(this, "Select Archive File",
                                              m_archiveFileEdit->text(),
                                              "DAT (*.dat);;All Files (*)");
    if (!f.isEmpty()) m_archiveFileEdit->setText(f);
}

void MainWindow::onBrowseRestore() {
    QString d = QFileDialog::getSaveFileName(this, "Select Restore Directory",
                                              m_restoreDirEdit->text(),
                                              "Folder (*)");
    if (!d.isEmpty()) m_restoreDirEdit->setText(d);
}

void MainWindow::onBrowseRestoreArchive() {
    QString f = QFileDialog::getOpenFileName(this, "Select Archive File to Restore",
                                              m_restoreArchiveEdit->text(),
                                              "DAT (*.dat);;All Files (*)");
    if (!f.isEmpty()) m_restoreArchiveEdit->setText(f);
}

void MainWindow::onBackup() {
    if (m_sourceDir.isEmpty() && m_selectedFiles.isEmpty()) {
        QMessageBox::warning(this, "Missing Input", "Please add files or a folder.");
        return;
    }
    if (m_archiveFileEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Missing Input", "Please specify an archive file.");
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
    m_backupProgress->setValue(0);
    m_backupProgress->setVisible(true);
    m_backupStatus->setText("Preparing backup...");
    QApplication::processEvents();

    BackupWorker* w = nullptr;
    std::string dst = m_archiveFileEdit->text().trimmed().toStdString();
    std::string pwd = m_backupPwdEdit->text().toStdString();

    if (!m_selectedFiles.isEmpty())
        w = new BackupWorker(m_selectedFiles, dst);
    else {
        w = new BackupWorker(m_sourceDir.toStdString(), dst);
        if (m_currentFilter.isActive()) w->setFilter(m_currentFilter);
    }

    w->setMode(BackupWorker::Backup);
    if (!pwd.empty()) w->setPassword(pwd);
    connect(w, &BackupWorker::progressUpdated, this, &MainWindow::onProgress);
    connect(w, &BackupWorker::operationFinished, this, &MainWindow::onOperationFinished);
    connect(w, &QThread::finished, w, &QObject::deleteLater);
    w->start();
}

void MainWindow::startRestore() {
    setInputsEnabled(false);
    m_backupProgress->setValue(0);
    m_backupProgress->setVisible(true);
    m_backupStatus->setText("Preparing restore...");
    QApplication::processEvents();

    auto* w = new BackupWorker(
        m_restoreArchiveEdit->text().trimmed().toStdString(),
        m_restoreDirEdit->text().trimmed().toStdString());
    w->setMode(BackupWorker::Restore);
    std::string pwd = m_restorePwdEdit->text().toStdString();
    if (!pwd.empty()) w->setPassword(pwd);
    connect(w, &BackupWorker::progressUpdated, this, &MainWindow::onProgress);
    connect(w, &BackupWorker::operationFinished, this, &MainWindow::onOperationFinished);
    connect(w, &QThread::finished, w, &QObject::deleteLater);
    w->start();
}

void MainWindow::onProgress(quint64 cur, quint64 total, const QString& file) {
    if (total > 0) {
        m_backupProgress->setRange(0, static_cast<int>(total));
        m_backupProgress->setValue(static_cast<int>(cur));
    }
    if (!file.isEmpty()) m_backupStatus->setText(file);
}

void MainWindow::onOperationFinished(bool ok, const QString& msg, quint64) {
    setInputsEnabled(true);
    m_backupProgress->setValue(m_backupProgress->maximum());
    m_backupStatus->setText(msg);
    if (ok) QMessageBox::information(this, "Success", msg);
    else    QMessageBox::critical(this, "Error", msg);
}

void MainWindow::setInputsEnabled(bool en) {
    m_archiveFileEdit->setEnabled(en);
    m_restoreArchiveEdit->setEnabled(en);
    m_restoreDirEdit->setEnabled(en);
    m_backupBtn->setEnabled(en);
    m_restoreBtn->setEnabled(en);
    m_filePreview->setEnabled(en);
    m_filtExtEdit->setEnabled(en);
    m_filtNameEdit->setEnabled(en);
    m_filtPathIncEdit->setEnabled(en);
    m_filtPathExcEdit->setEnabled(en);
    m_filtMinSize->setEnabled(en);
    m_filtMaxSize->setEnabled(en);
    m_filtTimeFrom->setEnabled(en);
    m_filtTimeTo->setEnabled(en);
    m_filtUserEdit->setEnabled(en);
    m_backupPwdEdit->setEnabled(en);
    m_restorePwdEdit->setEnabled(en);
    m_compressBtn->setEnabled(en);
    m_decompressBtn->setEnabled(en);
    m_compInputEdit->setEnabled(en);
    m_compOutputEdit->setEnabled(en);
    m_algoCombo->setEnabled(en);
    m_encInputEdit->setEnabled(en);
    m_encOutputEdit->setEnabled(en);
    m_encPwdEdit->setEnabled(en);
    m_encryptBtn->setEnabled(en);
    m_decryptBtn->setEnabled(en);
    m_packBtn->setEnabled(en);
    m_unpackBtn->setEnabled(en);
    m_packOutputEdit->setEnabled(en);
    m_unpackInputEdit->setEnabled(en);
    m_unpackOutputEdit->setEnabled(en);
}

// =====================================================================
//  Filters - Slots
// =====================================================================

void MainWindow::onToggleFilters() {
    m_filterPanel->setVisible(!m_filterPanel->isVisible());
}

void MainWindow::onClearFilters() {
    m_filtExtEdit->clear();
    m_filtNameEdit->clear();
    m_filtPathIncEdit->clear();
    m_filtPathExcEdit->clear();
    m_filtMinSize->setValue(0);
    m_filtMaxSize->setValue(0);
    m_filtTimeFrom->setDateTime(QDateTime(QDate(2000, 1, 1), QTime(0, 0)));
    m_filtTimeTo->setDateTime(QDateTime(QDate(2000, 1, 1), QTime(0, 0)));
    m_filtUserEdit->clear();
    m_currentFilter.clear();
    m_filterLabel->setText("no filters");
}

BackupFilter MainWindow::collectFilter() const {
    BackupFilter f;

    // Extensions
    QString ext = m_filtExtEdit->text().trimmed();
    if (!ext.isEmpty()) {
        for (const auto& e : ext.split(',')) {
            QString t = e.trimmed();
            if (!t.isEmpty()) {
                if (t.startsWith('-')) f.excludeExts.push_back(t.mid(1).toStdString());
                else f.includeExts.push_back(t.toStdString());
            }
        }
    }

    // Name
    QString name = m_filtNameEdit->text().trimmed();
    if (!name.isEmpty()) f.namePattern = name.toStdString();

    // Paths
    for (const auto& p : m_filtPathIncEdit->text().split(',')) {
        QString t = p.trimmed();
        if (!t.isEmpty()) f.includePaths.push_back(t.toStdString());
    }
    for (const auto& p : m_filtPathExcEdit->text().split(',')) {
        QString t = p.trimmed();
        if (!t.isEmpty()) f.excludePaths.push_back(t.toStdString());
    }

    // Size
    f.minSize = static_cast<uint64_t>(m_filtMinSize->value());
    f.maxSize = static_cast<uint64_t>(m_filtMaxSize->value());

    // Time
    if (m_filtTimeFrom->dateTime() != QDateTime(QDate(2000, 1, 1), QTime(0, 0))) {
        f.timeFrom = m_filtTimeFrom->dateTime().toSecsSinceEpoch();
    }
    if (m_filtTimeTo->dateTime() != QDateTime(QDate(2000, 1, 1), QTime(0, 0))) {
        f.timeTo = m_filtTimeTo->dateTime().toSecsSinceEpoch();
    }

    // User
    QString user = m_filtUserEdit->text().trimmed();
    if (!user.isEmpty()) f.userName = user.toStdString();

    return f;
}

void MainWindow::onApplyFilters() {
    m_currentFilter = collectFilter();
    if (m_currentFilter.isActive()) {
        m_filterLabel->setText(QString::fromStdString(m_currentFilter.summary()));
    } else {
        m_filterLabel->setText("no filters");
    }
    // Refresh preview to show filtered files
    if (!m_sourceDir.isEmpty()) refreshFilePreview();
    m_filterPanel->setVisible(false);
}

// =====================================================================
//  Compression - Slots
// =====================================================================

void MainWindow::onBrowseCompInput() {
    QString f = QFileDialog::getOpenFileName(this, "Select Input File",
                                              m_compInputEdit->text(),
                                              "All Files (*)");
    if (!f.isEmpty()) m_compInputEdit->setText(f);
}

void MainWindow::onBrowseCompFolder() {
    QString d = QFileDialog::getExistingDirectory(this, "Select Input Folder",
                                                    m_compInputEdit->text());
    if (!d.isEmpty()) m_compInputEdit->setText(d);
}

void MainWindow::onBrowseCompOutput() {
    QString f = QFileDialog::getSaveFileName(this, "Select Output File",
                                              m_compOutputEdit->text(),
                                              "All Files (*)");
    if (!f.isEmpty()) m_compOutputEdit->setText(f);
}

void MainWindow::onCompress() {
    QString in = m_compInputEdit->text().trimmed();
    QString out = m_compOutputEdit->text().trimmed();
    if (in.isEmpty() || out.isEmpty()) {
        QMessageBox::warning(this, "Missing Input", "Please specify input and output files.");
        return;
    }
    CompressAlgo algo = static_cast<CompressAlgo>(
        m_algoCombo->currentData().toInt());

    setInputsEnabled(false);
    m_compProgress->setVisible(true);
    m_compStatus->setText("Compressing...");
    QApplication::processEvents();

    auto* w = new CompressWorker(in.toStdString(), out.toStdString(), algo,
                                  CompressWorker::Compress);
    connect(w, &CompressWorker::operationFinished, this, &MainWindow::onCompFinished);
    connect(w, &QThread::finished, w, &QObject::deleteLater);
    w->start();
}

void MainWindow::onDecompress() {
    QString in = m_compInputEdit->text().trimmed();
    QString out = m_compOutputEdit->text().trimmed();
    if (in.isEmpty() || out.isEmpty()) {
        QMessageBox::warning(this, "Missing Input", "Please specify input and output files.");
        return;
    }
    CompressAlgo algo = static_cast<CompressAlgo>(
        m_algoCombo->currentData().toInt());

    setInputsEnabled(false);
    m_compProgress->setVisible(true);
    m_compStatus->setText("Decompressing...");
    QApplication::processEvents();

    auto* w = new CompressWorker(in.toStdString(), out.toStdString(), algo,
                                  CompressWorker::Decompress);
    connect(w, &CompressWorker::operationFinished, this, &MainWindow::onCompFinished);
    connect(w, &QThread::finished, w, &QObject::deleteLater);
    w->start();
}

void MainWindow::onCompFinished(bool ok, const QString& msg) {
    setInputsEnabled(true);
    m_compProgress->setVisible(false);
    m_compStatus->setText(msg);
    if (ok) QMessageBox::information(this, "Success", msg);
    else    QMessageBox::critical(this, "Error", msg);
}

// =====================================================================
//  Encryption - Slots (standalone)
// =====================================================================

void MainWindow::onBrowseEncInput() {
    QString f = QFileDialog::getOpenFileName(this, "Select Input File",
                                              m_encInputEdit->text(), "All Files (*)");
    if (!f.isEmpty()) m_encInputEdit->setText(f);
}

void MainWindow::onBrowseEncFolder() {
    QString d = QFileDialog::getExistingDirectory(this, "Select Input Folder",
                                                    m_encInputEdit->text());
    if (!d.isEmpty()) m_encInputEdit->setText(d);
}

void MainWindow::onBrowseEncOutput() {
    QString f = QFileDialog::getSaveFileName(this, "Select Output File",
                                              m_encOutputEdit->text(), "All Files (*)");
    if (!f.isEmpty()) m_encOutputEdit->setText(f);
}

void MainWindow::onEncryptFile() {
    QString in = m_encInputEdit->text().trimmed();
    QString out = m_encOutputEdit->text().trimmed();
    QString pwd = m_encPwdEdit->text();
    if (in.isEmpty() || out.isEmpty() || pwd.isEmpty()) {
        QMessageBox::warning(this, "Missing Input",
                             "Please specify input, output, and password.");
        return;
    }
    setInputsEnabled(false);
    m_encProgress->setVisible(true);
    m_encStatus->setText("Encrypting...");
    QApplication::processEvents();

    try {
        namespace fs = std::filesystem;
        std::string inStr = in.toStdString(), outStr = out.toStdString(), pwdStr = pwd.toStdString();
        if (fs::is_directory(fs::u8path(inStr))) {
            std::string tmpArc = outStr + ".tmp_archive";
            datasoftware::BackupEngine::backup(inStr, tmpArc);
            datasoftware::Crypto::encryptFile(tmpArc, outStr, pwdStr);
            fs::remove(fs::u8path(tmpArc));
        } else {
            datasoftware::Crypto::encryptFile(inStr, outStr, pwdStr);
        }
        setInputsEnabled(true);
        m_encProgress->setVisible(false);
        m_encStatus->setText("Encryption complete.");
        QMessageBox::information(this, "Success", "File encrypted successfully.");
    } catch (const std::exception& e) {
        setInputsEnabled(true);
        m_encProgress->setVisible(false);
        m_encStatus->setText("Error: " + QString(e.what()));
        QMessageBox::critical(this, "Error", e.what());
    }
}

void MainWindow::onDecryptFile() {
    QString in = m_encInputEdit->text().trimmed();
    QString out = m_encOutputEdit->text().trimmed();
    QString pwd = m_encPwdEdit->text();
    if (in.isEmpty() || out.isEmpty() || pwd.isEmpty()) {
        QMessageBox::warning(this, "Missing Input",
                             "Please specify input, output, and password.");
        return;
    }
    setInputsEnabled(false);
    m_encProgress->setVisible(true);
    m_encStatus->setText("Decrypting...");
    QApplication::processEvents();

    try {
        std::string inStr = in.toStdString(), outStr = out.toStdString(), pwdStr = pwd.toStdString();
        datasoftware::Crypto::decryptFile(inStr, outStr, pwdStr);
        {
            std::ifstream _checkArc(outStr, std::ios::binary);
            if (_checkArc.is_open()) {
                char _magic[6] = {};
                _checkArc.read(_magic, 6);
                _checkArc.close();
                if (std::memcmp(_magic, "DATASW", 6) == 0) {
                    std::string _arcPath = outStr + ".tmp_arc";
                    std::filesystem::rename(std::filesystem::u8path(outStr), std::filesystem::u8path(_arcPath));
                    std::filesystem::create_directories(std::filesystem::u8path(outStr));
                    datasoftware::BackupEngine::restore(_arcPath, outStr);
                    std::filesystem::remove(std::filesystem::u8path(_arcPath));
                }
            }
        }
        setInputsEnabled(true);
    } catch (const std::exception& e) {
        setInputsEnabled(true);
        m_encProgress->setVisible(false);
        m_encStatus->setText("Error: " + QString(e.what()));
        QMessageBox::critical(this, "Error", e.what());
    }
}

void MainWindow::onEncFinished(bool ok, const QString& msg) {
    (void)ok; (void)msg;
}

// =====================================================================
//  Pack/Unpack - Worker
// =====================================================================

void PackWorker::run() {
    try {
        if (m_action == Pack) {
            // Read files into entries
            std::vector<datasoftware::FileEntry> entries;
            size_t total = m_fileList.size();
            for (int i = 0; i < m_fileList.size(); ++i) {
                std::string path = m_fileList[i].toStdString();
                std::string name = std::filesystem::path(path).filename().string();
                emit progressUpdated(i, total, QString::fromStdString(name));

                std::ifstream in(path, std::ios::binary | std::ios::ate);
                auto sz = in.tellg(); in.seekg(0);
                std::vector<char> buf(static_cast<size_t>(sz));
                if (sz > 0) in.read(buf.data(), sz);
                in.close();

                datasoftware::FileEntry fe(name, static_cast<uint64_t>(sz), std::move(buf));

                // Read metadata
                WIN32_FILE_ATTRIBUTE_DATA info;
                if (GetFileAttributesExW(std::filesystem::path(path).c_str(),
                                         GetFileExInfoStandard, &info)) {
                    fe.metadata.createTime = (static_cast<int64_t>(info.ftCreationTime.dwHighDateTime) << 32)
                                            | info.ftCreationTime.dwLowDateTime;
                    fe.metadata.modTime = (static_cast<int64_t>(info.ftLastWriteTime.dwHighDateTime) << 32)
                                         | info.ftLastWriteTime.dwLowDateTime;
                    fe.metadata.accessTime = (static_cast<int64_t>(info.ftLastAccessTime.dwHighDateTime) << 32)
                                            | info.ftLastAccessTime.dwLowDateTime;
                    fe.metadata.attributes = info.dwFileAttributes;
                }

                entries.push_back(std::move(fe));
            }

            emit progressUpdated(total, total, "Writing archive...");
            datasoftware::ArchiveWriter::write(m_dst, entries);
            emit operationFinished(true, QString("Packed %1 files into %2")
                                   .arg(total).arg(QString::fromStdString(m_dst)));
        } else {
            // Unpack
            emit progressUpdated(0, 1, "Reading archive...");
            auto entries = datasoftware::ArchiveReader::read(m_src);

            std::filesystem::path outDir(m_dst);
            std::filesystem::create_directories(outDir);

            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& entry = entries[i];
                emit progressUpdated(i, entries.size(),
                                     QString::fromStdString(entry.relativePath));

                auto filePath = outDir / entry.relativePath;
                std::filesystem::create_directories(filePath.parent_path());

                std::ofstream out(filePath, std::ios::binary);
                if (entry.fileSize > 0)
                    out.write(entry.data.data(), entry.fileSize);
                out.close();

                // Restore metadata
                if (!entry.metadata.isEmpty()) {
                    HANDLE hFile = CreateFileW(filePath.c_str(), FILE_WRITE_ATTRIBUTES,
                                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                                               nullptr, OPEN_EXISTING, 0, nullptr);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        FILETIME ct, at, wt;
                        ct.dwLowDateTime  = static_cast<DWORD>(entry.metadata.createTime & 0xFFFFFFFF);
                        ct.dwHighDateTime = static_cast<DWORD>(entry.metadata.createTime >> 32);
                        at.dwLowDateTime  = static_cast<DWORD>(entry.metadata.accessTime & 0xFFFFFFFF);
                        at.dwHighDateTime = static_cast<DWORD>(entry.metadata.accessTime >> 32);
                        wt.dwLowDateTime  = static_cast<DWORD>(entry.metadata.modTime & 0xFFFFFFFF);
                        wt.dwHighDateTime = static_cast<DWORD>(entry.metadata.modTime >> 32);
                        SetFileTime(hFile, &ct, &at, &wt);
                        CloseHandle(hFile);
                    }
                    if (entry.metadata.attributes != 0)
                        SetFileAttributesW(filePath.c_str(), entry.metadata.attributes);
                }
            }

            emit operationFinished(true, QString("Unpacked %1 files to %2")
                                   .arg(entries.size()).arg(QString::fromStdString(m_dst)));
        }
    } catch (const std::exception& e) {
        emit operationFinished(false, QString("Error: %1").arg(e.what()));
    }
}

// =====================================================================
//  Pack/Unpack - Slots
// =====================================================================

void MainWindow::onPackAddFiles() {
    QStringList files = QFileDialog::getOpenFileNames(this, "Select Files to Pack");
    if (files.isEmpty()) return;
    m_packFiles.append(files);
    refreshPackPreview();
}

void MainWindow::onPackClear() {
    m_packFiles.clear();
    refreshPackPreview();
}

void MainWindow::refreshPackPreview() {
    m_packPreview->clear();
    uint64_t totalSize = 0;
    for (const auto& f : m_packFiles) {
        QFileInfo fi(f);
        auto* item = new QTreeWidgetItem();
        item->setText(0, fi.fileName());
        qint64 sz = fi.size();
        item->setText(1, sz >= 1048576 ? QString("%1 MB").arg(sz/1048576.0,0,'f',2)
                     : sz >= 1024 ? QString("%1 KB").arg(sz/1024.0,0,'f',1)
                     : QString("%1 B").arg(sz));
        m_packPreview->addTopLevelItem(item);
        totalSize += static_cast<uint64_t>(sz);
    }
    if (!m_packFiles.isEmpty()) {
        double mb = static_cast<double>(totalSize) / 1048576.0;
        m_packSummary->setText(QString("%1 files, %2 MB total")
                               .arg(m_packFiles.size()).arg(mb, 0, 'f', 2));
    } else {
        m_packSummary->clear();
    }
}

void MainWindow::onPackBrowseOutput() {
    QString f = QFileDialog::getSaveFileName(this, "Select Output Archive",
                                              m_packOutputEdit->text(),
                                              "DAT (*.dat);;All Files (*)");
    if (!f.isEmpty()) m_packOutputEdit->setText(f);
}

void MainWindow::onUnpackBrowseInput() {
    QString f = QFileDialog::getOpenFileName(this, "Select Archive to Unpack",
                                              m_unpackInputEdit->text(),
                                              "DAT (*.dat);;All Files (*)");
    if (!f.isEmpty()) m_unpackInputEdit->setText(f);
}

void MainWindow::onUnpackBrowseOutput() {
    QString d = QFileDialog::getSaveFileName(this, "Select Output Directory",
                                              m_unpackOutputEdit->text(),
                                              "Folder (*)");
    if (!d.isEmpty()) m_unpackOutputEdit->setText(d);
}

void MainWindow::onPack() {
    if (m_packFiles.isEmpty()) {
        QMessageBox::warning(this, "Missing Input", "Please add files to pack.");
        return;
    }
    QString dst = m_packOutputEdit->text().trimmed();
    if (dst.isEmpty()) {
        QMessageBox::warning(this, "Missing Input", "Please specify output archive path.");
        return;
    }

    setInputsEnabled(false);
    m_packProgress->setValue(0);
    m_packProgress->setVisible(true);
    m_packStatus->setText("Packing...");
    QApplication::processEvents();

    auto* w = new PackWorker(m_packFiles, dst.toStdString());
    connect(w, &PackWorker::progressUpdated, this, &MainWindow::onPackProgress);
    connect(w, &PackWorker::operationFinished, this, &MainWindow::onPackFinished);
    connect(w, &QThread::finished, w, &QObject::deleteLater);
    w->start();
}

void MainWindow::onUnpack() {
    QString src = m_unpackInputEdit->text().trimmed();
    QString dst = m_unpackOutputEdit->text().trimmed();
    if (src.isEmpty() || dst.isEmpty()) {
        QMessageBox::warning(this, "Missing Input",
                             "Please specify archive file and output directory.");
        return;
    }

    setInputsEnabled(false);
    m_packProgress->setValue(0);
    m_packProgress->setVisible(true);
    m_packStatus->setText("Unpacking...");
    QApplication::processEvents();

    auto* w = new PackWorker(src.toStdString(), dst.toStdString());
    connect(w, &PackWorker::progressUpdated, this, &MainWindow::onPackProgress);
    connect(w, &PackWorker::operationFinished, this, &MainWindow::onPackFinished);
    connect(w, &QThread::finished, w, &QObject::deleteLater);
    w->start();
}

void MainWindow::onPackProgress(quint64 current, quint64 total, const QString& file) {
    if (total > 0) {
        m_packProgress->setRange(0, static_cast<int>(total));
        m_packProgress->setValue(static_cast<int>(current));
    }
    if (!file.isEmpty()) m_packStatus->setText(file);
}

void MainWindow::onPackFinished(bool ok, const QString& msg) {
    setInputsEnabled(true);
    m_packProgress->setValue(m_packProgress->maximum());
    m_packStatus->setText(msg);
    if (ok) QMessageBox::information(this, "Success", msg);
    else    QMessageBox::critical(this, "Error", msg);
}

} // namespace datasoftware
