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
find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

# --- Modules ---------------------------------------------------------------
# Layered: core (no widgets) <- terminal / transfer <- ui <- app.
# Only the pieces that exist in the current milestone are wired up here;
# later milestones add_subdirectory their own modules.
add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(src/app)

if(TERMSYNC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
