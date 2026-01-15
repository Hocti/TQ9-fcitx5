#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>

class Database {
public:
  Database();
  ~Database();

  bool init(const std::string &dbPath);

  // Core Q9 Logic Queries
  std::vector<std::string> getWords(int key);
  std::vector<std::string> getRelate(const std::string &word);
  std::vector<std::string> getHomo(const std::string &word);
  std::string tcsc(const std::string &input);

  // Reverse lookup for "Find Code" feature (TODO if needed)
  std::vector<int> getCode(const std::string &word);

private:
  sqlite3 *db = nullptr;
  // Helper to split string by delimiter (if needed)
  std::vector<std::string> splitUTF8(const std::string &str);
};
