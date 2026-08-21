#pragma once

// The real DuckDB engine baseline, in process.
//
// This is the baseline that a speedup can honestly be quoted against, unlike
// the nested loop in reference.h. It is optional at configure time, so
// everything here is behind HN_BENCH_HAVE_DUCKDB and the callers still build
// without libduckdb. Making it required is an M5 item and is not this header's
// job yet.
//
// There were two copies of this scaffolding, one per benchmark, and they had
// drifted: one checked every C API return and one checked none, one supported
// ILIKE and ESCAPE and one hardcoded LIKE, and both wrote the same appender
// loop out twice. The differences were not decisions, they were the order the
// two files happened to get written in.

#include "fmt/format.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#ifdef HN_BENCH_HAVE_DUCKDB
#include <duckdb.h>

namespace bench {

// Exit rather than throw. Every caller is a benchmark whose numbers are
// meaningless if the engine did not answer, and there is nothing above main
// that would do anything but print this and exit anyway.
inline auto DuckCheck(duckdb_state s, const char *what) -> void {
  if (s != DuckDBSuccess) {
    fmt::print(stderr, "!! DuckDB error in {}\n", what);
    std::exit(3);
  }
}

// Open, connect and create the two tables. Destructor closes, so an exit path
// that forgets to is not a leak the next reader has to notice.
class EngineDb {
 public:
  EngineDb() {
    DuckCheck(duckdb_open(nullptr, &db_), "open");
    DuckCheck(duckdb_connect(db_, &con_), "connect");
    duckdb_result tmp{};
    DuckCheck(duckdb_query(con_,
                           "CREATE TABLE texts(text_id BIGINT, text VARCHAR);"
                           "CREATE TABLE patterns(pattern_id BIGINT, pattern VARCHAR);",
                           &tmp),
              "create tables");
    duckdb_destroy_result(&tmp);
  }

  ~EngineDb() {
    duckdb_disconnect(&con_);
    duckdb_close(&db_);
  }

  EngineDb(const EngineDb &)                     = delete;
  auto operator=(const EngineDb &) -> EngineDb & = delete;
  EngineDb(EngineDb &&)                          = delete;
  auto operator=(EngineDb &&) -> EngineDb      & = delete;

  auto con() const -> duckdb_connection { return con_; }

 private:
  duckdb_database db_{};
  duckdb_connection con_{};
};

// Bulk load one side through the Appender API.
//
// `ids` may be empty, in which case the row's position is its id. The
// synthetic workloads have no ids of their own and the loaded corpora do, and
// which one you are looking at changes what a pair in the result means, so it
// is a parameter rather than an assumption.
inline auto AppendColumn(duckdb_connection con, const char *table, const std::vector<std::string> &values,
                         const std::vector<int64_t> &ids) -> void {
  duckdb_appender ap{};
  DuckCheck(duckdb_appender_create(con, nullptr, table, &ap), "appender create");
  for (size_t i = 0; i < values.size(); i++) {
    const int64_t id = ids.empty() ? static_cast<int64_t>(i) : ids[i];
    DuckCheck(duckdb_append_int64(ap, id), "append id");
    DuckCheck(duckdb_append_varchar_length(ap, values[i].data(), static_cast<idx_t>(values[i].size())), "append value");
    DuckCheck(duckdb_appender_end_row(ap), "end row");
  }
  DuckCheck(duckdb_appender_destroy(&ap), "appender destroy");
}

inline auto LoadTables(duckdb_connection con, const std::vector<std::string> &texts,
                       const std::vector<int64_t> &text_ids, const std::vector<std::string> &patterns,
                       const std::vector<int64_t> &pattern_ids) -> void {
  AppendColumn(con, "texts", texts, text_ids);
  AppendColumn(con, "patterns", patterns, pattern_ids);
}

// SQL literal quoting, for the ESCAPE clause. The escape character comes out
// of a workload's meta.json and so is not ours, which is the whole reason it
// gets quoted rather than concatenated.
inline auto SqlQuote(std::string_view in) -> std::string {
  std::string out;
  out.reserve(in.size() + 2);
  out.push_back('\'');
  for (char c : in) {
    if (c == '\'') { out.push_back('\''); }
    out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

// The join, with `select_list` deciding whether you get the count or the pairs
// themselves. Both callers want the same join and differ only there, and a
// verification running a subtly different query from the one that was timed
// would be verifying the wrong thing.
inline auto JoinSql(std::string_view select_list, bool ilike, std::string_view escape,
                    std::string_view tail = {}) -> std::string {
  auto sql =
    fmt::format("SELECT {} FROM texts t JOIN patterns p ON t.text {} p.pattern", select_list, ilike ? "ILIKE" : "LIKE");
  if (!escape.empty()) {
    sql += " ESCAPE ";
    sql += SqlQuote(escape);
  }
  if (!tail.empty()) {
    sql += ' ';
    sql += tail;
  }
  return sql;
}

}  // namespace bench

#endif  // HN_BENCH_HAVE_DUCKDB
