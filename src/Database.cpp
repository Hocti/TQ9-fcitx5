#include "Database.h"
#include <iostream>

Database::Database() {}

Database::~Database() {
  if (db) {
    sqlite3_close(db);
  }
}

bool Database::init(const std::string &dbPath) {
  std::cerr << "[Database] init: opening '" << dbPath << "'" << std::endl;

  // Check if file exists first (sqlite3_open creates empty db if not exists)
  FILE *f = fopen(dbPath.c_str(), "r");
  if (!f) {
    std::cerr << "[Database] init: ERROR - file does not exist: " << dbPath
              << std::endl;
    return false;
  }
  fclose(f);

  if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
    std::cerr << "[Database] init: Can't open database: " << sqlite3_errmsg(db)
              << std::endl;
    return false;
  }

  // Verify table exists
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM mapped_table", -1, &stmt,
                         0) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      int count = sqlite3_column_int(stmt, 0);
      std::cerr << "[Database] init: SUCCESS - mapped_table has " << count
                << " rows" << std::endl;
    }
    sqlite3_finalize(stmt);
  } else {
    std::cerr << "[Database] init: ERROR - mapped_table not found: "
              << sqlite3_errmsg(db) << std::endl;
    return false;
  }

  return true;
}

std::vector<std::string> Database::getWords(int key) {
  std::vector<std::string> results;

  // Debug: Check if db is null
  if (!db) {
    std::cerr << "[Database] getWords: ERROR - db is null!" << std::endl;
    return results;
  }

  // Debug: First, let's check if the table exists and has data
  sqlite3_stmt *testStmt;
  std::string testSql = "SELECT COUNT(*) FROM mapped_table";
  if (sqlite3_prepare_v2(db, testSql.c_str(), -1, &testStmt, 0) == SQLITE_OK) {
    if (sqlite3_step(testStmt) == SQLITE_ROW) {
      int count = sqlite3_column_int(testStmt, 0);
      std::cerr << "[Database] getWords: mapped_table has " << count << " rows"
                << std::endl;
    } else {
      std::cerr << "[Database] getWords: COUNT query returned no rows"
                << std::endl;
    }
  } else {
    std::cerr << "[Database] getWords: Failed to prepare COUNT query: "
              << sqlite3_errmsg(db) << std::endl;
  }
  sqlite3_finalize(testStmt);

  sqlite3_stmt *stmt;
  // Note: mapped_table.id is unique, but user prompt implied word_code logic.
  // Q9Core.cs: "SELECT characters FROM mapped_table WHERE id='{key}'"
  std::string sql = "SELECT characters FROM mapped_table WHERE id = ?";

  std::cerr << "[Database] getWords: querying key=" << key << std::endl;

  int prepareResult = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0);
  if (prepareResult == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, key);
    int stepResult = sqlite3_step(stmt);
    std::cerr << "[Database] getWords: step result=" << stepResult
              << " (SQLITE_ROW=" << SQLITE_ROW
              << ", SQLITE_DONE=" << SQLITE_DONE << ")" << std::endl;
    if (stepResult == SQLITE_ROW) {
      const unsigned char *text = sqlite3_column_text(stmt, 0);
      if (text) {
        std::string rawText(reinterpret_cast<const char *>(text));
        std::cerr << "[Database] getWords: raw text='" << rawText << "'"
                  << std::endl;
        // The characters string needs to be split.
        // C# code: splits by text elements (unicode chars).
        // We'll trust our splitUTF8 helper or just return as one string
        // and let Logic split it?
        // Q9Core.cs `sql2strs` splits it into array of chars.
        results = splitUTF8(reinterpret_cast<const char *>(text));
        std::cerr << "[Database] getWords: split into " << results.size()
                  << " chars" << std::endl;
      } else {
        std::cerr << "[Database] getWords: text is NULL" << std::endl;
      }
    } else {
      std::cerr << "[Database] getWords: no row found for key=" << key
                << std::endl;
    }
  } else {
    std::cerr << "[Database] getWords: prepare failed: " << sqlite3_errmsg(db)
              << std::endl;
  }
  sqlite3_finalize(stmt);
  return results;
}

