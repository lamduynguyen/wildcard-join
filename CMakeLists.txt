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

# Two targets and not one, because two different sets of things live here and
# only one of them is safe to put on vendored source.
#
# prototype_abi is codegen: the architecture, the unwind tables, the
# sanitizers. Everything linked into one binary has to agree on these, so
# croaring gets them too. prototype_flags is our warning set, which croaring
# must not get. Warning clean third party source is not something we can
# promise or fix, and the file in question is a 30k line generated
# amalgamation.
#
# These used to be one target that croaring linked for -march=native, which
# handed it -Wall -Wextra -Werror as well. That is not what the comment here
# said it did, and it held for as long as it did only because no compiler had
# warned about the amalgamation yet. When clang started warning about an unused
# function in it, -Werror turned that into a failed build in a file nobody here
# can edit.
add_library(prototype_abi INTERFACE)
target_compile_options(prototype_abi INTERFACE -fasynchronous-unwind-tables)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" AND CMAKE_BUILD_TYPE MATCHES Debug)
  target_compile_options(prototype_abi INTERFACE -fstandalone-debug)
endif()
if(NATIVE_ARCH)
  target_compile_options(prototype_abi INTERFACE -march=native)
endif()

add_library(prototype_flags INTERFACE)
target_link_libraries(prototype_flags INTERFACE prototype_abi)

target_compile_options(prototype_flags INTERFACE -Wall -Wextra)
option(WERROR "Turn warnings into errors, on in CI" OFF)
if(WERROR)
  target_compile_options(prototype_flags INTERFACE -Werror)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(prototype_flags INTERFACE -Wno-stringop-overflow)
endif()

# -march=native asks the driver to name every feature the host CPU has, and on
# some hosts the set it settles on is one the compiler then complains about
# itself. Clang 18 on a current GitHub runner resolves native to +avx10.1-256
# and warns that it is promoting that to avx10.1-512. It is a warning about the
# driver's own feature resolution, it names no file and no line, and under
# -Werror it fails the build with nothing wrong anywhere in the tree. The only
# way to act on it is to stop asking for native, which is the one thing the
# release preset exists to do.
#
# Checked for rather than assumed, because gcc has no such flag and rejecting
# an unknown -W option would be a worse failure than the one being fixed. The
# positive spelling is what is probed: a compiler that does not know
# -Winvalid-feature-combination will usually accept -Wno-invalid-feature-combination
# in silence, so probing the negative one proves nothing.
if(NATIVE_ARCH)
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-Winvalid-feature-combination HAS_INVALID_FEATURE_COMBINATION_WARNING)
  if(HAS_INVALID_FEATURE_COMBINATION_WARNING)
    target_compile_options(prototype_flags INTERFACE -Wno-invalid-feature-combination)
  endif()
endif()

message(STATUS "prototype: build type ${CMAKE_BUILD_TYPE}, NATIVE_ARCH=${NATIVE_ARCH}")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

add_subdirectory(third_party/croaring)
add_subdirectory(third_party/csv-parser)

# ---------------------------------------------------------------------------
# Aho-Corasick main library
# ---------------------------------------------------------------------------
set(SOURCE_FILES
    src/aho_corasick.cc
    src/parser.cc
    src/skeleton.cc
    src/tokenizer.cc
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

option(ENABLE_BENCHMARK "Build benchmark binaries" OFF)
if(ENABLE_BENCHMARK)
  add_subdirectory(benchmark)
endif()

option(SANITIZER "Compile with address and leak sanitizer" OFF)
if(SANITIZER)
  message(STATUS "Enable sanitizer")
  target_compile_options(prototype_abi INTERFACE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(prototype_abi INTERFACE -fsanitize=address)
endif()

# Its own option and not folded into SANITIZER, because tsan cannot be
# combined with asan at all, and because the only thing it has to say
# anything about is the optimistic lock coupling in art/, which nothing in
# the single threaded paths would exercise.
option(TSAN "Compile with thread sanitizer" OFF)
if(TSAN)
  message(STATUS "Enable thread sanitizer")
  target_compile_options(prototype_abi INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(prototype_abi INTERFACE -fsanitize=thread)
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
