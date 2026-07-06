#include <iostream>
#include <string>
#include <chrono>
#include "datasoftware/BackupEngine.h"

void printUsage(const char* progName) {
    std::cerr << "Usage:" << std::endl;
    std::cerr << "  " << progName << " backup   <source-dir> <archive-file>" << std::endl;
    std::cerr << "  " << progName << " restore  <archive-file> <restore-dir>" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Examples:" << std::endl;
    std::cerr << "  " << progName << " backup   ./mydata    ./backup.dat" << std::endl;
    std::cerr << "  " << progName << " restore  ./backup.dat ./restored" << std::endl;
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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    try {
        if (command == "backup" || command == "b") {
            if (argc != 4) {
                printUsage(argv[0]);
                return 1;
            }
            std::string sourceDir = argv[2];
            std::string archivePath = argv[3];

            auto start = std::chrono::steady_clock::now();

            std::cout << "Backing up: " << sourceDir << " -> " << archivePath << std::endl;
            size_t count = datasoftware::BackupEngine::backup(sourceDir, archivePath);

            auto end = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(end - start).count();

            std::cout << "Backup complete! " << count << " files ("
                      << formatTime(elapsed) << ")." << std::endl;

        } else if (command == "restore" || command == "r") {
            if (argc != 4) {
                printUsage(argv[0]);
                return 1;
            }
            std::string archivePath = argv[2];
            std::string restoreDir = argv[3];

            auto start = std::chrono::steady_clock::now();

            std::cout << "Restoring: " << archivePath << " -> " << restoreDir << std::endl;
            size_t count = datasoftware::BackupEngine::restore(archivePath, restoreDir);

            auto end = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(end - start).count();

            std::cout << "Restore complete! " << count << " files ("
                      << formatTime(elapsed) << ")." << std::endl;

        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}