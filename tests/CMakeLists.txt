# Unit tests. GoogleTest is provided by vcpkg (the "tests" feature).
find_package(GTest CONFIG REQUIRED)

add_executable(termsync_unit_tests
    unit/smoke_test.cpp
    unit/vt_parser_test.cpp
)

target_link_libraries(termsync_unit_tests
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        termsync_terminal
)

include(GoogleTest)
# Discover tests at ctest-time (not build-time): the test exe links Qt6Core,
# whose DLL is only guaranteed to be on PATH when running ctest, not during the
# build step.
gtest_discover_tests(termsync_unit_tests DISCOVERY_MODE PRE_TEST)

option(TERMSYNC_BUILD_INTEGRATION "Build integration harnesses (need a live server)" OFF)
if(TERMSYNC_BUILD_INTEGRATION)
    add_subdirectory(integration)
endif()
