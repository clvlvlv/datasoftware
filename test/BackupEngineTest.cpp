#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

#include "datasoftware/FileTraverser.h"
#include "datasoftware/ArchiveWriter.h"
#include "datasoftware/ArchiveReader.h"
#include "datasoftware/BackupEngine.h"
#include "datasoftware/Compressor.h"
#include "datasoftware/Crypto.h"

namespace fs = std::filesystem;
using namespace datasoftware;

// === Test helpers ===

std::string testDir;
std::string srcDir;
std::string restoreDir;
std::string archivePath;

void setup() {
    testDir = (fs::temp_directory_path() / "dsbackup_test").string();
    srcDir = testDir + "/source";
    restoreDir = testDir + "/restore";
    archivePath = testDir + "/backup.dat";

    fs::remove_all(testDir);
    fs::create_directories(srcDir);
    fs::create_directories(restoreDir);
}

void createFile(const std::string& path, const std::string& content) {
    fs::path filePath(path);
    fs::create_directories(filePath.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), content.size());
    out.close();
}

bool fileContentEquals(const std::string& path, const std::string& expected) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return false;
    auto size = in.tellg();
    if (size != static_cast<std::streamoff>(expected.size())) return false;
    in.seekg(0);
    std::vector<char> buf(size);
    in.read(buf.data(), size);
    return std::memcmp(buf.data(), expected.data(), size) == 0;
}

int passed = 0;
int failed = 0;

#define TEST(name) \
    do { \
        std::cout << "  " << name << " ... "; \
        try {

#define END_TEST(name) \
            std::cout << "PASS" << std::endl; \
            passed++; \
        } catch (const std::exception& e) { \
            std::cout << "FAIL (" << e.what() << ")" << std::endl; \
            failed++; \
        } catch (...) { \
            std::cout << "FAIL (unknown error)" << std::endl; \
            failed++; \
        } \
    } while(0)

// === Test cases ===

void testTraverseEmptyDir() {
    TEST("Traverse empty directory")
        auto entries = FileTraverser::traverse(srcDir);
        assert(entries.empty());
    END_TEST("Traverse empty directory");
}

void testTraverseSingleFile() {
    TEST("Traverse single file")
        createFile(srcDir + "/hello.txt", "Hello World");
        auto entries = FileTraverser::traverse(srcDir);
        assert(entries.size() == 1);
        assert(entries[0].relativePath == "hello.txt");
        assert(entries[0].fileSize == 11);
        assert(std::string(entries[0].data.data(), 11) == "Hello World");
    END_TEST("Traverse single file");
}

void testTraverseNestedFiles() {
    TEST("Traverse nested directories")
        createFile(srcDir + "/a.txt", "aaa");
        createFile(srcDir + "/sub/b.txt", "bbb");
        createFile(srcDir + "/sub/deep/c.txt", "ccc");
        auto entries = FileTraverser::traverse(srcDir);
        assert(entries.size() == 3);
    END_TEST("Traverse nested directories");
}

void testArchiveRoundTrip() {
    TEST("Archive round-trip (write + read)")
        std::vector<FileEntry> original = {
            FileEntry("test.txt", 5, {'H', 'e', 'l', 'l', 'o'}),
            FileEntry("sub/file.bin", 3, {0x01, 0x02, 0x03}),
            FileEntry("empty.dat", 0, {})
        };
        ArchiveWriter::write(archivePath, original);
        auto loaded = ArchiveReader::read(archivePath);
        assert(loaded.size() == 3);
        assert(loaded[0].relativePath == "test.txt");
        assert(loaded[0].fileSize == 5);
        assert(loaded[2].fileSize == 0);
    END_TEST("Archive round-trip (write + read)");
}

