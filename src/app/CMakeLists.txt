# The application executable: thin bootstrap that wires modules together.
qt_add_executable(termsync
    main.cpp
)

target_link_libraries(termsync
    PRIVATE
        termsync_ui
)

target_compile_definitions(termsync PRIVATE
    TERMSYNC_VERSION="${TERMSYNC_VERSION}"
)

set_target_properties(termsync PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

termsync_deploy_qsqlite(termsync)
