# Host-side unit tests for lib/core. Build with:
#   cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
cmake_minimum_required(VERSION 3.16)
project(tw_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../lib/core ${CMAKE_BINARY_DIR}/tw_core)

add_executable(tw_tests
    test_time.cpp
    test_ntp.cpp
    test_weather.cpp
    test_ds3231.cpp
    test_recovery.cpp
    test_render.cpp
)
target_link_libraries(tw_tests PRIVATE tw_core GTest::gtest_main)
if(NOT MSVC)
    target_compile_options(tw_tests PRIVATE -Wall -Wextra)
    target_compile_options(tw_core PRIVATE -Wall -Wextra)
endif()

enable_testing()
include(GoogleTest)
gtest_discover_tests(tw_tests)