void testBackupAndRestore() {
    TEST("Full backup and restore flow")
        createFile(srcDir + "/doc.txt", "document content");
        createFile(srcDir + "/images/pic.dat", std::string(100, 'A'));
        createFile(srcDir + "/config/settings.ini", "[config]\nkey=value\n");

        size_t backedUp = BackupEngine::backup(srcDir, archivePath);
        assert(backedUp == 3);
        assert(fs::exists(archivePath));

        size_t restored = BackupEngine::restore(archivePath, restoreDir);
        assert(restored == 3);

        assert(fileContentEquals(restoreDir + "/doc.txt", "document content"));
        assert(fileContentEquals(restoreDir + "/images/pic.dat", std::string(100, 'A')));
        assert(fileContentEquals(restoreDir + "/config/settings.ini", "[config]\nkey=value\n"));
    END_TEST("Full backup and restore flow");
}

void testEmptyDirBackup() {
    TEST("Backup and restore empty directory")
        fs::remove_all(srcDir);
        fs::create_directories(srcDir);

        size_t backedUp = BackupEngine::backup(srcDir, archivePath);
        assert(backedUp == 0);
        assert(fs::file_size(archivePath) > 0);

        fs::remove_all(restoreDir);
        fs::create_directories(restoreDir);
        size_t restored = BackupEngine::restore(archivePath, restoreDir);
        assert(restored == 0);
    END_TEST("Backup and restore empty directory");
}

void testInvalidArchive() {
    TEST("Detect invalid archive")
        {
            std::ofstream out(archivePath, std::ios::binary);
            out << "NOTAVALIDARCHIVE";
        }
        bool caught = false;
        try {
            ArchiveReader::read(archivePath);
        } catch (const std::runtime_error&) {
            caught = true;
        }
        assert(caught);
    END_TEST("Detect invalid archive");
}

// === File type support tests ===

void testTraverseIncludesDirectories() {
    TEST("Traverse includes directory entries")
        createFile(srcDir + "/a/b/c/file.txt", "nested");
        auto entries = FileTraverser::traverse(srcDir);
        // Should have: a/, a/b/, a/b/c/, a/b/c/file.txt
        bool hasDirA = false, hasDirB = false, hasDirC = false;
        for (const auto& e : entries) {
            if (e.fileType == FileType::Directory) {
                if (e.relativePath == "a") hasDirA = true;
                if (e.relativePath == "a/b") hasDirB = true;
                if (e.relativePath == "a/b/c") hasDirC = true;
            }
        }
        assert(hasDirA && hasDirB && hasDirC);
    END_TEST("Traverse includes directory entries");
}

void testSymlinkRoundTrip() {
    TEST("Symlink file type round-trip")
        FileEntry sym("link_target.txt", FileType::Symlink, 0,
                      std::vector<char>{}, "../some/target");
        std::vector<FileEntry> entries = {sym};
        ArchiveWriter::write(archivePath, entries);
        auto loaded = ArchiveReader::read(archivePath);
        assert(loaded.size() == 1);
        assert(loaded[0].fileType == FileType::Symlink);
        assert(loaded[0].symlinkTarget == "../some/target");
        assert(loaded[0].fileSize == 0);
    END_TEST("Symlink file type round-trip");
}

void testFifoRoundTrip() {
    TEST("FIFO file type round-trip")
        FileEntry fifo("my_pipe", FileType::Fifo, 0, std::vector<char>{});
        std::vector<FileEntry> entries = {fifo};
        ArchiveWriter::write(archivePath, entries);
        auto loaded = ArchiveReader::read(archivePath);
        assert(loaded.size() == 1);
        assert(loaded[0].fileType == FileType::Fifo);
    END_TEST("FIFO file type round-trip");
}

void testDeviceRoundTrip() {
    TEST("Device file type round-trip")
        FileEntry dev("null_dev", FileType::Device, 0,
                      std::vector<char>{}, "", 0, 1, 3);
        std::vector<FileEntry> entries = {dev};
        ArchiveWriter::write(archivePath, entries);
        auto loaded = ArchiveReader::read(archivePath);
        assert(loaded.size() == 1);
        assert(loaded[0].fileType == FileType::Device);
        assert(loaded[0].deviceMajor == 1);
        assert(loaded[0].deviceMinor == 3);
    END_TEST("Device file type round-trip");
}

