add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(ui)
add_subdirectory(hw)

set(XRK_RESOURCES "${CMAKE_SOURCE_DIR}/resources/xrk.qrc")
if(WIN32)
    list(APPEND XRK_RESOURCES "${CMAKE_SOURCE_DIR}/resources/icons/xrk.rc")
endif()

add_executable(xrk main.cpp ${XRK_RESOURCES})
target_link_libraries(xrk PRIVATE xrk_ui xrk_hw)

# Copy translation files to output directory
if(QM_FILES)
    add_dependencies(xrk translations)
    foreach(QM_FILE ${QM_FILES})
        get_filename_component(QM_NAME ${QM_FILE} NAME)
        add_custom_command(TARGET xrk POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${QM_FILE}
                $<TARGET_FILE_DIR:xrk>/translations/${QM_NAME}
            COMMENT "Copying translation: ${QM_NAME}"
        )
    endforeach()
endif()
