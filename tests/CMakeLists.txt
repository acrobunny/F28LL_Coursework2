add_executable(queue_tests
    TestLockedQueue.cpp
)

target_link_libraries(queue_tests PRIVATE
    concurrent_queue
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(queue_tests)
