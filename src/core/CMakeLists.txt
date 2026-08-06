set(CORE_SOURCES
    network_manager.cpp
    protocol_manager.cpp
    device_discovery.cpp
    tcp_connection.cpp
    message_codec.cpp
    logger.cpp
    encryption.cpp
    wake_on_lan.cpp
    arp_resolver.cpp
    audit_logger.cpp
    translation_manager.cpp
    theme_manager.cpp
)

set(CORE_HEADERS
    network_manager.h
    protocol_manager.h
    device_discovery.h
    tcp_connection.h
    message_codec.h
    logger.h
    types.h
    frame_queue.h
    encryption.h
    wake_on_lan.h
    arp_resolver.h
    audit_logger.h
    translation_manager.h
    theme_manager.h
)

add_library(xrk_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})

target_include_directories(xrk_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)

set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/ffmpeg-n7.1-latest-win64-gpl-shared-7.1")
if(EXISTS "${FFMPEG_ROOT}/include")
    target_include_directories(xrk_core PUBLIC "${FFMPEG_ROOT}/include")
endif()

target_link_libraries(xrk_core PUBLIC Qt6::Core Qt6::Network Qt6::Gui)

if(WIN32)
    # ArpResolver uses SendARP() (iphlpapi) and InetPtonA() (ws2_32).
    target_link_libraries(xrk_core PUBLIC iphlpapi.lib ws2_32.lib)
endif()

find_library(AVCODEC_LIB avcodec HINTS "${FFMPEG_ROOT}/lib")
find_library(AVUTIL_LIB avutil HINTS "${FFMPEG_ROOT}/lib")
find_library(SWSCALE_LIB swscale HINTS "${FFMPEG_ROOT}/lib")

if(AVCODEC_LIB AND AVUTIL_LIB AND SWSCALE_LIB)
    set(XRK_FFMPEG_AVAILABLE TRUE)
    target_link_libraries(xrk_core PUBLIC ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWSCALE_LIB})
    target_compile_definitions(xrk_core PUBLIC XRK_FFMPEG_AVAILABLE)
else()
    set(XRK_FFMPEG_AVAILABLE FALSE)
    message(WARNING "FFmpeg not found, H264/AES support disabled")
endif()

# Also try to find FFmpeg via pkg-config on non-Windows
if(NOT WIN32 AND NOT XRK_FFMPEG_AVAILABLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(AVCODEC QUIET libavcodec)
        pkg_check_modules(AVUTIL QUIET libavutil)
        pkg_check_modules(SWSCALE QUIET libswscale)
        if(AVCODEC_FOUND AND AVUTIL_FOUND AND SWSCALE_FOUND)
            set(XRK_FFMPEG_AVAILABLE TRUE)
            target_include_directories(xrk_core PUBLIC ${AVCODEC_INCLUDE_DIRS} ${AVUTIL_INCLUDE_DIRS} ${SWSCALE_INCLUDE_DIRS})
            target_link_libraries(xrk_core PUBLIC ${AVCODEC_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES})
            target_compile_definitions(xrk_core PUBLIC XRK_FFMPEG_AVAILABLE)
        endif()
    endif()
endif()

# Propagate to parent scope for HW module
set(XRK_FFMPEG_AVAILABLE ${XRK_FFMPEG_AVAILABLE} PARENT_SCOPE)
