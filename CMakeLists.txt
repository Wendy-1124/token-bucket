cmake_minimum_required(VERSION 3.14)
project(token-bucket VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Header-only library
add_library(token_bucket INTERFACE)
target_include_directories(token_bucket INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Optional: Build examples
option(TOKEN_BUCKET_BUILD_EXAMPLES "Build example programs" OFF)
if(TOKEN_BUCKET_BUILD_EXAMPLES)
    add_executable(example_basic_usage examples/basic_usage.cpp)
    target_link_libraries(example_basic_usage PRIVATE token_bucket)
    target_compile_options(example_basic_usage PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )
endif()

# Optional: Build tests
option(TOKEN_BUCKET_BUILD_TESTS "Build unit tests" OFF)
if(TOKEN_BUCKET_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.13.0.zip
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(test_token_bucket tests/test_token_bucket.cpp)
    target_link_libraries(test_token_bucket PRIVATE
        token_bucket
        GTest::gtest_main
    )

    enable_testing()
    add_test(NAME TokenBucketTests COMMAND test_token_bucket)

    # Compiler warnings
    target_compile_options(test_token_bucket PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )
endif()

# Install targets (for package managers)
install(TARGETS token_bucket
    EXPORT token_bucketConfig
)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT token_bucketConfig DESTINATION lib/cmake/token_bucket)
