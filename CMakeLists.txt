# 3.25 and not 3.24, for the SYSTEM target property that cmake/Dependencies.cmake
# uses to keep warnings from fetched dependencies out of our build. Ubuntu
# 24.04 ships 3.28.3 and macOS homebrew is well past it.
cmake_minimum_required(VERSION 3.25)

project(StringPrototype VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23) # Compile as C++23
set(CMAKE_CXX_STANDARD_REQUIRED ON) # Require C++23 support
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # For clang-tidy.

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to `Debug` as none was specified.")
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# -march=native makes a build non-portable and non-comparable across machines,
# so it is opt-in and reported in the configure log instead of being silently on.
option(NATIVE_ARCH "Compile with -march=native" OFF)

add_library(prototype_flags INTERFACE)
target_compile_options(prototype_flags INTERFACE -fasynchronous-unwind-tables)

# On prototype_flags and not on CMAKE_CXX_FLAGS, so these apply to our code and
# not to croaring, fmt, TBB or googletest. Warning clean third party source is
# not something we can promise or fix.
target_compile_options(prototype_flags INTERFACE -Wall -Wextra)
option(WERROR "Turn warnings into errors, on in CI" OFF)
if(WERROR)
  target_compile_options(prototype_flags INTERFACE -Werror)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(prototype_flags INTERFACE -Wno-stringop-overflow)
endif()
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" AND CMAKE_BUILD_TYPE MATCHES Debug)
  target_compile_options(prototype_flags INTERFACE -fstandalone-debug)
endif()
if(NATIVE_ARCH)
  target_compile_options(prototype_flags INTERFACE -march=native)
endif()
message(STATUS "prototype: build type ${CMAKE_BUILD_TYPE}, NATIVE_ARCH=${NATIVE_ARCH}")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

add_subdirectory(third_party/croaring)

# ---------------------------------------------------------------------------
# Aho-Corasick main library
# ---------------------------------------------------------------------------
set(SOURCE_FILES
    src/aho_corasick.cc
    src/parser.cc
    src/skeleton.cc
    src/tokenizer.cc
    src/common/rand.cc
    src/common/util.cc)

add_library(ahocorasick STATIC ${SOURCE_FILES})
target_link_libraries(ahocorasick PUBLIC fmt::fmt croaring TBB::tbb prototype_flags)
target_include_directories(ahocorasick PUBLIC src/include)

# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------
option(ENABLE_TESTING "Build tests" OFF)
if(ENABLE_TESTING)
  wj_provide_googletest()
  enable_testing()
  add_subdirectory(test)
endif()

option(SANITIZER "Compile with address and leak sanitizer" OFF)
if(SANITIZER)
  message(STATUS "Enable sanitizer")
  target_compile_options(prototype_flags INTERFACE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(prototype_flags INTERFACE -fsanitize=address)
endif()

set(TEST_FILES
    test/test_ahocorasick.cc
    test/test_matching.cc
    test/matcher.h
    test/test_words.h)

add_custom_target(
  format clang-format --verbose -style=file:${CMAKE_CURRENT_SOURCE_DIR}/.clang-format -i
         ${SOURCE_FILES}
         ${TEST_FILES})
