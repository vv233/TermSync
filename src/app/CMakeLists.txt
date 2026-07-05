# The application executable: thin bootstrap that wires modules together.
qt_add_executable(termsync
    main.cpp
)

target_link_libraries(termsync
    PRIVATE
        termsync_ui
)

set_target_properties(termsync PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

termsync_deploy_qsqlite(termsync)

# --- Install / deployment (M10) --------------------------------------------
install(TARGETS termsync
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .)

install(FILES
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION .)

# Bundle the non-Qt runtime DLLs (libssh2, libcurl, OpenSSL, zlib — provided by
# vcpkg). windeployqt below handles only the Qt DLLs/plugins, so these must be
# copied explicitly or the packaged app fails with "libssh2.dll not found".
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    install(CODE "
        set(_vcpkg_bin \"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin\")
        if(\"\${CMAKE_INSTALL_CONFIG_NAME}\" MATCHES \"[Dd]ebug\")
            set(_vcpkg_bin \"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin\")
        endif()
        file(GLOB _runtime_dlls \"\${_vcpkg_bin}/*.dll\")
        if(_runtime_dlls)
            file(INSTALL \${_runtime_dlls} DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
        else()
            message(WARNING \"No vcpkg runtime DLLs found in \${_vcpkg_bin}\")
        endif()
    ")
endif()

# Bundle the Qt runtime (DLLs + plugins) beside the installed executable so the
# app is self-contained. On Windows this uses windeployqt.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${QT6_INSTALL_PREFIX}/bin" "${_qt_bin_dir}"
        PATHS "$ENV{QTDIR}/bin")
    if(WINDEPLOYQT_EXECUTABLE)
        # windeployqt auto-detects debug/release from the binary.
        install(CODE "
            execute_process(COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                --no-compiler-runtime --no-translations
                \"\${CMAKE_INSTALL_PREFIX}/termsync.exe\")
        ")
    else()
        message(WARNING "windeployqt not found; the installed app will need Qt on PATH.")
    endif()
endif()

# A convenience 'deploy' target for local, self-contained runs (dev builds).
if(WIN32 AND WINDEPLOYQT_EXECUTABLE)
    add_custom_target(deploy
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations
                "$<TARGET_FILE:termsync>"
        DEPENDS termsync
        COMMENT "Bundling Qt runtime next to termsync.exe via windeployqt")
endif()
