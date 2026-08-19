# Dependency resolution for the prototype.
#
# By default fmt, oneTBB and GoogleTest are fetched at configure time at the
# exact versions pinned below and checked against a SHA256. Before this file
# existed the build called find_package() and took whatever the host had,
# which was a different version on every machine we tried:
#
#   component   server3 (Ubuntu 24.04.4)   macOS arm64
#   fmt         9.1.0                      12.2.0
#   oneTBB      2021.11.0                  2023.1.0
#   GoogleTest  1.14.0                     1.18.0
#
# A reviewer reproducing the paper on their own machine got a fourth set. The
# versions here are the same ones tamnd/prototype-string pins, so the two
# repos build the same thing.
#
# Set USE_SYSTEM_DEPS=ON to go back to find_package(). That is the right
# choice for a distro package or a machine with no network, and it is exactly
# what the build did before, so it is not a regression path.
#
# CRoaring is not here. It is vendored under third_party/croaring as the
# upstream single header amalgamation, so it is already pinned by being
# checked in, and its version is the ROARING_VERSION define in roaring.h.
#
# DuckDB is not here either. It is not a build dependency of this repo. It
# appears as a prebuilt binary in bin/, and what that binary is and where it
# came from is in docs/umbra-binaries.md.

include(FetchContent)

option(USE_SYSTEM_DEPS "Resolve fmt, TBB and GoogleTest with find_package instead of the pinned versions" OFF)

set(WJ_FMT_VERSION "11.0.2")
set(WJ_FMT_SHA256 "6cb1e6d37bdcb756dbbe59be438790db409cdb4868c66e888d5df9f13f7c027f")
set(WJ_TBB_VERSION "2021.13.0")
set(WJ_TBB_SHA256 "3ad5dd08954b39d113dc5b3f8a8dc6dc1fd5250032b7c491eb07aed5c94133e1")
set(WJ_GTEST_VERSION "1.15.2")
set(WJ_GTEST_SHA256 "7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926")

# Fetching needs network on the first configure of a given build directory,
# and only then. To build with no network, either pass USE_SYSTEM_DEPS=ON, or
# point FETCHCONTENT_SOURCE_DIR_FMT, FETCHCONTENT_SOURCE_DIR_TBB and
# FETCHCONTENT_SOURCE_DIR_GOOGLETEST at copies you already have unpacked.

function(wj_report_dependency name how version)
  message(STATUS "prototype: ${name} ${version} (${how})")
endfunction()

# A dependency added with add_subdirectory, which is what FetchContent does,
# is not an imported target, so its headers are not system headers and every
# warning in them lands on us. TBB's concurrent_unordered_map header uses
# std::aligned_storage, which C++23 deprecates, and that alone is enough to
# fail a -Werror build on code we do not own. The SYSTEM target property
# needs cmake 3.25.
function(wj_mark_system)
  foreach(target ${ARGV})
    if(TARGET ${target})
      set_target_properties(${target} PROPERTIES SYSTEM TRUE)
    endif()
  endforeach()
endfunction()

if(USE_SYSTEM_DEPS)
  find_package(fmt REQUIRED)
  find_package(TBB REQUIRED)
  wj_report_dependency(fmt "system" "${fmt_VERSION}")
  wj_report_dependency(TBB "system" "${TBB_VERSION}")
else()
  FetchContent_Declare(
    fmt
    URL "https://github.com/fmtlib/fmt/archive/refs/tags/${WJ_FMT_VERSION}.tar.gz"
    URL_HASH SHA256=${WJ_FMT_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

  # TBB_TEST off because its own test suite is not our problem and roughly
  # doubles the build. TBB_STRICT off because it turns warnings into errors
  # and a compiler newer than the release will trip on something eventually.
  set(TBB_TEST OFF CACHE BOOL "" FORCE)
  set(TBB_STRICT OFF CACHE BOOL "" FORCE)
  set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(TBBMALLOC_BUILD OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    tbb
    URL "https://github.com/uxlfoundation/oneTBB/archive/refs/tags/v${WJ_TBB_VERSION}.tar.gz"
    URL_HASH SHA256=${WJ_TBB_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

  FetchContent_MakeAvailable(fmt tbb)
  wj_mark_system(fmt tbb)
  wj_report_dependency(fmt "pinned" "${WJ_FMT_VERSION}")
  wj_report_dependency(TBB "pinned" "${WJ_TBB_VERSION}")
endif()

# GoogleTest is only pulled in under ENABLE_TESTING, and it has to stay that
# way. A library only build has to work on a machine with no gtest at all,
# and that is one of the things a reviewer's machine looks like.
function(wj_provide_googletest)
  if(USE_SYSTEM_DEPS)
    find_package(GTest REQUIRED)
    wj_report_dependency(GoogleTest "system" "${GTest_VERSION}")
    return()
  endif()
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    URL "https://github.com/google/googletest/archive/refs/tags/v${WJ_GTEST_VERSION}.tar.gz"
    URL_HASH SHA256=${WJ_GTEST_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(googletest)
  wj_mark_system(gtest gtest_main)
  wj_report_dependency(GoogleTest "pinned" "${WJ_GTEST_VERSION}")
endfunction()
