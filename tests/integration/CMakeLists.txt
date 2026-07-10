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

add_executable(sftp_smoke
    sftp_smoke.cpp
)
target_link_libraries(sftp_smoke
    PRIVATE
        termsync_transfer
        Qt6::Core
)

add_executable(sftp_bench
    sftp_bench.cpp
)
target_link_libraries(sftp_bench
    PRIVATE
        termsync_transfer
        Qt6::Core
)

add_executable(sftp_resume
    sftp_resume.cpp
)
target_link_libraries(sftp_resume
    PRIVATE
        termsync_transfer
        Qt6::Core
)

add_executable(sftp_features
    sftp_features.cpp
)
target_link_libraries(sftp_features
    PRIVATE
        termsync_transfer
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

add_executable(ftp_smoke
    ftp_smoke.cpp
)
target_link_libraries(ftp_smoke
    PRIVATE
        termsync_transfer
        Qt6::Core
)

add_executable(telnet_smoke
    telnet_smoke.cpp
)
target_link_libraries(telnet_smoke
    PRIVATE
        termsync_core
        Qt6::Core
)

# X11 forwarding end-to-end (needs a live SSH server + a reachable X server).
add_executable(x11_smoke
    x11_smoke.cpp
)
target_link_libraries(x11_smoke
    PRIVATE
        termsync_core
        Qt6::Core
        Qt6::Network
)

# Headless (no server): runs the platform shell and checks a marker echoes back.
add_executable(local_shell_smoke
    local_shell_smoke.cpp
)
target_link_libraries(local_shell_smoke
    PRIVATE
        termsync_core
        Qt6::Core
)

# Headless (no external server): TFTP server WRQ+RRQ round-trip over loopback.
add_executable(tftp_smoke
    tftp_smoke.cpp
)
target_link_libraries(tftp_smoke
    PRIVATE
        termsync_core
        Qt6::Core
        Qt6::Network
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

add_executable(dualpane_render_smoke
    dualpane_render_smoke.cpp
)
target_link_libraries(dualpane_render_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# Renders the Windows-11-Explorer-style SFTP browser against a live server.
add_executable(explorer_render_smoke
    explorer_render_smoke.cpp
)
target_link_libraries(explorer_render_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# End-to-end: drag-and-drop upload in the Explorer SFTP browser (needs a server).
add_executable(drag_upload_smoke
    drag_upload_smoke.cpp
)
target_link_libraries(drag_upload_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# Headless (no server): verifies the TerminalWidget session-logging tee.
add_executable(session_log_smoke
    session_log_smoke.cpp
)
target_link_libraries(session_log_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# Render smoke (no server): TerminalWidget keyword highlighting -> PNG.
add_executable(highlight_render_smoke
    highlight_render_smoke.cpp
)
target_link_libraries(highlight_render_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# Render smoke (no server): TerminalWidget Hex View -> PNG.
add_executable(hexview_render_smoke
    hexview_render_smoke.cpp
)
target_link_libraries(hexview_render_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# UI smoke (no server): DualPaneBrowser bookmarks + sync-browse toolbar -> PNG.
add_executable(dualpane_ui_smoke
    dualpane_ui_smoke.cpp
)
target_link_libraries(dualpane_ui_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# Menu smoke (no server): MainWindow M20 menu wiring -> PNGs + assertions.
add_executable(mainwindow_menu_smoke
    mainwindow_menu_smoke.cpp
)
target_link_libraries(mainwindow_menu_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# Renders the whole MainWindow to a PNG (UI review).
add_executable(mainwindow_render_smoke
    mainwindow_render_smoke.cpp
)
target_link_libraries(mainwindow_render_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# Renders the Hosts home page with demo cards (UI review).
add_executable(home_render_smoke
    home_render_smoke.cpp
)
target_link_libraries(home_render_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# Renders the terminal "connecting…" overlay (UI review).
add_executable(connecting_render_smoke
    connecting_render_smoke.cpp
)
target_link_libraries(connecting_render_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# Render smoke (no server): local shell rendered in a TerminalWidget -> PNG.
add_executable(local_shell_render_smoke
    local_shell_render_smoke.cpp
)
target_link_libraries(local_shell_render_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# Render smoke (no server): terminal colour scheme + font -> PNG.
add_executable(theme_render_smoke
    theme_render_smoke.cpp
)
target_link_libraries(theme_render_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# Render smoke (no server): the Terminal Appearance picker dialog -> PNG.
add_executable(appearance_dialog_smoke
    appearance_dialog_smoke.cpp
)
target_link_libraries(appearance_dialog_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)

# Render smoke (no admin): the TFTP Server control dialog -> PNG.
add_executable(tftp_dialog_smoke
    tftp_dialog_smoke.cpp
)
target_link_libraries(tftp_dialog_smoke
    PRIVATE
        termsync_ui
        Qt6::Widgets
)
