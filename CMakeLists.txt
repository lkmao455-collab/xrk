cmake_minimum_required(VERSION 3.20)
project(XRK VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Universal binary option (macOS only)
option(XRK_BUILD_UNIVERSAL "Build universal binary (arm64 + x86_64) on macOS" OFF)

# macOS architecture detection and configuration
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # Detect architecture
    if(CMAKE_OSX_ARCHITECTURES)
        message(STATUS "macOS architectures specified: ${CMAKE_OSX_ARCHITECTURES}")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures" FORCE)
        message(STATUS "Detected Apple Silicon (arm64), building for arm64")
    else()
        set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Target architectures" FORCE)
        message(STATUS "Detected Intel (x86_64), building for x86_64")
    endif()

    # Minimum macOS version
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
    message(STATUS "macOS deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")

    # Universal binary support (both arm64 and x86_64)
    if(XRK_BUILD_UNIVERSAL)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures" FORCE)
        message(STATUS "Building universal binary (arm64 + x86_64)")
    endif()

    # RPATH for Qt frameworks
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_INSTALL_RPATH "@loader_path/../Frameworks")
    set(BUILD_WITH_INSTALL_RPATH ON)
endif()

# Optimize for faster builds
if(MSVC)
    # Enable multi-processor compilation (/MP) and UTF-8 source charset
    add_compile_options(/permissive- /Zc:__cplusplus /utf-8 /MP)
elseif(APPLE)
    # macOS specific compiler flags
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Hide symbols by default
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
elseif(UNIX)
    # Linux/Unix compiler flags
    add_compile_options(-Wall -Wextra -Wpedantic -fPIC)
endif()

# macOS specific Qt6 find paths
if(APPLE)
    # Common Qt6 installation paths on macOS
    list(APPEND CMAKE_PREFIX_PATH
        "/opt/homebrew/opt/qt6"      # Apple Silicon Homebrew
        "/usr/local/opt/qt6"         # Intel Homebrew
        "/Applications/Qt/6.*/macos" # Qt Online Installer
        "$ENV{HOME}/Qt/6.*/macos"    # User Qt Online Installer
    )
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Core Gui Multimedia MultimediaWidgets Sql Test WebSockets)

# macOS app bundle configuration
if(APPLE)
    set(MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/Info.plist")
    set(MACOSX_BUNDLE_ICON_FILE "xrk.icns")
    set(MACOSX_BUNDLE_BUNDLE_NAME "XRK")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2024")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.xrk.app")
    set(MACOSX_BUNDLE_LONG_VERSION_STRING "XRK ${PROJECT_VERSION}")
endif()

enable_testing()
set(XRK_GTEST_AVAILABLE FALSE)
find_package(GTest QUIET)
if(GTest_FOUND)
    set(XRK_GTEST_AVAILABLE TRUE)
    message(STATUS "GTest found via find_package")
else()
    # Check if source was already fetched
    if(EXISTS "${CMAKE_BINARY_DIR}/_deps/googletest-src/CMakeLists.txt")
        message(STATUS "GTest source already fetched, reusing cached version")
        add_subdirectory("${CMAKE_BINARY_DIR}/_deps/googletest-src"
                         "${CMAKE_BINARY_DIR}/_deps/googletest-build")
        set(XRK_GTEST_AVAILABLE TRUE)
    else()
        message(STATUS "GTest not found; tests will be skipped (fetching from GitHub timed out previously)")
    endif()
endif()

# Translation files
set(TRANSLATION_DIR "${CMAKE_SOURCE_DIR}/translations")
file(GLOB TS_FILES "${TRANSLATION_DIR}/xrk_*.ts")

# Create custom target for translations
find_program(LRELEASE_EXECUTABLE lrelease HINTS "${Qt6_DIR}/../../../bin")
if(LRELEASE_EXECUTABLE AND TS_FILES)
    foreach(TS_FILE ${TS_FILES})
        get_filename_component(TS_NAME ${TS_FILE} NAME)
        string(REPLACE ".ts" ".qm" QM_NAME ${TS_NAME})
        set(QM_FILE "${CMAKE_BINARY_DIR}/translations/${QM_NAME}")

        add_custom_command(
            OUTPUT ${QM_FILE}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/translations"
            COMMAND ${LRELEASE_EXECUTABLE} ${TS_FILE} -qm ${QM_FILE}
            DEPENDS ${TS_FILE}
            COMMENT "Compiling translation: ${TS_NAME}"
        )
        list(APPEND QM_FILES ${QM_FILE})
    endforeach()

    add_custom_target(translations ALL DEPENDS ${QM_FILES})
    message(STATUS "Translation support enabled, ${TS_FILES} files found")
else()
    message(STATUS "Translation support disabled (lrelease not found or no .ts files)")
endif()

add_subdirectory(src)
add_subdirectory(tools)
if(XRK_GTEST_AVAILABLE)
    add_subdirectory(tests)
else()
    message(STATUS "Skipping tests (GTest not available)")
endif()
