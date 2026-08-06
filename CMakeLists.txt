cmake_minimum_required(VERSION 3.20)
project(XRK VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Optimize for faster builds
if(MSVC)
    # Enable multi-processor compilation (/MP) and UTF-8 source charset
    add_compile_options(/permissive- /Zc:__cplusplus /utf-8 /MP)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Core Gui Multimedia MultimediaWidgets Sql Test WebSockets)

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
