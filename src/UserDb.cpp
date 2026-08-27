#include "UserDb.h"

#include <algorithm>
#include <iostream>

UserDb::UserDb() {}

UserDb::~UserDb() {
  if (db_)
    sqlite3_close(db_);
}

void UserDb::exec(const char *sql) {
  char *err = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::cerr << "[UserDb] " << sql << ": " << (err ? err : "?") << std::endl;
    sqlite3_free(err);
  }
}

bool UserDb::init(const std::string &path) {
  // Unlike the shipped dataset, this one is meant to be created on first use.
  if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
    std::cerr << "[UserDb] init: can't open '" << path
              << "': " << sqlite3_errmsg(db_) << std::endl;
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  // A count is written on every commit, so the write must not cost a disk
  // flush - losing the last few counts to a crash is of no consequence.
  exec("PRAGMA journal_mode=WAL;");
  exec("PRAGMA synchronous=NORMAL;");
  exec("CREATE TABLE IF NOT EXISTS char_freq ("
       "ch TEXT PRIMARY KEY, n INTEGER NOT NULL);");
  exec("CREATE TABLE IF NOT EXISTS pair_freq ("
       "prev TEXT NOT NULL, next TEXT NOT NULL, n INTEGER NOT NULL, "
       "PRIMARY KEY (prev, next));");

  load();
  std::cerr << "[UserDb] init: '" << path << "' (" << chars_.size()
            << " chars, " << pairs_.size() << " leading chars)" << std::endl;
  return true;
}

// Both tables are read on every candidate list, so they are held in memory and
// the database is only ever the backing store.
void UserDb::load() {
  sqlite3_stmt *stmt = nullptr;

  if (sqlite3_prepare_v2(db_, "SELECT ch, n FROM char_freq", -1, &stmt, 0) ==
      SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const unsigned char *ch = sqlite3_column_text(stmt, 0);
      if (ch)
        chars_[reinterpret_cast<const char *>(ch)] =
            sqlite3_column_int(stmt, 1);
    }
  }
  sqlite3_finalize(stmt);

  stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT prev, next, n FROM pair_freq", -1, &stmt,
                         0) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const unsigned char *prev = sqlite3_column_text(stmt, 0);
      const unsigned char *next = sqlite3_column_text(stmt, 1);
      if (prev && next)
        pairs_[reinterpret_cast<const char *>(prev)]
              [reinterpret_cast<const char *>(next)] =
            sqlite3_column_int(stmt, 2);
    }
  }
  sqlite3_finalize(stmt);
}

void UserDb::addChar(const std::string &ch) {
  if (!db_)
    return;

  const int n = ++chars_[ch];

  // INSERT OR REPLACE rather than an UPSERT: the new count is already known
  // here, and this works on every SQLite version.
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "INSERT OR REPLACE INTO char_freq (ch, n) VALUES (?, ?)",
                         -1, &stmt, 0) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, ch.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, n);
    sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
}

void UserDb::addPair(const std::string &prev, const std::string &next) {
  if (!db_)
    return;

  const int n = ++pairs_[prev][next];

  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "INSERT OR REPLACE INTO pair_freq (prev, next, n) VALUES (?, ?, ?)",
          -1, &stmt, 0) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, prev.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, next.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, n);
    sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
}

int UserDb::charCount(const std::string &ch) const {
  auto it = chars_.find(ch);
  return it == chars_.end() ? 0 : it->second;
}

std::vector<std::string> UserDb::followers(const std::string &prev) const {
  std::vector<std::string> result;

  auto it = pairs_.find(prev);
  if (it == pairs_.end())
    return result;

  std::vector<std::pair<std::string, int>> hits;
  for (const auto &entry : it->second) {
    if (entry.second >= kMinCount)
      hits.push_back(entry);
  }

  // The character breaks ties so the list does not shuffle between runs -
  // an unordered_map has no order of its own to fall back on.
  std::sort(hits.begin(), hits.end(),
            [](const std::pair<std::string, int> &a,
               const std::pair<std::string, int> &b) {
              return a.second != b.second ? a.second > b.second
                                          : a.first < b.first;
            });

  result.reserve(hits.size());
  for (const auto &hit : hits)
    result.push_back(hit.first);
  return result;
}