void testHardLinkRoundTrip() {
    TEST("Hard link file type round-trip")
        FileEntry hlink("linked_file.txt", FileType::HardLink, 5,
                        std::vector<char>{'h', 'e', 'l', 'l', 'o'}, "", 42);
        std::vector<FileEntry> entries = {hlink};
        ArchiveWriter::write(archivePath, entries);
        auto loaded = ArchiveReader::read(archivePath);
        assert(loaded.size() == 1);
        assert(loaded[0].fileType == FileType::HardLink);
        assert(loaded[0].hardLinkId == 42);
        assert(loaded[0].fileSize == 5);
    END_TEST("Hard link file type round-trip");
}

void testBackwardCompatV1() {
    TEST("Backward compatibility with v1 archive")
        // Build a v1-format archive manually
        {
            std::ofstream out(archivePath, std::ios::binary);
            // Magic
            out.write("DATASW", 6);
            // Reserved
            uint16_t res = 0;
            out.write(reinterpret_cast<const char*>(&res), 2);
            // Version = 1
            uint32_t ver = 1;
            out.write(reinterpret_cast<const char*>(&ver), 4);
            // EntryCount = 2
            uint32_t count = 2;
            out.write(reinterpret_cast<const char*>(&count), 4);
            // Entry 1
            uint32_t plen = 8;
            out.write(reinterpret_cast<const char*>(&plen), 4);
            out.write("v1_file.txt", 8);
            uint64_t fsize = 5;
            out.write(reinterpret_cast<const char*>(&fsize), 8);
            out.write("Hello", 5);
            // Entry 2
            plen = 10;
            out.write(reinterpret_cast<const char*>(&plen), 4);
            out.write("empty_v1.bin", 10);
            fsize = 0;
            out.write(reinterpret_cast<const char*>(&fsize), 8);
        }
        auto loaded = ArchiveReader::read(archivePath);
        assert(loaded.size() == 2);
        assert(loaded[0].relativePath == "v1_file.txt");
        assert(loaded[0].fileSize == 5);
        assert(loaded[0].fileType == FileType::Regular);
        assert(loaded[1].relativePath == "empty_v1.bin");
        assert(loaded[1].fileSize == 0);
    END_TEST("Backward compatibility with v1 archive");
}

void testBackupRestoreSymlink() {
    // Check if symlinks are supported on this system
    std::error_code probeEc;
    fs::path probeLink = testDir + "/_probe_link";
    fs::path probeTarget = testDir + "/_probe_target";
    { std::ofstream o(probeTarget); o << "x"; }
    fs::create_symlink("_probe_target", probeLink, probeEc);
    bool symlinksOk = !probeEc;
    fs::remove_all(probeLink, probeEc);
    fs::remove_all(probeTarget, probeEc);

    if (!symlinksOk) {
        TEST("Backup/restore symlink (SKIPPED - not supported)")
            FileEntry se("link.txt", FileType::Symlink, 0,
                         std::vector<char>{}, "../target");
            ArchiveWriter::write(archivePath, {se});
            auto loaded = ArchiveReader::read(archivePath);
            assert(loaded.size() == 1);
            assert(loaded[0].fileType == FileType::Symlink);
            std::cout << "(format verified)";
            passed++;
        END_TEST("Backup/restore symlink");
        return;
    }

    TEST("Backup and restore with symlink")
        createFile(srcDir + "/real_file.txt", "symlink content");
        std::error_code ec;
        fs::create_symlink("real_file.txt",
                           srcDir + "/link_to_real.txt", ec);
        assert(!ec);

        size_t backedUp = BackupEngine::backup(srcDir, archivePath);
        std::cout << "(" << backedUp << " entries in archive) ";

        // Debug: check what's in the archive
        auto debugEntries = ArchiveReader::read(archivePath);
        for (const auto& de : debugEntries) {
            std::cout << "[" << de.relativePath
                      << " type=" << (int)de.fileType
                      << " size=" << de.fileSize << "] ";
        }

        // Test restore to a temp subdir
        std::string restoreSub = restoreDir + "/_out";
        size_t restored = BackupEngine::restore(archivePath, restoreSub);
        assert(restored == backedUp);

        assert(fileContentEquals(restoreSub + "/real_file.txt",
                                 "symlink content"));
    END_TEST("Backup and restore with symlink");
}

