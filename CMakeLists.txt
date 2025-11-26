cmake_minimum_required(VERSION 3.10)

project(StringPrototype VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23) # Compile as C++23
set(CMAKE_CXX_STANDARD_REQUIRED ON) # Require C++23 support
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
set(CMAKE_CXX_FLAGS
    "${CMAKE_C_FLAGS} -fPIC -Wno-stringop-overflow -fasynchronous-unwind-tables -march=native"
)
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

add_subdirectory(third_party/succinct)
add_subdirectory(third_party/croaring)

file(GLOB_RECURSE SOURCE_FILES aho_corasick/**.cc aho_corasick/**/**.h)
add_executable(prototype main.cc ${SOURCE_FILES})
target_include_directories(prototype PRIVATE aho_corasick/include)
target_link_libraries(prototype succinct fmt croaring)
add_custom_target(
  format clang-format -style=file:${CMAKE_CURRENT_SOURCE_DIR}/.clang-format -i
         ${CMAKE_CURRENT_SOURCE_DIR}/main.cc
         ${CMAKE_CURRENT_SOURCE_DIR}/join_strings.h
         ${SOURCE_FILES})
