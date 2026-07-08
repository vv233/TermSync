# core: shared backbone (no Qt Widgets dependency).
# Depends on Qt Core + Sql (for the SQLite profile store) plus the network
# libraries. This keeps it headlessly unit-testable.

find_package(Qt6 REQUIRED COMPONENTS Core Sql Network)
find_package(libssh2 CONFIG REQUIRED)

add_library(termsync_core STATIC
    AbstractTerminalConnection.h
    model/ConnectionProfile.h
    model/Bookmark.h
    browse/PathMirror.h
    store/ProfileStore.h
    store/ProfileStore.cpp
    store/BookmarkStore.h
    store/BookmarkStore.cpp
    store/ConfigTransfer.h
    store/ConfigTransfer.cpp
    log/SessionLogger.h
    log/SessionLogger.cpp
    credential/CredentialStore.h
    credential/CredentialStore.cpp
    net/Socks5.h
    net/Socks5.cpp
    net/ProxyClient.h
    net/ProxyClient.cpp
    ssh/SshConnection.h
    ssh/SshConnection.cpp
    ssh/X11Auth.h
    ssh/X11Auth.cpp
    ssh/PortForwarder.h
    ssh/PortForwarder.cpp
    telnet/TelnetConnection.h
    telnet/TelnetConnection.cpp
    serial/SerialConnection.h
    serial/SerialConnection.cpp
    local/LocalShellConnection.h
    local/LocalShellConnection.cpp
    tftp/TftpProtocol.h
    tftp/TftpProtocol.cpp
    tftp/TftpServer.h
    tftp/TftpServer.cpp
    tn3270/Tn3270Stream.h
    tn3270/Tn3270Stream.cpp
    tn3270/Tn3270Connection.h
    tn3270/Tn3270Connection.cpp
    tn3270/Tn5250Stream.h
    tn3270/Tn5250Stream.cpp
    tn3270/Tn5250Connection.h
    tn3270/Tn5250Connection.cpp
)

target_include_directories(termsync_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(termsync_core
    PUBLIC
        Qt6::Core
        Qt6::Sql
        Qt6::Network
    PRIVATE
        libssh2::libssh2
)

if(WIN32)
    # ws2_32 for sockets, advapi32 for the Credential Manager (CredWrite/Read).
    target_link_libraries(termsync_core PRIVATE ws2_32 advapi32)
endif()
