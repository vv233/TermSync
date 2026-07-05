# Integration harnesses that need a live server. Built only when
# TERMSYNC_BUILD_INTEGRATION is ON; not run as part of ctest by default.

add_executable(ssh_smoke
    ssh_smoke.cpp
)

target_link_libraries(ssh_smoke
    PRIVATE
        termsync_core
        Qt6::Core
)
