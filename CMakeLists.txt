cmake_minimum_required(VERSION 3.25)

project(TermSync
    VERSION 0.1.0
    DESCRIPTION "Open-source SSH terminal + SFTP/FTP client"
    LANGUAGES CXX)

set(TERMSYNC_PRERELEASE_LABEL "pre.1" CACHE STRING "Pre-release label")
set(TERMSYNC_VERSION "${PROJECT_VERSION}-${TERMSYNC_PRERELEASE_LABEL}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/termsync-version.txt"
    CONTENT "${TERMSYNC_VERSION}\n")

# --- Global settings -------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# On Windows, prevent <windows.h> (pulled in transitively via libssh2/curl)
# from defining the min/max macros, which collide with std::min/std::max and
# std::numeric_limits<>::max(). Also trim the Win32 header surface.
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Qt code generation
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Development builds default to build/<preset>/bin. The release script overrides
# this so distributable executables are written directly to /release.
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

option(TERMSYNC_BUILD_TESTS "Build unit tests" ON)

# --- Dependencies ----------------------------------------------------------
# Qt 6 is located via CMAKE_PREFIX_PATH (see docs/building.md).
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network Qml)

qt_standard_project_setup()

function(termsync_deploy_qsqlite target_name)
    # Release deployment is handled by scripts/make-release.ps1.
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
add_subdirectory(src/core)
add_subdirectory(src/terminal)
add_subdirectory(src/transfer)
add_subdirectory(src/script)
add_subdirectory(src/ui)
add_subdirectory(src/app)
add_subdirectory(src/cli)

if(TERMSYNC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
