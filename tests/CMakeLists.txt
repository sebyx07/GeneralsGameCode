# TheSuperHackers @feature Claude 08/01/2026 Add test infrastructure with Google Test
cmake_minimum_required(VERSION 3.25)

# Fetch Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

enable_testing()

# Thread pool tests - standalone, doesn't need Windows libraries
add_executable(ThreadPoolTest
    threadpool/ThreadPoolTest.cpp
)

target_link_libraries(ThreadPoolTest PRIVATE
    gtest_main
    gmock
)

# Include the WWLib directory for headers (ThreadPool.h, Parallelize.h)
target_include_directories(ThreadPoolTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWLib
)

# Platform-specific threading libraries
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(ThreadPoolTest PRIVATE Threads::Threads)
endif()

include(GoogleTest)
gtest_discover_tests(ThreadPoolTest)

# Parallelization utility tests - standalone
add_executable(ParallelizeTest
    parallelize/ParallelizeTest.cpp
)

target_link_libraries(ParallelizeTest PRIVATE
    gtest_main
    gmock
)

target_include_directories(ParallelizeTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWLib
)

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(ParallelizeTest PRIVATE Threads::Threads)
endif()

gtest_discover_tests(ParallelizeTest)
