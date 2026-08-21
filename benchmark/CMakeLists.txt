include(DuckDBRuntime)

# One target wants the DuckDB wiring today. It stays a function because the
# next thing that needs an engine baseline should get the same flags and the
# same link order rather than a second hand copy of them.
function(ac_link_duckdb target)
  if(NOT DUCKDB_FOUND)
    return()
  endif()
  target_compile_definitions(${target} PRIVATE HN_BENCH_HAVE_DUCKDB=1
                             HN_BENCH_DUCKDB_VERSION="${DUCKDB_RUNTIME_VERSION}")
  # Linked last on purpose, see the note in cmake/DuckDBRuntime.cmake about
  # the include directory ordering against fmt.
  target_link_libraries(${target} PUBLIC duckdb::duckdb)
endfunction()

# baseline_bench and hn_bench used to be two binaries here. They ran the same
# kernel over data from two different places and had grown two of everything
# downstream of that, so their numbers were not comparable. This is both.
add_executable(ac_bench ac_bench.cc)
target_link_libraries(ac_bench PUBLIC ahocorasick fmt::fmt csv_parser)
# For #include "benchmark/workload.h", which test/test_golden.cc includes by the
# same spelling so the two cannot drift apart.
target_include_directories(ac_bench PRIVATE ${CMAKE_SOURCE_DIR})
ac_link_duckdb(ac_bench)
