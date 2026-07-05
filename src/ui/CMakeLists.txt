# UI module: main window shell, dialogs, terminal/transfer views.

add_library(termsync_ui STATIC
    mainwindow/MainWindow.h
    mainwindow/MainWindow.cpp
    session_dialogs/QuickConnectDialog.h
    session_dialogs/QuickConnectDialog.cpp
    terminal_view/RawTerminalView.h
    terminal_view/RawTerminalView.cpp
    terminal_view/TerminalWidget.h
    terminal_view/TerminalWidget.cpp
    transfer_view/SftpBrowserWidget.h
    transfer_view/SftpBrowserWidget.cpp
    transfer_view/DualPaneBrowser.h
    transfer_view/DualPaneBrowser.cpp
)

target_include_directories(termsync_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(termsync_ui
    PUBLIC
        Qt6::Widgets
        termsync_core
        termsync_terminal
        termsync_transfer
)
