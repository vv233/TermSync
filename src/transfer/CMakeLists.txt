# transfer: file-transfer engines and later queue/sync layers.

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(libssh2 CONFIG REQUIRED)
find_package(CURL REQUIRED)

add_library(termsync_transfer STATIC
    FileEngine.h
    sftp/SftpFileEngine.h
    sftp/SftpFileEngine.cpp
    ftp/FtpFileEngine.h
    ftp/FtpFileEngine.cpp
    queue/SftpSession.h
    queue/SftpSession.cpp
    sync/SyncTypes.h
    sync/DirectoryDiffer.h
    sync/DirectoryDiffer.cpp
    sync/SyncEngine.h
    sync/SyncEngine.cpp
    modem/XYModem.h
    modem/XYModem.cpp
)

target_include_directories(termsync_transfer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(termsync_transfer
    PUBLIC
        Qt6::Core
        termsync_core
    PRIVATE
        libssh2::libssh2
        CURL::libcurl
)

if(WIN32)
    target_link_libraries(termsync_transfer PRIVATE ws2_32)
endif()