// === Compression tests ===

void testRleRoundTrip() {
    TEST("RLE round-trip")
        std::vector<char> original = {
            'A', 'A', 'A', 'B', 'B', 'C', 'D', 'D', 'D', 'D'
        };
        auto compressed = Compressor::compress(original, CompressAlgo::RLE);
        auto decompressed = Compressor::decompress(compressed, CompressAlgo::RLE);
        assert(decompressed.size() == original.size());
        assert(std::memcmp(decompressed.data(), original.data(), original.size()) == 0);
        // Compression should be smaller for repetitive data
        assert(compressed.size() < original.size());
    END_TEST("RLE round-trip");
}

void testRleNoRepeat() {
    TEST("RLE with no repeats")
        std::vector<char> original = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        auto compressed = Compressor::compress(original, CompressAlgo::RLE);
        auto decompressed = Compressor::decompress(compressed, CompressAlgo::RLE);
        assert(decompressed.size() == original.size());
        assert(std::memcmp(decompressed.data(), original.data(), original.size()) == 0);
    END_TEST("RLE with no repeats");
}

void testRleEmpty() {
    TEST("RLE empty input")
        std::vector<char> original;
        auto compressed = Compressor::compress(original, CompressAlgo::RLE);
        auto decompressed = Compressor::decompress(compressed, CompressAlgo::RLE);
        assert(decompressed.empty());
    END_TEST("RLE empty input");
}

void testLz77RoundTrip() {
    TEST("LZ77 round-trip")
        std::string text = "ABABABABABABABABABABABABABABABAB";
        std::vector<char> original(text.begin(), text.end());
        auto compressed = Compressor::compress(original, CompressAlgo::LZ77);
        auto decompressed = Compressor::decompress(compressed, CompressAlgo::LZ77);
        assert(decompressed.size() == original.size());
        assert(std::memcmp(decompressed.data(), original.data(), original.size()) == 0);
        // LZ77 should compress repetitive patterns well
        assert(compressed.size() < original.size());
    END_TEST("LZ77 round-trip");
}

void testLz77General() {
    TEST("LZ77 general data")
        std::string text = "The quick brown fox jumps over the lazy dog. "
                           "The quick brown fox jumps over the lazy dog again!";
        std::vector<char> original(text.begin(), text.end());
        auto compressed = Compressor::compress(original, CompressAlgo::LZ77);
        auto decompressed = Compressor::decompress(compressed, CompressAlgo::LZ77);
        assert(decompressed.size() == original.size());
        assert(std::memcmp(decompressed.data(), original.data(), original.size()) == 0);
    END_TEST("LZ77 general data");
}

void testCompressFileRoundTrip() {
    TEST("Compress file round-trip (RLE)")
        std::string testFile = testDir + "/compress_test.bin";
        std::string compFile = testDir + "/compressed.rlz";
        std::string decompFile = testDir + "/decompressed.bin";

        std::vector<char> data(5000, 'X');
        for (size_t i = 0; i < 100; ++i) data[i * 50] = static_cast<char>(i);

        {
            std::ofstream out(testFile, std::ios::binary);
            out.write(data.data(), data.size());
        }

        Compressor::compressFile(testFile, compFile, CompressAlgo::RLE);
        Compressor::decompressFile(compFile, decompFile, CompressAlgo::RLE);

        std::ifstream in(decompFile, std::ios::binary | std::ios::ate);
        auto size = in.tellg();
        assert(static_cast<size_t>(size) == data.size());
        in.seekg(0);
        std::vector<char> result(size);
        in.read(result.data(), size);
        assert(std::memcmp(result.data(), data.data(), data.size()) == 0);
    END_TEST("Compress file round-trip (RLE)");
}

