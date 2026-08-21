# Locates libduckdb and, when found, asks the library itself which version it
# is by calling duckdb_library_version() at configure time.
#
# The version matters because DuckDB is a baseline in the paper. The two
# machines we build on ship 1.4.4 and 1.5.5, so a speedup quoted against
# "DuckDB" without a version is not a reproducible number. Nothing in the
# repo used to record it. This at least prints it in the configure log and
# puts it in the cache where a run manifest can pick it up.
#
# Sets DUCKDB_LIB, DUCKDB_INCLUDE_DIR, DUCKDB_FOUND and DUCKDB_RUNTIME_VERSION.

find_library(DUCKDB_LIB NAMES duckdb HINTS /opt/homebrew/lib /usr/local/lib)
find_path(DUCKDB_INCLUDE_DIR duckdb.h HINTS /opt/homebrew/include /usr/local/include)

if(DUCKDB_LIB AND DUCKDB_INCLUDE_DIR)
  set(DUCKDB_FOUND TRUE)
else()
  set(DUCKDB_FOUND FALSE)
  set(DUCKDB_RUNTIME_VERSION "not found")
  message(STATUS "prototype: libduckdb not found, the DuckDB baseline is disabled")
  return()
endif()

if(NOT DEFINED CACHE{DUCKDB_RUNTIME_VERSION})
  # .cc and not .c: the project only enables CXX, so a C source would not
  # compile here. duckdb.h has extern "C" guards so this is fine.
  set(_probe "${CMAKE_CURRENT_BINARY_DIR}/duckdb_version_probe.cc")
  file(WRITE "${_probe}"
       "#include <duckdb.h>\n#include <cstdio>\nint main(){std::printf(\"%s\\n\", duckdb_library_version());return 0;}\n")
  try_run(_run_rc _compile_ok
          "${CMAKE_CURRENT_BINARY_DIR}/duckdb_version_probe"
          "${_probe}"
          LINK_LIBRARIES "${DUCKDB_LIB}"
          CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${DUCKDB_INCLUDE_DIR}"
          RUN_OUTPUT_VARIABLE _duckdb_version)
  if(_compile_ok AND _run_rc EQUAL 0)
    string(STRIP "${_duckdb_version}" _duckdb_version)
  else()
    set(_duckdb_version "unknown")
  endif()
  set(DUCKDB_RUNTIME_VERSION "${_duckdb_version}" CACHE STRING "Version reported by duckdb_library_version()")
endif()

message(STATUS "prototype: DuckDB baseline enabled, ${DUCKDB_LIB} reports ${DUCKDB_RUNTIME_VERSION}")

# An imported target rather than raw variables, so that the include directory
# travels as a usage requirement and lands after the ones from targets linked
# earlier. That ordering matters here. DUCKDB_INCLUDE_DIR is the whole of
# /opt/homebrew/include or /usr/local/include, which also holds fmt, and if
# it goes on the compile line ahead of the fmt we actually link against you
# get headers from one version and symbols from another. Imported targets
# also get their includes treated as system, which stops the benchmarks from
# warning about third party code.
if(NOT TARGET duckdb::duckdb)
  add_library(duckdb::duckdb UNKNOWN IMPORTED)
  set_target_properties(duckdb::duckdb PROPERTIES
                        IMPORTED_LOCATION "${DUCKDB_LIB}"
                        INTERFACE_INCLUDE_DIRECTORIES "${DUCKDB_INCLUDE_DIR}")
endif()
