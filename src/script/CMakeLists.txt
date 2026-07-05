# script: JavaScript automation engine (QJSEngine) exposing the crt object
# model. Depends only on Qt Qml (for QJSEngine) — no Widgets.

find_package(Qt6 REQUIRED COMPONENTS Core Qml)

add_library(termsync_script STATIC
    ScriptContext.h
    ScriptEngine.h
    ScriptEngine.cpp
)

target_include_directories(termsync_script
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(termsync_script
    PUBLIC
        Qt6::Core
        Qt6::Qml
)
