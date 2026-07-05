# termsync-cli: headless SFTP/FTP transfer & sync tool (SFXCL-style).
add_executable(termsync_cli
    main.cpp
)

set_target_properties(termsync_cli PROPERTIES OUTPUT_NAME "termsync-cli")

target_link_libraries(termsync_cli
    PRIVATE
        termsync_transfer
        termsync_core
        Qt6::Core
)
