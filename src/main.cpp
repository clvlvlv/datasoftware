#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include "datasoftware/BackupEngine.h"
#include "datasoftware/ArchiveWriter.h"
#include "datasoftware/ArchiveReader.h"
#include "datasoftware/Compressor.h"
#include "datasoftware/Crypto.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void printUsage(const char* progName) {
    std::cerr << "Usage:" << std::endl;
    std::cerr << "  " << progName << " backup     <source-dir> <archive-file>" << std::endl;
    std::cerr << "  " << progName << " restore    <archive-file> <restore-dir>" << std::endl;
    std::cerr << "  " << progName << " pack       <file1> [file2 ...] <archive-file>" << std::endl;
    std::cerr << "  " << progName << " unpack     <archive-file> <output-dir>" << std::endl;
    std::cerr << "  " << progName << " compress   <input> <output> <algo: 0=RLE,1=LZ77,2=Huffman>" << std::endl;
    std::cerr << "  " << progName << " decompress <input> <output> <algo: 0=RLE,1=LZ77,2=Huffman>" << std::endl;
    std::cerr << "  " << progName << " encrypt    <input> <output> <password>" << std::endl;
    std::cerr << "  " << progName << " decrypt    <input> <output> <password>" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Examples:" << std::endl;
    std::cerr << "  " << progName << " backup     ./mydata    ./backup.dat" << std::endl;
    std::cerr << "  " << progName << " restore    ./backup.dat ./restored" << std::endl;
    std::cerr << "  " << progName << " pack       a.txt b.txt ./packed.dat" << std::endl;
    std::cerr << "  " << progName << " unpack     ./packed.dat ./outdir" << std::endl;
    std::cerr << "  " << progName << " compress   data.bin data.rle 0" << std::endl;
    std::cerr << "  " << progName << " decompress data.rle data.bin 0" << std::endl;
    std::cerr << "  " << progName << " encrypt    secret.txt secret.enc mypassword" << std::endl;
    std::cerr << "  " << progName << " decrypt    secret.enc secret.txt mypassword" << std::endl;
}

std::string formatTime(double seconds) {
    if (seconds < 1.0) {
        return std::to_string(static_cast<int>(seconds * 1000)) + " ms";
    } else if (seconds < 60.0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f s", seconds);
        return buf;
    } else {
        int mins = static_cast<int>(seconds) / 60;
        int secs = static_cast<int>(seconds) % 60;
        return std::to_string(mins) + " min " + std::to_string(secs) + " s";
    }
}

// Progress callback that outputs to stdout in a parseable format
static auto makeProgressCallback(const std::string& actionName) {
    return [actionName](size_t current, size_t total, const std::string& msg) {
        std::cout << "[PROGRESS] " << current << " " << total << " " << msg << std::endl;
    };
}

int doPack(int argc, char* argv[]) {
    std::string archivePath = argv[argc - 1];
    std::vector<std::string> files;
    for (int i = 2; i < argc - 1; ++i)
        files.push_back(argv[i]);

    auto start = std::chrono::steady_clock::now();

    std::vector<datasoftware::FileEntry> entries;
    size_t total = files.size();
    for (size_t idx = 0; idx < total; ++idx) {
        const auto& path = files[idx];
        std::string name = std::filesystem::path(path).filename().string();
        std::cout << "[PROGRESS] " << idx << " " << total << " " << name << std::endl;

        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in.is_open()) {
            std::cout << "[DONE] ERR Cannot open file: " << path << std::endl;
            return 1;
        }
        auto sz = in.tellg(); in.seekg(0);
        std::vector<char> buf(static_cast<size_t>(sz));
        if (sz > 0) in.read(buf.data(), sz);
        in.close();

        datasoftware::FileEntry fe(name, static_cast<uint64_t>(sz), std::move(buf));

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

    std::cout << "[PROGRESS] " << total << " " << total << " Writing archive..." << std::endl;
    datasoftware::ArchiveWriter::write(archivePath, entries);

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "[DONE] OK Packed " << entries.size() << " files into " << archivePath
              << " (" << formatTime(elapsed) << ")" << std::endl;
    return 0;
}