std::vector<std::string> Database::getRelate(const std::string &word) {
  std::vector<std::string> results;
  sqlite3_stmt *stmt;
  // Q9Core.cs: "SELECT candidates FROM related_candidates_table WHERE
  // character='{word}'" And it passed " " as splitChar.
  std::string sql =
      "SELECT candidates FROM related_candidates_table WHERE character = ?";

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      const unsigned char *text = sqlite3_column_text(stmt, 0);
      if (text) {
        // Split by space? Or just splitUTF8?
        // C# calls `sql2strs(..., " ")`. So it expects space separated?
        // If the DB stores "A B C", we need to split by space.
        // We'll assume space separation for candidates.
        // But wait, C# `sql2strs` with splitStr=" " just returns `str.Split('
        // ')` (implied). Let's check `sql2strs` in Q9Core.cs again. It's
        // `return str.` (truncated in ViewFile) - wait, it was truncated! Line
        // 43: `return str.` ... I missed that! But for `keyInput` it passed
        // default `""`, which meant split by chars. For `getRelate` it passed
        // `" "`. So candidates are space separated.
        std::string s(reinterpret_cast<const char *>(text));
        size_t pos = 0;
        while ((pos = s.find(' ')) != std::string::npos) {
          if (pos > 0)
            results.push_back(s.substr(0, pos));
          s.erase(0, pos + 1);
        }
        if (!s.empty())
          results.push_back(s);
      }
    }
  }
  sqlite3_finalize(stmt);
  return results;
}

std::vector<std::string> Database::getHomo(const std::string &word) {
  std::vector<std::string> results;
  sqlite3_stmt *stmt;
  // Q9Core.cs: complex query
  std::string sql = "SELECT w1.char FROM word_meta w1 INNER JOIN word_meta w2 "
                    "ON w1.ping = w2.ping WHERE w2.char = ? ORDER BY CASE WHEN "
                    "w1.ping2 = w2.ping2 THEN 0 ELSE 1 END ASC;";

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_STATIC);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const unsigned char *text = sqlite3_column_text(stmt, 0);
      if (text) {
        results.push_back(std::string(reinterpret_cast<const char *>(text)));
      }
    }
  }
  sqlite3_finalize(stmt);
  return results;
}

std::string Database::tcsc(const std::string &input) {
  // This is expensive if we query for every char. C# does loop.
  std::string output;
  sqlite3_stmt *stmt;
  std::string sql =
      "SELECT simplified FROM ts_chinese_table WHERE traditional = ? LIMIT 1";

  // We need to iterate utf8 chars in input.
  std::vector<std::string> chars = splitUTF8(input);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return input; // Fallback
  }

  for (const auto &c : chars) {
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, c.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      const unsigned char *text = sqlite3_column_text(stmt, 0);
      if (text)
        output += std::string(reinterpret_cast<const char *>(text));
    } else {
      output += c;
    }
  }
  sqlite3_finalize(stmt);
  return output;
}

std::vector<int> Database::getCode(const std::string &word) {
  std::vector<int> results;
  sqlite3_stmt *stmt;
  // Q9Core.cs: "SELECT `id` FROM `mapped_table` WHERE
  // INSTR(`characters`,'{word}');" INSTR returns position (1-based) if found, 0
  // if not found. Non-zero is truthy.
  std::string sql =
      "SELECT id FROM mapped_table WHERE INSTR(characters, ?) > 0";

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_STATIC);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      int id = sqlite3_column_int(stmt, 0);
      results.push_back(id);
    }
  }
  sqlite3_finalize(stmt);
  return results;
}

// Simple UTF-8 splitter helper
std::vector<std::string> Database::splitUTF8(const std::string &str) {
  std::vector<std::string> res;
  size_t i = 0;
  while (i < str.size()) {
    int c = str[i];
    int len = 0;
    if ((c & 0x80) == 0)
      len = 1;
    else if ((c & 0xE0) == 0xC0)
      len = 2;
    else if ((c & 0xF0) == 0xE0)
      len = 3;
    else if ((c & 0xF8) == 0xF0)
      len = 4;
    else
      len = 1; // Invalid, skip 1

    if (i + len > str.size())
      len = str.size() - i;
    res.push_back(str.substr(i, len));
    i += len;
  }
  return res;
}
