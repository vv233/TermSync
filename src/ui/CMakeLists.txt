# UI module: main window shell, dialogs, terminal/transfer views.

add_library(termsync_ui STATIC
    common/Icons.h
    common/Icons.cpp
    mainwindow/ChromeTabWidget.h
    mainwindow/MainWindow.h
    mainwindow/MainWindow.cpp
    home/HostsHomeWidget.h
    home/HostsHomeWidget.cpp
    quick_commands/QuickCommandsWidget.h
    quick_commands/QuickCommandsWidget.cpp
    session_dialogs/QuickConnectDialog.h
    session_dialogs/QuickConnectDialog.cpp
    session_dialogs/KeywordHighlightDialog.h
    session_dialogs/KeywordHighlightDialog.cpp
    session_dialogs/TerminalAppearanceDialog.h
    session_dialogs/TerminalAppearanceDialog.cpp
    session_dialogs/TftpServerDialog.h
    session_dialogs/TftpServerDialog.cpp
    session_dialogs/SessionOptionsDialog.h
    session_dialogs/SessionOptionsDialog.cpp
    session_dialogs/GlobalOptionsDialog.h
    session_dialogs/GlobalOptionsDialog.cpp
    terminal_view/RawTerminalView.h
    terminal_view/RawTerminalView.cpp
    terminal_view/TerminalWidget.h
    terminal_view/TerminalWidget.cpp
    terminal_view/ConnectingOverlay.h
    terminal_view/ConnectingOverlay.cpp
    terminal_view/FindBar.h
    terminal_view/FindBar.cpp
    transfer_view/SftpBrowserWidget.h
    transfer_view/SftpBrowserWidget.cpp
    transfer_view/DualPaneBrowser.h
    transfer_view/DualPaneBrowser.cpp
    transfer_view/ExplorerSftpBrowser.h
    transfer_view/ExplorerSftpBrowser.cpp
    transfer_view/TransferQueueWidget.h
    transfer_view/TransferQueueWidget.cpp
    transfer_view/WinDragOut.h
    transfer_view/WinDragOut.cpp
    transfer_view/SynchronizeDialog.h
    transfer_view/SynchronizeDialog.cpp
    script/TerminalScriptContext.h
    script/TerminalScriptContext.cpp
)

target_include_directories(termsync_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(termsync_ui
    PUBLIC
        Qt6::Widgets
        Qt6::PrintSupport
        termsync_core
        termsync_terminal
        termsync_transfer
        termsync_script
)

if(WIN32)
    # Native drag-out to Explorer (OLE DoDragDrop / IDataObject) + dark title bar.
    target_link_libraries(termsync_ui PRIVATE ole32 shell32 dwmapi)
endif()
