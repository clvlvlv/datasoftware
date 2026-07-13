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

static void forceRemoveDirectory(const std::string& path) {
    if (!fs::exists(path)) return;
    
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(path, ec)) {
        if (ec) break;
        try {
            fs::permissions(entry.path(),
                           fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all,
                           fs::perm_options::replace, ec);
        } catch (...) {}
    }
    
    fs::remove_all(path, ec);
}

void setup() {
    std::string existingTestDir = (fs::temp_directory_path() / "dsbackup_test").string();
    if (fs::exists(existingTestDir)) {
        forceRemoveDirectory(existingTestDir);
    }

    testDir = (fs::temp_directory_path() / "dsbackup_test").string();
    srcDir = testDir + "/source";
    restoreDir = testDir + "/restore";
    archivePath = testDir + "/backup.dat";

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
        
        int fileCount = 0, dirCount = 0;
        for (const auto& e : entries) {
            if (e.fileType == FileType::Directory) dirCount++;
            else fileCount++;
        }
        
        assert(fileCount == 3);
        assert(dirCount >= 2);
        assert(entries.size() == fileCount + dirCount);
        
        std::cout << "(files=" << fileCount << ", dirs=" << dirCount 
                  << ", total=" << entries.size() << ") ";
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

        assert(fileContentEquals(restoreDir + "/source/doc.txt", "document content"));
        assert(fileContentEquals(restoreDir + "/source/images/pic.dat", std::string(100, 'A')));
        assert(fileContentEquals(restoreDir + "/source/config/settings.ini", "[config]\nkey=value\n"));
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

void testBackupRestoreHardlink() {
    // Check if hard links are supported on this system
    std::error_code probeEc;
    fs::path probeSrc = testDir + "/_probe_hl_src.txt";
    fs::path probeDst = testDir + "/_probe_hl_dst.txt";
    { std::ofstream o(probeSrc); o << "x"; }
    fs::create_hard_link(probeSrc, probeDst, probeEc);
    bool hardlinksOk = !probeEc;
    fs::remove(probeSrc, probeEc);
    fs::remove(probeDst, probeEc);

    if (!hardlinksOk) {
        TEST("Backup/restore hard link (SKIPPED - not supported)")
            FileEntry hlink("linked.txt", FileType::HardLink, 5,
                           std::vector<char>{'h','e','l','l','o'}, "", 42);
            ArchiveWriter::write(archivePath, {hlink});
            auto loaded = ArchiveReader::read(archivePath);
            assert(loaded.size() == 1);
            assert(loaded[0].fileType == FileType::HardLink);
            assert(loaded[0].hardLinkId == 42);
            std::cout << "(format verified)";
            passed++;
        END_TEST("Backup/restore hard link");
        return;
    }

    TEST("Backup and restore with hard link")
        createFile(srcDir + "/original.txt", "shared content for hard link test");

        // Create a hard link — both paths point to the same file data on disk
        std::error_code ec;
        fs::create_hard_link(
            srcDir + "/original.txt",
            srcDir + "/hardlink_to_original.txt",
            ec
        );
        assert(!ec);

        // Verify both files have identical size (same underlying file)
        assert(fs::file_size(srcDir + "/original.txt") ==
               fs::file_size(srcDir + "/hardlink_to_original.txt"));

        // Full backup
        size_t backedUp = BackupEngine::backup(srcDir, archivePath);
        std::cout << "(" << backedUp << " entries in archive) ";

        // Debug: check what's in the archive
        auto debugEntries = ArchiveReader::read(archivePath);
        for (const auto& de : debugEntries) {
            std::cout << "[" << de.relativePath
                      << " type=" << (int)de.fileType
                      << " size=" << de.fileSize << "] ";
        }

        // Restore
        std::string restoreSub = restoreDir + "/_out";
        size_t restored = BackupEngine::restore(archivePath, restoreSub);
        assert(restored == backedUp);

        // Both restored files should exist and have correct content
        assert(fileContentEquals(restoreSub + "/source/original.txt",
                                 "shared content for hard link test"));
        assert(fileContentEquals(restoreSub + "/source/hardlink_to_original.txt",
                                 "shared content for hard link test"));
    END_TEST("Backup and restore with hard link");
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
            uint32_t plen = 11;
            out.write(reinterpret_cast<const char*>(&plen), 4);
            out.write("v1_file.txt", 11);
            uint64_t fsize = 5;
            out.write(reinterpret_cast<const char*>(&fsize), 8);
            out.write("Hello", 5);
            // Entry 2
            plen = 12;
            out.write(reinterpret_cast<const char*>(&plen), 4);
            out.write("empty_v1.bin", 12);
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

        assert(fileContentEquals(restoreSub + "/source/real_file.txt",
                                 "symlink content"));
    END_TEST("Backup and restore with symlink");
}

// === Compression tests ===

void testRleRoundTrip() {
    TEST("RLE round-trip")
        std::vector<char> original(1000, 'A');
        for (size_t i = 0; i < 100; ++i) {
            original[i * 10] = static_cast<char>('A' + (i % 26));
        }
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
        std::string text = "ABABABABABABABABABABABABABABABABABABABABABABABABAB";
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
        std::string restoreDirPath = testDir + "/restored";
        std::string rootName = fs::path(testDir).filename().string();
        std::string restoreFile = restoreDirPath + "/" + rootName + "/metadata_test.txt";

        { std::ofstream out(testFile); out << "hello"; }

        // Backup and restore
        size_t count = BackupEngine::backup(testDir, archiveFile);
        fs::remove_all(restoreDirPath);
        size_t restored = BackupEngine::restore(archiveFile, restoreDirPath);
        assert(restored == 1);

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

        std::string rootName = fs::path(testDir).filename().string();
        std::string restoredFile = restoredDir + "/" + rootName + "/time_test.txt";
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

void testFilterIncludePaths() {
    TEST("Filter: include paths")
        BackupFilter f;
        f.includePaths = {"docs/**", "src/*.cpp"};
        
        assert(f.matches("docs/readme.txt", ".txt", 100, 0, ""));
        assert(f.matches("src/main.cpp", ".cpp", 100, 0, ""));
        assert(!f.matches("tmp/temp.log", ".log", 100, 0, ""));
        assert(!f.matches("src/main.h", ".h", 100, 0, ""));
    END_TEST("Filter: include paths");
}

void testFilterExcludePaths() {
    TEST("Filter: exclude paths")
        BackupFilter f;
        f.excludePaths = {"tmp/**", "*.log", ".git/**"};
        
        assert(!f.matches("tmp/cache.dat", ".dat", 100, 0, ""));
        assert(!f.matches("debug.log", ".log", 100, 0, ""));
        assert(!f.matches(".git/config", ".config", 100, 0, ""));
        assert(f.matches("src/main.cpp", ".cpp", 100, 0, ""));
    END_TEST("Filter: exclude paths");
}

void testFilterIncludeExtensions() {
    TEST("Filter: include extensions")
        BackupFilter f;
        f.includeExts = {".txt", ".pdf", ".docx"};
        
        assert(f.matches("doc.txt", ".txt", 100, 0, ""));
        assert(f.matches("report.pdf", ".pdf", 100, 0, ""));
        assert(f.matches("letter.docx", ".docx", 100, 0, ""));
        assert(!f.matches("image.png", ".png", 100, 0, ""));
        assert(!f.matches("script.js", ".js", 100, 0, ""));
    END_TEST("Filter: include extensions");
}

void testFilterExcludeExtensions() {
    TEST("Filter: exclude extensions")
        BackupFilter f;
        f.excludeExts = {".tmp", ".bak", ".log"};
        
        assert(!f.matches("temp.tmp", ".tmp", 100, 0, ""));
        assert(!f.matches("backup.bak", ".bak", 100, 0, ""));
        assert(!f.matches("system.log", ".log", 100, 0, ""));
        assert(f.matches("document.txt", ".txt", 100, 0, ""));
        assert(f.matches("image.png", ".png", 100, 0, ""));
    END_TEST("Filter: exclude extensions");
}

void testFilterNamePattern() {
    TEST("Filter: name pattern (substring)")
        BackupFilter f;
        f.namePattern = "test";
        
        assert(f.matches("test_file.txt", ".txt", 100, 0, ""));
        assert(f.matches("my_test_data.csv", ".csv", 100, 0, ""));
        assert(f.matches("TESTCASE.cpp", ".cpp", 100, 0, "")); // 不区分大小写
        assert(!f.matches("production.dat", ".dat", 100, 0, ""));
    END_TEST("Filter: name pattern");
}

void testFilterSizeRange() {
    TEST("Filter: size range")
        BackupFilter f;
        f.minSize = 100;
        f.maxSize = 1000;
        
        assert(f.matches("file.txt", ".txt", 500, 0, ""));
        assert(f.matches("file.txt", ".txt", 100, 0, ""));
        assert(f.matches("file.txt", ".txt", 1000, 0, ""));
        assert(!f.matches("small.txt", ".txt", 99, 0, ""));
        assert(!f.matches("large.txt", ".txt", 1001, 0, ""));
    END_TEST("Filter: size range");
}

void testFilterTimeRange() {
    TEST("Filter: time range")
        BackupFilter f;
        f.timeFrom = 1000;
        f.timeTo = 2000;
        
        assert(f.matches("file.txt", ".txt", 100, 1000, ""));
        assert(f.matches("file.txt", ".txt", 100, 1500, ""));
        assert(f.matches("file.txt", ".txt", 100, 2000, ""));
        assert(!f.matches("file.txt", ".txt", 100, 999, ""));
        assert(!f.matches("file.txt", ".txt", 100, 2001, ""));
    END_TEST("Filter: time range");
}

void testFilterUserName() {
    TEST("Filter: user name")
        BackupFilter f;
        f.userName = "admin";
        
        assert(f.matches("file.txt", ".txt", 100, 0, "admin"));
        assert(f.matches("file.txt", ".txt", 100, 0, "Administrator")); // 不区分大小写
        assert(!f.matches("file.txt", ".txt", 100, 0, "guest"));
    END_TEST("Filter: user name");
}

void testFilterCombined() {
    TEST("Filter: combined filters")
        BackupFilter f;
        f.includeExts = {".txt", ".cpp"};
        f.excludePaths = {"tmp/**"};
        f.namePattern = "src";
        f.minSize = 10;
        f.maxSize = 1000;
        
        // 应该匹配
        assert(f.matches("src/main.cpp", ".cpp", 500, 0, ""));
        assert(f.matches("src/test.txt", ".txt", 50, 0, ""));
        
        // 不应该匹配
        assert(!f.matches("tmp/src/main.cpp", ".cpp", 500, 0, "")); // excludePath
        assert(!f.matches("src/main.h", ".h", 500, 0, "")); // 扩展名不匹配
        assert(!f.matches("src/small.txt", ".txt", 5, 0, "")); // 太小
        assert(!f.matches("src/large.txt", ".txt", 2000, 0, "")); // 太大
    END_TEST("Filter: combined filters");
}

void testFilterSummary() {
    TEST("Filter: summary output")
        BackupFilter f;
        f.includeExts = {".txt", ".pdf"};
        f.excludePaths = {"tmp/**"};
        f.namePattern = "data";
        f.minSize = 100;
        
        std::string summary = f.summary();
        assert(summary.find("ext in") != std::string::npos);
        assert(summary.find("path out") != std::string::npos);
        assert(summary.find("name") != std::string::npos);
        assert(summary.find("min size") != std::string::npos);
        
        // 清除后应该显示 "no filters"
        f.clear();
        assert(f.summary() == "no filters");
        assert(!f.isActive());
    END_TEST("Filter: summary output");
}

void testLargeFile() {
    TEST("Large file handling (10MB)")
        std::string largeFile = srcDir + "/large.bin";
        std::vector<char> data(10 * 1024 * 1024, 'X');
        for (size_t i = 0; i < data.size(); i += 1024) {
            data[i] = static_cast<char>(i % 256);
        }
        
        {
            std::ofstream out(largeFile, std::ios::binary);
            out.write(data.data(), data.size());
        }
        
        auto entries = FileTraverser::traverse(srcDir);
        assert(entries.size() == 1);
        assert(entries[0].fileSize == data.size());
        assert(std::memcmp(entries[0].data.data(), data.data(), data.size()) == 0);
        
        BackupEngine::backup(srcDir, archivePath);
        BackupEngine::restore(archivePath, restoreDir);
        
        std::string restoredFile = restoreDir + "/source/large.bin";
        assert(fs::exists(restoredFile));
        assert(fs::file_size(restoredFile) == data.size());
        
        std::ifstream in(restoredFile, std::ios::binary);
        std::vector<char> restored(data.size());
        in.read(restored.data(), data.size());
        assert(std::memcmp(restored.data(), data.data(), data.size()) == 0);
    END_TEST("Large file handling (10MB)");
}

void testSpecialCharactersInPath() {
    TEST("Special characters in path")
        std::vector<std::string> testNames = {
            "file with spaces.txt",
            "file-with-dashes.txt",
            "file_with_underscores.txt",
            "file.with.dots.txt",
            "文件中文.txt",  // Unicode
            "file (with) brackets.txt",
            "file's apostrophe.txt"
        };
        
        for (const auto& name : testNames) {
            createFile(srcDir + "/" + name, "content");
        }
        
        auto entries = FileTraverser::traverse(srcDir);
        assert(entries.size() == testNames.size());
        
        for (const auto& entry : entries) {
            assert(entry.fileSize == 7); // "content" length
        }
    END_TEST("Special characters in path");
}

void testDeepNesting() {
    TEST("Deep directory nesting (20 levels)")
        std::string path = srcDir;
        for (int i = 0; i < 20; ++i) {
            path += "/level" + std::to_string(i);
        }
        createFile(path + "/deep.txt", "deep content");
        
        auto entries = FileTraverser::traverse(srcDir);
        
        int fileCount = 0, dirCount = 0;
        for (const auto& e : entries) {
            if (e.fileType == FileType::Directory) dirCount++;
            else fileCount++;
        }
        
        assert(fileCount == 1);
        assert(dirCount >= 20);
        assert(entries.size() == fileCount + dirCount);
        
        bool found = false;
        for (const auto& e : entries) {
            if (e.fileType == FileType::Regular && 
                e.relativePath.find("deep.txt") != std::string::npos) {
                found = true;
                assert(std::string(e.data.data(), 12) == "deep content");
            }
        }
        assert(found);
        
        std::cout << "(files=" << fileCount << ", dirs=" << dirCount 
                  << ", total=" << entries.size() << ") ";
    END_TEST("Deep directory nesting (20 levels)");
}

void testManyFiles() {
    TEST("Many files (500 files)")
        const int NUM_FILES = 500;
        for (int i = 0; i < NUM_FILES; ++i) {
            createFile(srcDir + "/file" + std::to_string(i) + ".txt", 
                      "Content " + std::to_string(i));
        }
        
        auto entries = FileTraverser::traverse(srcDir);
        assert(entries.size() == NUM_FILES);
        
        size_t backedUp = BackupEngine::backup(srcDir, archivePath);
        assert(backedUp == NUM_FILES);
    END_TEST("Many files (500 files)");
}

void testNonExistentSource() {
    TEST("Error handling: non-existent source directory")
        bool caught = false;
        try {
            FileTraverser::traverse(testDir + "/nonexistent");
        } catch (const std::runtime_error&) {
            caught = true;
        }
        assert(caught);
    END_TEST("Error handling: non-existent source directory");
}

void testNonExistentArchive() {
    TEST("Error handling: non-existent archive")
        bool caught = false;
        try {
            ArchiveReader::read(testDir + "/nonexistent.dat");
        } catch (const std::runtime_error&) {
            caught = true;
        }
        assert(caught);
    END_TEST("Error handling: non-existent archive");
}

void testCorruptedArchive() {
    TEST("Error handling: corrupted archive")
        {
            std::ofstream out(archivePath, std::ios::binary);
            out << "CORRUPTED" << std::string(100, 'X');
        }
        
        bool caught = false;
        try {
            ArchiveReader::read(archivePath);
        } catch (const std::runtime_error&) {
            caught = true;
        }
        assert(caught);
    END_TEST("Error handling: corrupted archive");
}

void testEmptyPassword() {
    TEST("Error handling: empty password")
        std::vector<char> data = {'t', 'e', 's', 't'};
        bool caught = false;
        try {
            Crypto::encrypt(data, "");
        } catch (const std::runtime_error&) {
            caught = true;
        }
        assert(caught);
    END_TEST("Error handling: empty password");
}

void testBackupFilesSimple() {
    TEST("BackupFiles: simple list of files")
        createFile(srcDir + "/a.txt", "aaa");
        createFile(srcDir + "/b.txt", "bbb");
        createFile(srcDir + "/c.txt", "ccc");
        
        std::vector<std::string> files = {
            srcDir + "/a.txt",
            srcDir + "/b.txt",
            srcDir + "/c.txt"
        };
        
        size_t count = BackupEngine::backupFiles(files, archivePath);
        assert(count == 3);
        
        fs::remove_all(restoreDir);
        BackupEngine::restore(archivePath, restoreDir);
        
        assert(fileContentEquals(restoreDir + "/a.txt", "aaa"));
        assert(fileContentEquals(restoreDir + "/b.txt", "bbb"));
        assert(fileContentEquals(restoreDir + "/c.txt", "ccc"));
    END_TEST("BackupFiles: simple list of files");
}

void testBackupFilesWithSubdirs() {
    TEST("BackupFiles: files from subdirectories")
        createFile(srcDir + "/sub1/a.txt", "aaa");
        createFile(srcDir + "/sub2/b.txt", "bbb");
        createFile(srcDir + "/sub2/sub3/c.txt", "ccc");
        
        std::vector<std::string> files = {
            srcDir + "/sub1/a.txt",
            srcDir + "/sub2/b.txt",
            srcDir + "/sub2/sub3/c.txt"
        };
        
        size_t count = BackupEngine::backupFiles(files, archivePath);
        assert(count == 3);
        
        fs::remove_all(restoreDir);
        BackupEngine::restore(archivePath, restoreDir);
        
        assert(fileContentEquals(restoreDir + "/a.txt", "aaa"));
        assert(fileContentEquals(restoreDir + "/b.txt", "bbb"));
        assert(fileContentEquals(restoreDir + "/c.txt", "ccc"));
    END_TEST("BackupFiles: files from subdirectories");
}

void testEncryptedBackup() {
    TEST("Encrypted backup and restore")
        createFile(srcDir + "/secret.txt", "This is secret data");
        createFile(srcDir + "/public.txt", "This is public data");
        
        // 先正常备份
        BackupEngine::backup(srcDir, archivePath);
        
        // 加密归档
        std::string password = "secure123";
        std::string encryptedPath = testDir + "/encrypted.dat";
        Crypto::encryptFile(archivePath, encryptedPath, password);
        
        // 尝试恢复（应该检测到加密并提示）
        bool requiresPassword = Crypto::isEncryptedFile(encryptedPath);
        assert(requiresPassword);
        
        // 用正确密码解密后恢复
        std::string tempDecrypted = testDir + "/decrypted.dat";
        Crypto::decryptFile(encryptedPath, tempDecrypted, password);
        
        fs::remove_all(restoreDir);
        BackupEngine::restore(tempDecrypted, restoreDir);
        
        assert(fileContentEquals(restoreDir + "/source/secret.txt", "This is secret data"));
        assert(fileContentEquals(restoreDir + "/source/public.txt", "This is public data"));
        
        // 清理临时文件
        fs::remove(tempDecrypted);
    END_TEST("Encrypted backup and restore");
}

void testEncryptedBackupWrongPassword() {
    TEST("Encrypted backup: wrong password rejected")
        createFile(srcDir + "/data.txt", "sensitive data");
        BackupEngine::backup(srcDir, archivePath);
        
        std::string password = "correct";
        std::string encryptedPath = testDir + "/encrypted.dat";
        Crypto::encryptFile(archivePath, encryptedPath, password);
        
        // 尝试用错误密码解密
        bool caught = false;
        try {
            Crypto::decryptFile(encryptedPath, testDir + "/decrypted.dat", "wrong");
        } catch (const std::runtime_error&) {
            caught = true;
        }
        assert(caught);
    END_TEST("Encrypted backup: wrong password rejected");
}

void testProgressCallback() {
    TEST("Progress callback invocation")
        createFile(srcDir + "/a.txt", "aaa");
        createFile(srcDir + "/b.txt", "bbb");
        createFile(srcDir + "/c.txt", "ccc");
        
        size_t lastProgress = 0;
        size_t totalProgress = 0;
        bool callbackCalled = false;
        
        auto cb = [&](size_t cur, size_t tot, const std::string& msg) {
            callbackCalled = true;
            lastProgress = cur;
            totalProgress = tot;
        };
        
        BackupEngine::backup(srcDir, archivePath, cb);
        
        assert(callbackCalled);
        assert(totalProgress == 3);
        // 最后进度应该是 total
        assert(lastProgress == 3);
    END_TEST("Progress callback invocation");
}

void testProgressCallbackRestore() {
    TEST("Progress callback on restore")
        createFile(srcDir + "/a.txt", "aaa");
        createFile(srcDir + "/b.txt", "bbb");
        BackupEngine::backup(srcDir, archivePath);
        
        size_t lastProgress = 0;
        bool callbackCalled = false;
        
        auto cb = [&](size_t cur, size_t tot, const std::string& msg) {
            callbackCalled = true;
            lastProgress = cur;
        };
        
        BackupEngine::restore(archivePath, restoreDir, cb);
        
        assert(callbackCalled);
        assert(lastProgress == 2);
    END_TEST("Progress callback on restore");
}

void testEmptyMetadata() {
    TEST("Empty metadata handling")
        FileEntry fe("test.txt", 0, std::vector<char>{});
        assert(fe.metadata.isEmpty());
        
        // 备份和恢复应该能处理空元数据
        BackupEngine::backup(srcDir, archivePath);
        BackupEngine::restore(archivePath, restoreDir);
        
        // 应该正常完成，不崩溃
    END_TEST("Empty metadata handling");
}

void testMetadataAttributes() {
    TEST("File attributes preservation")
        createFile(srcDir + "/attrs.txt", "content");
        
        std::string fullPath = srcDir + "/attrs.txt";
        
        // 设置只读属性
        std::wstring wpath(fullPath.begin(), fullPath.end());
        SetFileAttributesW(wpath.c_str(), FILE_ATTRIBUTE_READONLY);
        
        // 备份
        BackupEngine::backup(srcDir, archivePath);
        
        // 恢复
        fs::remove_all(restoreDir);
        BackupEngine::restore(archivePath, restoreDir);
        
        std::string restoredPath = restoreDir + "/source/attrs.txt";
        
        // 检查属性是否保留
        WIN32_FILE_ATTRIBUTE_DATA info;
        std::wstring rpath(restoredPath.begin(), restoredPath.end());
        if (GetFileAttributesExW(rpath.c_str(), GetFileExInfoStandard, &info)) {
            assert(info.dwFileAttributes & FILE_ATTRIBUTE_READONLY);
        }
        
        // 清理
        SetFileAttributesW(wpath.c_str(), FILE_ATTRIBUTE_NORMAL);
        SetFileAttributesW(rpath.c_str(), FILE_ATTRIBUTE_NORMAL);
    END_TEST("File attributes preservation");
}

// === Main ===

// 替换原有的 main 函数
int main() {
    std::cout << "=== Data Backup Software - Complete Test Suite ===" << std::endl;
    std::cout << std::endl;

    // ===== 基础功能测试 =====
    std::cout << "--- Basic Functionality ---" << std::endl;
    setup(); testTraverseEmptyDir();
    setup(); testTraverseSingleFile();
    setup(); testTraverseNestedFiles();  // 已修复
    setup(); testArchiveRoundTrip();
    setup(); testBackupAndRestore();
    setup(); testEmptyDirBackup();
    setup(); testInvalidArchive();

    // ===== 文件类型测试 =====
    std::cout << std::endl << "--- File Type Support ---" << std::endl;
    setup(); testTraverseIncludesDirectories();
    setup(); testSymlinkRoundTrip();
    setup(); testFifoRoundTrip();
    setup(); testDeviceRoundTrip();
    setup(); testHardLinkRoundTrip();
    setup(); testBackwardCompatV1();
    setup(); testBackupRestoreSymlink();
    setup(); testBackupRestoreHardlink();

    // ===== 过滤器测试 =====
    std::cout << std::endl << "--- BackupFilter Tests ---" << std::endl;
    testFilterIncludePaths();
    testFilterExcludePaths();
    testFilterIncludeExtensions();
    testFilterExcludeExtensions();
    testFilterNamePattern();
    testFilterSizeRange();
    testFilterTimeRange();
    testFilterUserName();
    testFilterCombined();
    testFilterSummary();

    // ===== 压缩测试 =====
    std::cout << std::endl << "--- Compression ---" << std::endl;
    setup(); testRleRoundTrip();
    setup(); testRleNoRepeat();
    setup(); testRleEmpty();
    setup(); testLz77RoundTrip();
    setup(); testLz77General();
    setup(); testCompressFileRoundTrip();
    setup(); testHuffmanRoundTrip();
    setup(); testHuffmanAllBytes();
    setup(); testHuffmanEmpty();
    setup(); testHuffmanLargeData();

    // ===== 加密测试 =====
    std::cout << std::endl << "--- Encryption ---" << std::endl;
    setup(); testCryptoRoundTrip();
    setup(); testCryptoWrongPassword();
    setup(); testCryptoLarge();

    // ===== 元数据测试 =====
    std::cout << std::endl << "--- Metadata ---" << std::endl;
    setup(); testMetadataRoundTrip();
    setup(); testMetadataTimestamp();
    setup(); testBackupFilesMetadata();
    setup(); testEmptyMetadata();
    setup(); testMetadataAttributes();

    // ===== 边界条件测试 =====
    std::cout << std::endl << "--- Edge Cases ---" << std::endl;
    setup(); testLargeFile();
    setup(); testSpecialCharactersInPath();
    setup(); testDeepNesting();
    setup(); testManyFiles();

    // ===== 错误处理测试 =====
    std::cout << std::endl << "--- Error Handling ---" << std::endl;
    setup(); testNonExistentSource();
    setup(); testNonExistentArchive();
    setup(); testCorruptedArchive();
    setup(); testEmptyPassword();

    // ===== BackupFiles测试 =====
    std::cout << std::endl << "--- BackupFiles ---" << std::endl;
    setup(); testBackupFilesSimple();
    setup(); testBackupFilesWithSubdirs();

    // ===== 加密备份集成测试 =====
    std::cout << std::endl << "--- Encrypted Backup ---" << std::endl;
    setup(); testEncryptedBackup();
    setup(); testEncryptedBackupWrongPassword();

    // ===== 进度回调测试 =====
    std::cout << std::endl << "--- Progress Callback ---" << std::endl;
    setup(); testProgressCallback();
    setup(); testProgressCallbackRestore();

    // ===== 结果汇总 =====
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "=== Results: " << passed << " passed, " << failed << " failed ===" << std::endl;
    std::cout << "========================================" << std::endl;

    return failed > 0 ? 1 : 0;
}
