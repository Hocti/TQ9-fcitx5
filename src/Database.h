#pragma once

#include <sqlite3.h>
#include <string>
#include <unordered_map>
#include <vector>

class Database {
public:
  Database();
  ~Database();

  bool init(const std::string &dbPath);

  // Core Q9 Logic Queries
  std::vector<std::string> getWords(int key);
  std::vector<std::string> getRelate(const std::string &word);

  // 同音字, 先同音尾近音: the exact homophones first, then the ones that only
  // match once both pronunciations are run through fuzzyPing().
  std::vector<std::string> getHomo(const std::string &word);
  std::vector<std::string> exactHomo(const std::string &word);
  std::vector<std::string> nearHomo(const std::string &word,
                                    const std::vector<std::string> &skip);

  std::string tcsc(const std::string &input);

  // Reverse lookup for "Find Code" feature (TODO if needed)
  std::vector<int> getCode(const std::string &word);

  // 懶音: what a `word_meta.ping` collapses to. Two pings that collapse to the
  // same string count as near-homophones. This works on the romanisation
  // alone - there is no per-character table anywhere.
  static std::string fuzzyPing(const std::string &raw);

private:
  sqlite3 *db = nullptr;
  // Helper to split string by delimiter (if needed)
  std::vector<std::string> splitUTF8(const std::string &str);

  std::vector<std::string> pingsOf(const std::string &word);

  // fuzzyPing(ping) -> every ping in that group. There are only ~700 pings in
  // the whole table, so it is read once and kept.
  const std::unordered_map<std::string, std::vector<std::string>> &
  fuzzyGroups();
  std::unordered_map<std::string, std::vector<std::string>> fuzzy_;
  bool fuzzyLoaded_ = false;
};
