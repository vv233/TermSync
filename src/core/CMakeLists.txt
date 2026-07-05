# core: shared backbone (no Qt Widgets dependency).
# Depends only on Qt Core (for QObject/signals/threads) plus the network
# libraries. This keeps it headlessly unit-testable.

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(libssh2 CONFIG REQUIRED)

add_library(termsync_core STATIC
    ssh/SshConnection.h
    ssh/SshConnection.cpp
)

target_include_directories(termsync_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(termsync_core
    PUBLIC
        Qt6::Core
    PRIVATE
        libssh2::libssh2
)

if(WIN32)
    target_link_libraries(termsync_core PRIVATE ws2_32)
endif()
