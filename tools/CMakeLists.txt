# Standalone tool to demonstrate the cross-keyboard-layout keyboard fix on a
# real desktop session. Builds a small GUI that injects keystrokes into a text
# box via both the old (VK-only) and new (Unicode) paths while forcing the
# thread's keyboard layout to a different one.
add_executable(keyboard_layout_test keyboard_layout_test.cpp)
target_link_libraries(keyboard_layout_test PRIVATE xrk_hw Qt6::Widgets)

# Headless, no-GUI measurement of the cross-layout behaviour (reproduces the
# root cause and the fix at the Win32 API level). Runnable from a terminal.
add_executable(keyboard_layout_probe keyboard_layout_probe.cpp)
target_link_libraries(keyboard_layout_probe PRIVATE user32)
