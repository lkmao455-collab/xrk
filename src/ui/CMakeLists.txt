set(UI_SOURCES
    main_window.cpp
    simple_home_widget.cpp
    device_list_widget.cpp
    remote_desktop_widget.cpp
    file_transfer_widget.cpp
    settings_widget.cpp
    terminal_widget.cpp
    chat_widget.cpp
    system_info_widget.cpp
    address_book_dialog.cpp
    clipboard_history_widget.cpp
    media_test_dialog.cpp
    ipmsg_widget.cpp
    group_statistics_widget.cpp
    group_member_management_widget.cpp
    chat_search_widget.cpp
)

set(UI_HEADERS
    main_window.h
    simple_home_widget.h
    device_list_widget.h
    remote_desktop_widget.h
    file_transfer_widget.h
    settings_widget.h
    terminal_widget.h
    chat_widget.h
    system_info_widget.h
    address_book_dialog.h
    clipboard_history_widget.h
    media_test_dialog.h
    ipmsg_widget.h
    group_statistics_widget.h
    group_member_management_widget.h
    chat_search_widget.h
)

add_library(xrk_ui STATIC ${UI_SOURCES} ${UI_HEADERS})

target_include_directories(xrk_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(xrk_ui PUBLIC xrk_app Qt6::Widgets Qt6::MultimediaWidgets)
