cmake_minimum_required(VERSION 3.10)
project(datasoftware VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_AUTOMOC ON)

# MSVC UTF-8 support
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Find Qt5
find_package(Qt5 REQUIRED COMPONENTS Widgets Core)

# Core library sources
set(SOURCES
    src/FileEntry.cpp
    src/FileTraverser.cpp
    src/ArchiveWriter.cpp
    src/ArchiveReader.cpp
    src/BackupEngine.cpp
)

set(HEADERS
    include/datasoftware/FileEntry.h
    include/datasoftware/FileTraverser.h
    include/datasoftware/ArchiveWriter.h
    include/datasoftware/ArchiveReader.h
    include/datasoftware/BackupEngine.h
)

# Core static library
add_library(datasoftware STATIC ${SOURCES} ${HEADERS})
target_include_directories(datasoftware PUBLIC include)
target_link_libraries(datasoftware PUBLIC Qt5::Core)

# CLI executable
add_executable(dsbackup src/main.cpp)
target_link_libraries(dsbackup PRIVATE datasoftware)

# GUI executable
add_executable(dsbackup_gui
    src/main_gui.cpp
    src/gui/MainWindow.h
    src/gui/MainWindow.cpp
)
target_link_libraries(dsbackup_gui PRIVATE datasoftware Qt5::Widgets)

# Test executable
add_executable(dsbackup_test test/BackupEngineTest.cpp)
target_link_libraries(dsbackup_test PRIVATE datasoftware)