void testHuffmanRoundTrip() {
    TEST("Huffman round-trip")
        std::vector<char> orig = {
            'A', 'B', 'C', 'A', 'B', 'C', 'A', 'B', 'C',
            'A', 'A', 'A', 'A', 'B', 'B', 'B', 'C', 'C'
        };
        auto comp = Compressor::compress(orig, CompressAlgo::Huffman);
        auto dec = Compressor::decompress(comp, CompressAlgo::Huffman);
        assert(dec.size() == orig.size());
        assert(std::memcmp(dec.data(), orig.data(), orig.size()) == 0);
    END_TEST("Huffman round-trip");
}

void testHuffmanAllBytes() {
    TEST("Huffman all 256 byte values")
        std::vector<char> orig(256 * 2, 0);
        // Each byte value appears twice (balanced)
        for (int i = 0; i < 256; ++i) {
            orig[i] = static_cast<char>(i);
            orig[256 + i] = static_cast<char>(i);
        }
        auto comp = Compressor::compress(orig, CompressAlgo::Huffman);
        auto dec = Compressor::decompress(comp, CompressAlgo::Huffman);
        assert(dec.size() == orig.size());
        assert(std::memcmp(dec.data(), orig.data(), orig.size()) == 0);
        // With balanced frequencies, compression ratio should be reasonable
        // (can't be smaller due to overhead, but shouldn't error)
    END_TEST("Huffman all 256 byte values");
}

void testHuffmanEmpty() {
    TEST("Huffman empty input")
        std::vector<char> orig;
        auto comp = Compressor::compress(orig, CompressAlgo::Huffman);
        auto dec = Compressor::decompress(comp, CompressAlgo::Huffman);
        assert(dec.empty());
    END_TEST("Huffman empty input");
}

void testHuffmanLargeData() {
    TEST("Huffman large data (100KB)")
        std::vector<char> orig(100000, 0);
        for (size_t i = 0; i < 100000; ++i)
            orig[i] = static_cast<char>((i * 7 + 13) % 251); // pseudo-random
        auto comp = Compressor::compress(orig, CompressAlgo::Huffman);
        auto dec = Compressor::decompress(comp, CompressAlgo::Huffman);
        assert(dec.size() == orig.size());
        assert(std::memcmp(dec.data(), orig.data(), orig.size()) == 0);
        std::cout << "(" << comp.size() << "/" << orig.size() << " bytes) ";
    END_TEST("Huffman large data (100KB)");
}

// === Crypto tests ===

void testCryptoRoundTrip() {
    TEST("Crypto encrypt/decrypt round-trip")
        std::vector<char> orig = {'H','e','l','l','o',' ','W','o','r','l','d','!','1','2','3','4','5'};
        std::string pwd = "test123";
        auto enc = Crypto::encrypt(orig, pwd);
        assert(Crypto::isEncrypted(enc));
        auto dec = Crypto::decrypt(enc, pwd);
        assert(dec.size() == orig.size());
        assert(std::memcmp(dec.data(), orig.data(), orig.size()) == 0);
    END_TEST("Crypto encrypt/decrypt round-trip");
}

void testCryptoWrongPassword() {
    TEST("Crypto wrong password rejected")
        std::vector<char> orig = {'t','e','s','t'};
        std::string pwd = "correct";
        auto enc = Crypto::encrypt(orig, pwd);
        bool caught = false;
        try {
            Crypto::decrypt(enc, "wrong");
        } catch (const std::runtime_error&) {
            caught = true;
        }
        assert(caught);
    END_TEST("Crypto wrong password rejected");
}

