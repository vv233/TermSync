cmake_minimum_required(VERSION 3.25)

project(TermSync
    VERSION 0.1.0
    DESCRIPTION "Open-source SSH terminal + SFTP/FTP client"
    LANGUAGES CXX)

# --- Global settings -------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt code generation
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Put binaries in a predictable place so Qt deploy tools can find them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(TERMSYNC_BUILD_TESTS "Build unit tests" ON)

# --- Dependencies ----------------------------------------------------------
# Qt 6 is located via CMAKE_PREFIX_PATH (see docs/building.md).
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network)

qt_standard_project_setup()

function(termsync_deploy_qsqlite target_name)
    # M10 packaging will hand runtime deployment over to windeployqt/CPack.
    if(TARGET Qt6::QSQLiteDriverPlugin)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${target_name}>/sqldrivers"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::QSQLiteDriverPlugin>"
                "$<TARGET_FILE_DIR:${target_name}>/sqldrivers/$<TARGET_FILE_NAME:Qt6::QSQLiteDriverPlugin>"
            VERBATIM
        )
    else()
        message(WARNING "Qt SQLite driver plugin target was not found; QSQLITE will not be deployed for ${target_name}.")
    endif()
endfunction()

# --- Modules ---------------------------------------------------------------
# Layered: core (no widgets) <- terminal / transfer <- ui <- app.
# Only the pieces that exist in the current milestone are wired up here;
# later milestones add_subdirectory their own modules.
add_subdirectory(src/core)
add_subdirectory(src/terminal)
add_subdirectory(src/transfer)
add_subdirectory(src/script)
add_subdirectory(src/ui)
add_subdirectory(src/app)

if(TERMSYNC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Packaging (M10) -------------------------------------------------------
set(CPACK_PACKAGE_NAME "TermSync")
set(CPACK_PACKAGE_VENDOR "TermSync contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "SSH terminal + SFTP/FTP client")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "TermSync")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

if(WIN32)
    # NSIS installer (requires NSIS installed to actually build the package).
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_PACKAGE_NAME "TermSync")
    set(CPACK_NSIS_DISPLAY_NAME "TermSync")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "termsync.exe")
    set(CPACK_PACKAGE_EXECUTABLES "termsync;TermSync")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
