cmake_minimum_required(VERSION 3.25)
project(ConcurrentQueue VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_library(concurrent_queue INTERFACE)
target_include_directories(concurrent_queue INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/concurrentqueue/public)
target_link_libraries(concurrent_queue INTERFACE Threads::Threads)

add_subdirectory(tests)