int doUnpack(const std::string& archivePath, const std::string& outputDir) {
    auto start = std::chrono::steady_clock::now();

    auto entries = datasoftware::ArchiveReader::read(archivePath);
    std::filesystem::create_directories(outputDir);

    size_t total = 0;
    for (const auto& entry : entries) {
        if (entry.fileType != datasoftware::FileType::Directory)
            total++;
    }

    size_t current = 0;
    for (const auto& entry : entries) {
        if (entry.fileType == datasoftware::FileType::Directory) {
            auto filePath = std::filesystem::u8path(outputDir) / std::filesystem::u8path(entry.relativePath);
            std::filesystem::create_directories(filePath);
            continue;
        }

        std::cout << "[PROGRESS] " << current << " " << total << " " << entry.relativePath << std::endl;

        auto filePath = std::filesystem::u8path(outputDir) / std::filesystem::u8path(entry.relativePath);
        std::filesystem::create_directories(filePath.parent_path());

        std::ofstream out(filePath, std::ios::binary);
        if (entry.fileSize > 0)
            out.write(entry.data.data(), entry.fileSize);
        out.close();

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
        current++;
    }

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "[DONE] OK Unpacked " << total << " files to " << outputDir
              << " (" << formatTime(elapsed) << ")" << std::endl;
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    try {
        if (command == "backup" || command == "b") {
            if (argc != 4) { printUsage(argv[0]); return 1; }
            auto start = std::chrono::steady_clock::now();
            std::cout << "Backing up: " << argv[2] << " -> " << argv[3] << std::endl;
            size_t count = datasoftware::BackupEngine::backup(argv[2], argv[3],
                makeProgressCallback("backup"));
            auto end = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(end - start).count();
            std::cout << "[DONE] OK Backup complete! " << count << " files ("
                      << formatTime(elapsed) << ")." << std::endl;

        } else if (command == "restore" || command == "r") {
            if (argc != 4) { printUsage(argv[0]); return 1; }
            auto start = std::chrono::steady_clock::now();
            std::cout << "Restoring: " << argv[2] << " -> " << argv[3] << std::endl;
            size_t count = datasoftware::BackupEngine::restore(argv[2], argv[3],
                makeProgressCallback("restore"));
            auto end = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(end - start).count();
            std::cout << "[DONE] OK Restore complete! " << count << " files ("
                      << formatTime(elapsed) << ")." << std::endl;

        } else if (command == "pack" || command == "p") {
            if (argc < 4) { printUsage(argv[0]); return 1; }
            return doPack(argc, argv);

        } else if (command == "unpack" || command == "u") {
            if (argc != 4) { printUsage(argv[0]); return 1; }
            return doUnpack(argv[2], argv[3]);

        } else if (command == "compress" || command == "c") {
            if (argc != 5) { printUsage(argv[0]); return 1; }
            std::string inputPath = argv[2];
            std::string outputPath = argv[3];
            int algoIdx = std::stoi(argv[4]);
            auto algo = static_cast<datasoftware::CompressAlgo>(algoIdx);
            auto start = std::chrono::steady_clock::now();
            std::cout << "Compressing: " << inputPath << " -> " << outputPath
                      << " (" << datasoftware::Compressor::name(algo) << ")" << std::endl;
            datasoftware::Compressor::compressFile(inputPath, outputPath, algo);
            auto end = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(end - start).count();

            auto inSize = std::filesystem::file_size(inputPath);
            auto outSize = std::filesystem::file_size(outputPath);
            double ratio = inSize > 0 ? (1.0 - (double)outSize / inSize) * 100.0 : 0.0;
            std::cout << "[DONE] OK Compress complete! Ratio: " << std::fixed
                      << std::setprecision(1) << ratio << "% ("
                      << formatTime(elapsed) << ")." << std::endl;

        } else if (command == "decompress" || command == "d") {
            if (argc != 5) { printUsage(argv[0]); return 1; }
            std::string inputPath = argv[2];
            std::string outputPath = argv[3];
            int algoIdx = std::stoi(argv[4]);
            auto algo = static_cast<datasoftware::CompressAlgo>(algoIdx);
            auto start = std::chrono::steady_clock::now();
            std::cout << "Decompressing: " << inputPath << " -> " << outputPath
                      << " (" << datasoftware::Compressor::name(algo) << ")" << std::endl;
            datasoftware::Compressor::decompressFile(inputPath, outputPath, algo);
            auto end = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(end - start).count();
            std::cout << "[DONE] OK Decompress complete! ("
                      << formatTime(elapsed) << ")." << std::endl;

        } else if (command == "encrypt" || command == "e") {
            if (argc != 5) { printUsage(argv[0]); return 1; }
            std::string inputPath = argv[2];
            std::string outputPath = argv[3];
            std::string password = argv[4];
            auto start = std::chrono::steady_clock::now();
            std::cout << "Encrypting: " << inputPath << " -> " << outputPath << std::endl;
            datasoftware::Crypto::encryptFile(inputPath, outputPath, password);
            auto end = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(end - start).count();
            std::cout << "[DONE] OK Encrypt complete! ("
                      << formatTime(elapsed) << ")." << std::endl;

        } else if (command == "decrypt") {
            if (argc != 5) { printUsage(argv[0]); return 1; }
            std::string inputPath = argv[2];
            std::string outputPath = argv[3];
            std::string password = argv[4];
            auto start = std::chrono::steady_clock::now();
            std::cout << "Decrypting: " << inputPath << " -> " << outputPath << std::endl;
            datasoftware::Crypto::decryptFile(inputPath, outputPath, password);
            auto end = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(end - start).count();
            std::cout << "[DONE] OK Decrypt complete! ("
                      << formatTime(elapsed) << ")." << std::endl;

        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "[DONE] ERR " << e.what() << std::endl;
        return 1;
    }

    return 0;
}