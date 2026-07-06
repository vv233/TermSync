# Unit tests. GoogleTest is provided by vcpkg (the "tests" feature).
find_package(GTest CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Sql Network Qml)

add_executable(termsync_unit_tests
    unit/test_main.cpp
    unit/smoke_test.cpp
    unit/vt_parser_test.cpp
    unit/profile_store_test.cpp
    unit/sync_diff_test.cpp
    unit/socks5_test.cpp
    unit/proxy_client_test.cpp
    unit/xymodem_test.cpp
    unit/script_engine_test.cpp
    unit/tn3270_stream_test.cpp
    unit/tn5250_stream_test.cpp
    unit/scheduler_test.cpp
    unit/bookmark_test.cpp
    unit/text_features_test.cpp
    unit/session_logger_test.cpp
    unit/config_transfer_test.cpp
)

target_link_libraries(termsync_unit_tests
    PRIVATE
        GTest::gtest
        termsync_terminal
        termsync_core
        termsync_transfer
        termsync_script
        Qt6::Core
        Qt6::Sql
        Qt6::Network
        Qt6::Qml
)

termsync_deploy_qsqlite(termsync_unit_tests)

include(GoogleTest)
# Discover tests at ctest-time (not build-time): the test exe links Qt6Core,
# whose DLL is only guaranteed to be on PATH when running ctest, not during the
# build step.
gtest_discover_tests(termsync_unit_tests DISCOVERY_MODE PRE_TEST)

option(TERMSYNC_BUILD_INTEGRATION "Build integration harnesses (need a live server)" OFF)
if(TERMSYNC_BUILD_INTEGRATION)
    add_subdirectory(integration)
endif()
