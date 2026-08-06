set(HW_SOURCES
    screen_capture.cpp
    input_control.cpp
    video_encoder.cpp
    jpeg_encoder.cpp
    h264_encoder.cpp
    video_decoder.cpp
    camera_capture.cpp
    audio_capture.cpp
    audio_player.cpp
)

set(HW_HEADERS
    screen_capture.h
    input_control.h
    video_encoder.h
    jpeg_encoder.h
    h264_encoder.h
    video_decoder.h
    camera_capture.h
    camera_control.h
    audio_capture.h
    audio_player.h
)

add_library(xrk_hw STATIC ${HW_SOURCES} ${HW_HEADERS})

target_include_directories(xrk_hw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(xrk_hw PUBLIC xrk_core Qt6::Gui Qt6::Multimedia)

if(WIN32)
    target_link_libraries(xrk_hw PUBLIC d3d11 dxgi)
endif()

if(UNIX AND NOT APPLE)
    target_compile_definitions(xrk_hw PUBLIC _LINUX)
    find_package(X11 REQUIRED)
    target_include_directories(xrk_hw PUBLIC ${X11_INCLUDE_DIR})
    target_link_libraries(xrk_hw PUBLIC X11 Xext Xtst)
endif()

if(APPLE)
    target_link_libraries(xrk_hw PUBLIC "-framework ApplicationServices" "-framework CoreGraphics")
endif()
