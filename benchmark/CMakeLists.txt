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

# No DuckDB. It measures the probe against itself at two pattern counts and two
# row shapes, so nothing here needs a second implementation, and a guard that
# CI cannot run without an optional runtime is a guard that does not run.
add_executable(perf_guard perf_guard.cc)
target_link_libraries(perf_guard PUBLIC ahocorasick fmt::fmt)
target_include_directories(perf_guard PRIVATE ${CMAKE_SOURCE_DIR})
