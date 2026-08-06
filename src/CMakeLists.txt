add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(ui)
add_subdirectory(hw)

set(XRK_RESOURCES "${CMAKE_SOURCE_DIR}/resources/xrk.qrc")
if(WIN32)
    list(APPEND XRK_RESOURCES "${CMAKE_SOURCE_DIR}/resources/icons/xrk.rc")
endif()

if(APPLE)
    # macOS app bundle
    set(XRK_MACOS_BUNDLE_SOURCES
        "${CMAKE_SOURCE_DIR}/resources/Info.plist"
        "${CMAKE_SOURCE_DIR}/resources/icons/xrk.icns"
    )
    add_executable(xrk MACOSX_BUNDLE main.cpp ${XRK_RESOURCES} ${XRK_MACOS_BUNDLE_SOURCES})
    set_target_properties(xrk PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/Info.plist"
        MACOSX_BUNDLE_ICON_FILE "xrk.icns"
        MACOSX_BUNDLE_BUNDLE_NAME "XRK"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2024"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.xrk.app"
        MACOSX_BUNDLE_LONG_VERSION_STRING "XRK ${PROJECT_VERSION}"
        RESOURCE "${CMAKE_SOURCE_DIR}/resources/icons/xrk.icns"
    )
else()
    add_executable(xrk main.cpp ${XRK_RESOURCES})
endif()

target_link_libraries(xrk PRIVATE xrk_ui xrk_hw)

# Copy translation files to output directory
if(QM_FILES)
    add_dependencies(xrk translations)
    foreach(QM_FILE ${QM_FILES})
        get_filename_component(QM_NAME ${QM_FILE} NAME)
        if(APPLE)
            # On macOS, copy to app bundle Resources
            add_custom_command(TARGET xrk POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${QM_FILE}
                    $<TARGET_FILE_DIR:xrk>/../Resources/translations/${QM_NAME}
                COMMENT "Copying translation: ${QM_NAME}"
            )
        else()
            add_custom_command(TARGET xrk POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${QM_FILE}
                    $<TARGET_FILE_DIR:xrk>/translations/${QM_NAME}
                COMMENT "Copying translation: ${QM_NAME}"
            )
        endif()
    endforeach()
endif()

# macOS: Copy Qt frameworks into app bundle
if(APPLE)
    find_program(QT_MAC_DEPLOY_TOOL macdeployqt)
    if(QT_MAC_DEPLOY_TOOL)
        add_custom_command(TARGET xrk POST_BUILD
            COMMAND ${QT_MAC_DEPLOY_TOOL} $<TARGET_FILE_DIR:xrk>/xrk.app -verbose=2
            COMMENT "Deploying Qt frameworks to app bundle"
        )
    else()
        message(WARNING "macdeployqt not found. Run manually after build: macdeployqt build/src/xrk.app")
    endif()
endif()
