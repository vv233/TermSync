# core: shared backbone (no Qt Widgets dependency).
# Depends on Qt Core + Sql (for the SQLite profile store) plus the network
# libraries. This keeps it headlessly unit-testable.

find_package(Qt6 REQUIRED COMPONENTS Core Sql Network)
find_package(libssh2 CONFIG REQUIRED)

add_library(termsync_core STATIC
    model/ConnectionProfile.h
    store/ProfileStore.h
    store/ProfileStore.cpp
    credential/CredentialStore.h
    credential/CredentialStore.cpp
    net/Socks5.h
    net/Socks5.cpp
    ssh/SshConnection.h
    ssh/SshConnection.cpp
    ssh/PortForwarder.h
    ssh/PortForwarder.cpp
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
