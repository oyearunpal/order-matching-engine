cmake_minimum_required(VERSION 3.20)
project(order_matching_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Shared compile settings for every target in this project.
add_library(project_options INTERFACE)
target_compile_options(project_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
                                                 -Wsign-conversion -Wnon-virtual-dtor)
if(WARNINGS_AS_ERRORS)
  target_compile_options(project_options INTERFACE -Werror)
endif()
if(ENABLE_SANITIZERS)
  target_compile_options(project_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer
                                                   -fno-sanitize-recover=all)
  target_link_options(project_options INTERFACE -fsanitize=address,undefined)
endif()

# Core library: price, parser, order book (header-only template), session.
add_library(matching_lib STATIC
  src/price.cpp
  src/parser.cpp
  src/session.cpp)
target_include_directories(matching_lib PUBLIC include)
target_link_libraries(matching_lib PUBLIC project_options)

add_executable(matching_engine src/main.cpp)
target_link_libraries(matching_engine PRIVATE matching_lib)

# Tools: random order-flow generator and benchmark.
add_executable(gen_orders tools/gen_orders.cpp)
target_include_directories(gen_orders PRIVATE tools)
target_link_libraries(gen_orders PRIVATE matching_lib)

add_executable(bench tools/bench.cpp)
target_include_directories(bench PRIVATE tools)
target_link_libraries(bench PRIVATE matching_lib)

# Tests.
enable_testing()

add_executable(unit_tests
  tests/test_main.cpp
  tests/test_price.cpp
  tests/test_parser.cpp
  tests/test_order_book.cpp
  tests/test_session.cpp
  tests/test_randomized.cpp)
target_include_directories(unit_tests PRIVATE tests tools)
target_link_libraries(unit_tests PRIVATE matching_lib)
add_test(NAME unit_tests COMMAND unit_tests)

# End-to-end: every tests/data/<name>.in is piped through the app and its
# stdout (and stderr, when <name>.err exists) compared byte-for-byte.
file(GLOB E2E_INPUTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/data/*.in)
foreach(input ${E2E_INPUTS})
  get_filename_component(name ${input} NAME_WE)
  add_test(NAME e2e_${name}
           COMMAND ${CMAKE_COMMAND}
                   -DAPP=$<TARGET_FILE:matching_engine>
                   -DCASE=${CMAKE_SOURCE_DIR}/tests/data/${name}
                   -P ${CMAKE_SOURCE_DIR}/tests/run_e2e.cmake)
endforeach()
