cmake_minimum_required(VERSION 3.10)

project(StringPrototype VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23) # Compile as C++23
set(CMAKE_CXX_STANDARD_REQUIRED ON) # Require C++23 support
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
set(CMAKE_CXX_FLAGS"${CMAKE_C_FLAGS} -fPIC -fasynchronous-unwind-tables -march=native")
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wno-stringop-overflow)
endif()
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # For clang-tidy.

if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Setting build type to `Debug` as none was specified.")
  set(CMAKE_BUILD_TYPE "Debug")
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" AND CMAKE_BUILD_TYPE MATCHES
                                                   Debug)
  add_compile_options(-fstandalone-debug)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(GTest REQUIRED)

add_subdirectory(third_party/succinct)
add_subdirectory(third_party/croaring)

# ---------------------------------------------------------------------------
# Aho-Corasick main library
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SOURCE_FILES aho_corasick/**.cc aho_corasick/**/**.cc aho_corasick/**/**.h)
add_library(ahocorasick STATIC ${SOURCE_FILES})
target_link_libraries(ahocorasick PUBLIC succinct fmt croaring tbb)
target_include_directories(ahocorasick PUBLIC aho_corasick/include)

# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------
option(ENABLE_TESTING "Build tests" OFF)
if(ENABLE_TESTING)
  enable_testing()
  set(CMAKE_BUILD_TYPE "DEBUG")
  add_subdirectory(test)
endif()

option(SANITIZER "Compile with sanitizer" OFF)
if(SANITIZER)
  message(STATUS "Enable sanitizer")
  target_compile_options(prototype PUBLIC -fsanitize=address)
  target_link_libraries(prototype PUBLIC asan)
endif()

file(GLOB_RECURSE TEST_FILES test/**.cc test/**.h)
add_custom_target(
  format clang-format --verbose -style=file:${CMAKE_CURRENT_SOURCE_DIR}/.clang-format -i
         ${SOURCE_FILES}
         ${TEST_FILES})
