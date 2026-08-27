#pragma once

#include <sqlite3.h>
#include <string>
#include <unordered_map>
#include <vector>

// What the user has actually typed, kept in its own database so the shipped
// dataset.db is never written to (it is installed read-only under
// /usr/share on a root install).
//
// Two tables, both tiny enough to keep in memory and write through on every
// commit: how often each character was typed, and how often one character was
// typed straight after another.
class UserDb {
public:
  UserDb();
  ~UserDb();

  // Creates the file and the tables if they are not there yet.
  bool init(const std::string &path);
  bool ready() const { return db_ != nullptr; }

  void addChar(const std::string &ch);
  void addPair(const std::string &prev, const std::string &next);

  int charCount(const std::string &ch) const;

  // Characters typed right after `prev` at least kMinCount times, most
  // frequent first.
  std::vector<std::string> followers(const std::string &prev) const;

  // Nothing is promoted until it has been typed this often, so one stray
  // selection never reshuffles a candidate list.
  static constexpr int kMinCount = 2;

private:
  void exec(const char *sql);
  void load();

  sqlite3 *db_ = nullptr;
  std::unordered_map<std::string, int> chars_;
  std::unordered_map<std::string, std::unordered_map<std::string, int>> pairs_;
};
