cmake_minimum_required(VERSION 3.16)
project(FastBook LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Sanitizer option (ASan + UBSan)
option(ENABLE_SAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" ON)

# Enable compiler warnings & sanitizers
if(MSVC)
    add_compile_options(/W4 /permissive-)
    if(ENABLE_SAN)
        add_compile_options(/fsanitize=address)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(ENABLE_SAN)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Dependencies: Only Catch2 for testing (Zero heavy external dependencies)
include(FetchContent)

find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.2
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# Source files
set(SOURCES
    src/order_book.cpp
)

# Header files
set(HEADERS
    include/fastbook/messages.hpp
    include/fastbook/order_book.hpp
    include/fastbook/matching_engine.hpp
)

# Main demo executable
add_executable(fastbook_demo
    src/main.cpp
    ${SOURCES}
)

target_include_directories(fastbook_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Test executable (Unit tests + Randomized Differential Fuzz tests)
add_executable(fastbook_tests
    tests/test_matching_engine.cpp
    tests/test_differential.cpp
    ${SOURCES}
)

target_include_directories(fastbook_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(fastbook_tests PRIVATE
    Catch2::Catch2WithMain
)

# Enable testing
enable_testing()
add_test(NAME fastbook_tests COMMAND fastbook_tests)

# Install rules
install(TARGETS fastbook_demo fastbook_tests
        RUNTIME DESTINATION bin)
install(FILES ${HEADERS}
        DESTINATION include/fastbook)