cmake_minimum_required(VERSION 3.20)
project(XRK VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Core Gui Multimedia)

enable_testing()
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "GTest not found via find_package; fetching from source via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    # For Windows: prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    set(GTest_FOUND TRUE)
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
add_subdirectory(tests)