void testCryptoLarge() {
    TEST("Crypto large data (1MB)")
        std::vector<char> orig(1000000, 'A');
        std::string pwd = "longpassword123456";
        auto enc = Crypto::encrypt(orig, pwd);
        auto dec = Crypto::decrypt(enc, pwd);
        assert(dec.size() == orig.size());
        assert(std::memcmp(dec.data(), orig.data(), orig.size()) == 0);
    END_TEST("Crypto large data (1MB)");
}

// === Metadata tests ===

void testMetadataRoundTrip() {
    TEST("Metadata round-trip (v3 format)")
        std::string testFile = testDir + "/metadata_test.txt";
        std::string archiveFile = testDir + "/meta_archive.dat";
        std::string restoreFile = testDir + "/restored/meta_test.txt";

        { std::ofstream out(testFile); out << "hello"; }

        // Backup and restore
        BackupEngine::backup(testDir, archiveFile);
        fs::remove_all(testDir + "/restored");
        BackupEngine::restore(archiveFile, testDir + "/restored");

        // Check file exists and content is correct
        assert(fs::exists(restoreFile));
        std::ifstream in(restoreFile);
        std::string content; in >> content;
        assert(content == "hello");
        in.close();

        // Verify v3 format was written
        std::ifstream arc(archiveFile, std::ios::binary);
        char magic[6]; arc.read(magic, 6);
        arc.seekg(8);
        uint32_t ver; arc.read(reinterpret_cast<char*>(&ver), 4);
        assert(ver == 3);
        arc.close();
    END_TEST("Metadata round-trip (v3 format)");
}

void testMetadataTimestamp() {
    TEST("Metadata timestamp preservation")
        std::string testFile = testDir + "/time_test.txt";
        std::string archiveFile = testDir + "/time_archive.dat";
        std::string restoredDir = testDir + "/time_restored";

        { std::ofstream out(testFile); out << "data"; }

        // Read original timestamps using Windows API
        int64_t origCreate = 0, origWrite = 0;
        WIN32_FILE_ATTRIBUTE_DATA w32;
        std::wstring wpath(testFile.begin(), testFile.end());
        if (GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &w32)) {
            origCreate = (static_cast<int64_t>(w32.ftCreationTime.dwHighDateTime) << 32)
                        | w32.ftCreationTime.dwLowDateTime;
            origWrite = (static_cast<int64_t>(w32.ftLastWriteTime.dwHighDateTime) << 32)
                       | w32.ftLastWriteTime.dwLowDateTime;
        }
        assert(origCreate != 0);

        // Backup and restore
        BackupEngine::backup(testDir, archiveFile);
        fs::remove_all(restoredDir);
        BackupEngine::restore(archiveFile, restoredDir);

        std::string restoredFile = restoredDir + "/time_test.txt";
        assert(fs::exists(restoredFile));

        // Check content preserved
        std::ifstream in(restoredFile);
        std::string content; in >> content;
        assert(content == "data");
        in.close();

        // Compare timestamps after restore
        int64_t restCreate = 0, restWrite = 0;
        std::wstring rpath(restoredFile.begin(), restoredFile.end());
        if (GetFileAttributesExW(rpath.c_str(), GetFileExInfoStandard, &w32)) {
            restCreate = (static_cast<int64_t>(w32.ftCreationTime.dwHighDateTime) << 32)
                        | w32.ftCreationTime.dwLowDateTime;
            restWrite = (static_cast<int64_t>(w32.ftLastWriteTime.dwHighDateTime) << 32)
                       | w32.ftLastWriteTime.dwLowDateTime;
        }

        assert(origCreate == restCreate);
        assert(origWrite == restWrite);
        std::cout << "(timestamps match) ";
    END_TEST("Metadata timestamp preservation");
}

