# Integration harnesses that need a live server. Built only when
# TERMSYNC_BUILD_INTEGRATION is ON; not run as part of ctest by default.

add_executable(ssh_smoke
    ssh_smoke.cpp
)

target_link_libraries(ssh_smoke
    PRIVATE
        termsync_core
        Qt6::Core
)

add_executable(credential_smoke
    credential_smoke.cpp
)
target_link_libraries(credential_smoke
    PRIVATE
        termsync_core
        Qt6::Core
)

# GUI render smoke test (needs Qt Widgets + a GUI platform).
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(terminal_render_smoke
    terminal_render_smoke.cpp
)
target_link_libraries(terminal_render_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)
