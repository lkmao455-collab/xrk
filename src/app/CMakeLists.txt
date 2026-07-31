set(APP_SOURCES
    device_manager.cpp
    session_manager.cpp
    remote_controller.cpp
    security_manager.cpp
    file_transfer_manager.cpp
    file_sync_manager.cpp
    host.cpp
    host_service.cpp
    clipboard_manager.cpp
    clipboard_history.cpp
    remote_terminal.cpp
    screen_recorder.cpp
    privacy_screen.cpp
    relay_server.cpp
    nat_traversal.cpp
    p2p_manager.cpp
    system_info_collector.cpp
    address_book.cpp
)

set(APP_HEADERS
    device_manager.h
    session_manager.h
    remote_controller.h
    security_manager.h
    file_transfer_manager.h
    file_sync_manager.h
    host.h
    host_service.h
    clipboard_manager.h
    clipboard_history.h
    remote_terminal.h
    screen_recorder.h
    privacy_screen.h
    relay_server.h
    nat_traversal.h
    p2p_manager.h
    system_info_collector.h
    address_book.h
)

add_library(xrk_app STATIC ${APP_SOURCES} ${APP_HEADERS})

target_include_directories(xrk_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(xrk_app PUBLIC xrk_core xrk_hw Qt6::Widgets)

if(WIN32)
    target_link_libraries(xrk_app PRIVATE pdh.lib advapi32.lib)
endif()