void testBackupFilesMetadata() {
    TEST("backupFiles metadata preservation")
        std::string testFile = testDir + "/single_file.txt";
        std::string archiveFile = testDir + "/bf_archive.dat";
        std::string restoredDir = testDir + "/bf_restored";

        { std::ofstream out(testFile); out << "data"; }

        // Set specific timestamp
        int64_t origWrite = 0;
        WIN32_FILE_ATTRIBUTE_DATA w32;
        std::wstring wpath(testFile.begin(), testFile.end());
        if (GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &w32)) {
            origWrite = (static_cast<int64_t>(w32.ftLastWriteTime.dwHighDateTime) << 32)
                       | w32.ftLastWriteTime.dwLowDateTime;
        }

        // Use backupFiles (simulates GUI "Add Files..." mode)
        BackupEngine::backupFiles({testFile}, archiveFile);

        // Restore
        fs::remove_all(restoredDir);
        BackupEngine::restore(archiveFile, restoredDir);

        std::string restoredFile = restoredDir + "/single_file.txt";
        int64_t restWrite = 0;
        std::wstring rpath(restoredFile.begin(), restoredFile.end());
        if (GetFileAttributesExW(rpath.c_str(), GetFileExInfoStandard, &w32)) {
            restWrite = (static_cast<int64_t>(w32.ftLastWriteTime.dwHighDateTime) << 32)
                       | w32.ftLastWriteTime.dwLowDateTime;
        }

        assert(origWrite == restWrite);
        std::cout << "(backupFiles timestamps match) ";
    END_TEST("backupFiles metadata preservation");
}

// === Main ===

int main() {
    std::cout << "=== Data Backup Software - Basic Function Tests ===" << std::endl;
    std::cout << std::endl;

    // Clean up any leftover test directory
    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / "dsbackup_test", ec);
    setup();
    testTraverseEmptyDir();

    setup();
    testTraverseSingleFile();

    setup();
    testTraverseNestedFiles();

    setup();
    testArchiveRoundTrip();

    setup();
    testBackupAndRestore();

    setup();
    testEmptyDirBackup();

    setup();
    testInvalidArchive();

    // File type support tests
    std::cout << std::endl;
    std::cout << "--- File Type Support ---" << std::endl;

    setup();
    testTraverseIncludesDirectories();

    setup();
    testSymlinkRoundTrip();

    setup();
    testFifoRoundTrip();

    setup();
    testDeviceRoundTrip();

    setup();
    testHardLinkRoundTrip();

    setup();
    testBackwardCompatV1();

    setup();
    testBackupRestoreSymlink();

    // Compression tests
    std::cout << std::endl;
    std::cout << "--- Compression ---" << std::endl;

    setup();
    testRleRoundTrip();

    setup();
    testRleNoRepeat();

    setup();
    testRleEmpty();

    setup();
    testLz77RoundTrip();

    setup();
    testLz77General();

    setup();
    testCompressFileRoundTrip();

    setup();
    testHuffmanRoundTrip();

    setup();
    testHuffmanAllBytes();

    setup();
    testHuffmanEmpty();

    setup();
    testHuffmanLargeData();

    // Crypto tests
    std::cout << std::endl;
    std::cout << "--- Encryption ---" << std::endl;

    setup();
    testCryptoRoundTrip();

    setup();
    testCryptoWrongPassword();

    setup();
    testCryptoLarge();

    // Metadata tests
    std::cout << std::endl;
    std::cout << "--- Metadata ---" << std::endl;

    setup();
    testMetadataRoundTrip();

    setup();
    testMetadataTimestamp();

    setup();
    testBackupFilesMetadata();

    std::cout << std::endl;
    std::cout << "=== Results: " << passed << " passed, " << failed << " failed ===" << std::endl;

    return failed > 0 ? 1 : 0;
}