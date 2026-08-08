# The Host-teardown test spins up a real Host (TCP server, screen capture,
# media encoder, camera) and takes ~1.5s. It needs a working Qt multimedia /
# camera stack, so it can be excluded for fast or portable builds.
option(XRK_BUILD_HOST_TEARDOWN_TEST "Build the Host-start/stop/destroy teardown test" ON)

if(XRK_GTEST_AVAILABLE)
    set(XRK_TEST_SOURCES
        test_main.cpp
        test_device_discovery.cpp
        test_protocol.cpp
        test_network.cpp
        test_logger.cpp
        test_session_manager.cpp
        test_device_manager.cpp
        test_file_transfer_manager.cpp
        test_file_transfer_drag.cpp
        test_clipboard.cpp
        test_file_sync.cpp
        test_address_book.cpp
        test_security_manager.cpp
        test_p2p_relay.cpp
        test_loopback.cpp
        test_camera_capture.cpp
        test_video_roundtrip.cpp
        test_controller_screen.cpp
        test_host_authkey.cpp
        test_host_encoder_default.cpp
        test_encryption.cpp
        test_e2e_black_screen.cpp
        test_svg_icons.cpp
        test_device_registry.cpp
        test_camera_basic.cpp
        test_microphone_basic.cpp
        test_database_manager.cpp
        test_ipmsg_crypto.cpp
        test_ipmsg_transfer.cpp
        test_wake_on_lan.cpp
        test_clipboard_history.cpp
        test_audit_logger.cpp
        test_jpeg_encoder.cpp
        test_frame_queue.cpp
        test_system_info.cpp
        test_theme_manager.cpp
        test_screen_recorder.cpp
        test_arp_resolver.cpp
        test_translation_manager.cpp
        test_message_codec_edge.cpp
        test_remote_terminal.cpp
        test_web_socket_gateway.cpp
        test_voice_messages.cpp
        test_input_control.cpp
    )

    if(XRK_BUILD_HOST_TEARDOWN_TEST)
        list(APPEND XRK_TEST_SOURCES test_host_teardown.cpp test_host_run.cpp)
    else()
        message(STATUS "Host teardown test disabled (XRK_BUILD_HOST_TEARDOWN_TEST=OFF)")
    endif()

    add_executable(xrk_tests ${XRK_TEST_SOURCES} "${CMAKE_SOURCE_DIR}/resources/xrk.qrc")
    
    target_link_libraries(xrk_tests PRIVATE 
        xrk_ui
        xrk_core 
        xrk_app 
        Qt6::Test
        GTest::gtest
    )
    
    include(GoogleTest)
    # PRE_TEST defers test discovery to ctest time. The test executable links
    # Qt and needs the Qt runtime DLLs on PATH; running discovery at build time
    # (POST_BUILD) fails with STATUS_DLL_NOT_FOUND because the build environment
    # does not have Qt on PATH. Run ctest from a shell where Qt's bin dir is on
    # PATH (or run build/tests/Release/xrk_tests.exe directly).
    gtest_discover_tests(xrk_tests DISCOVERY_MODE PRE_TEST)
else()
    message(STATUS "GTest not available, tests disabled")
endif()
