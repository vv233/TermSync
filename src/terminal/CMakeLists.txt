# terminal: VT parser + screen model. Pure logic (Qt Core only, no Widgets),
# so it is unit-testable headlessly. The QPainter renderer lives in src/ui.

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(termsync_terminal STATIC
    screen/Color.h
    screen/Cell.h
    screen/ScreenBuffer.h
    screen/ScreenBuffer.cpp
    vt/VtParser.h
    vt/VtParser.cpp
    text/KeywordHighlighter.h
    text/KeywordHighlighter.cpp
    text/HexView.h
    text/HexView.cpp
    theme/ColorScheme.h
    theme/ColorScheme.cpp
)

target_include_directories(termsync_terminal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(termsync_terminal
    PUBLIC
        Qt6::Core
)
