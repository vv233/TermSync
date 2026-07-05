# Unit tests. GoogleTest is provided by vcpkg (the "tests" feature).
find_package(GTest CONFIG REQUIRED)

add_executable(termsync_unit_tests
    unit/smoke_test.cpp
)

target_link_libraries(termsync_unit_tests
    PRIVATE
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(termsync_unit_tests)

option(TERMSYNC_BUILD_INTEGRATION "Build integration harnesses (need a live server)" OFF)
if(TERMSYNC_BUILD_INTEGRATION)
    add_subdirectory(integration)
endif()
