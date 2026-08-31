#include "Database.h"
#include <cctype>
#include <iostream>
#include <unordered_set>

namespace {

// Exactly one UTF-8 character. 同音 only ever makes sense for a single one.
bool isSingleChar(const std::string &s) {
  if (s.empty())
    return false;
  const unsigned char c = static_cast<unsigned char>(s[0]);
  size_t len = 1;
  if ((c & 0x80) == 0)
    len = 1;
  else if ((c & 0xE0) == 0xC0)
    len = 2;
  else if ((c & 0xF0) == 0xE0)
    len = 3;
  else if ((c & 0xF8) == 0xF0)
    len = 4;
  return len == s.size();
}

void replaceAll(std::string &s, const std::string &from, const std::string &to) {
  for (size_t at = s.find(from); at != std::string::npos;
       at = s.find(from, at + to.size()))
    s.replace(at, from.size(), to);
}

bool startsWith(const std::string &s, const char *prefix) {
  return s.rfind(prefix, 0) == 0;
}

bool endsWith(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

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

  return true;
}

std::vector<std::string> Database::getWords(int key) {
  std::vector<std::string> results;

  sqlite3_stmt *stmt;
  // Note: mapped_table.id is unique, but user prompt implied word_code logic.
  // Q9Core.cs: "SELECT characters FROM mapped_table WHERE id='{key}'"
  std::string sql = "SELECT characters FROM mapped_table WHERE id = ?";

  int prepareResult = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0);
  if (prepareResult == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, key);
    int stepResult = sqlite3_step(stmt);
    if (stepResult == SQLITE_ROW) {
      const unsigned char *text = sqlite3_column_text(stmt, 0);
      if (text) {
        std::string rawText(reinterpret_cast<const char *>(text));
        // The characters string needs to be split.
        // C# code: splits by text elements (unicode chars).
        // We'll trust our splitUTF8 helper or just return as one string
        // and let Logic split it?
        // Q9Core.cs `sql2strs` splits it into array of chars.
        results = splitUTF8(reinterpret_cast<const char *>(text));
      } else {
        // std::cerr << "[Database] getWords: text is NULL" << std::endl;
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
  std::vector<std::string> results = exactHomo(word);
  const std::vector<std::string> near = nearHomo(word, results);
  results.insert(results.end(), near.begin(), near.end());
  return results;
}

// 同音字: the rows whose `ping` is identical, same tone first. This is the list
// the 同音 key has always shown.
std::vector<std::string> Database::exactHomo(const std::string &word) {
  std::vector<std::string> results;
  if (!isSingleChar(word))
    return results;

  sqlite3_stmt *stmt;
  // Q9Core.cs: complex query
  std::string sql = "SELECT w1.char FROM word_meta w1 INNER JOIN word_meta w2 "
                    "ON w1.ping = w2.ping WHERE w2.char = ? ORDER BY CASE WHEN "
                    "w1.ping2 = w2.ping2 THEN 0 ELSE 1 END ASC;";

  std::unordered_set<std::string> seen;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_STATIC);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const unsigned char *text = sqlite3_column_text(stmt, 0);
      if (!text)
        continue;
      std::string s(reinterpret_cast<const char *>(text));
      // The same character can hold several rows (one per code), and the join
      // multiplies them out again.
      if (seen.insert(s).second)
        results.push_back(s);
    }
  }
  sqlite3_finalize(stmt);
  return results;
}

// 懶音字: the characters whose `ping` only matches once both sides have been
// through fuzzyPing() - 我 (`ngo`) reaching the `o` and `a` characters, 發
// (`faat`) reaching `fat`, and so on. Always appended after the exact
// homophones, so the characters the user already picks never get pushed off
// the first page.
//
// `skip` is what exactHomo() has already returned. A dataset without `ping`
// simply yields nothing here, leaving 同音 exactly as it was.
std::vector<std::string> Database::nearHomo(const std::string &word,
                                            const std::vector<std::string> &skip) {
  std::vector<std::string> results;
  if (!isSingleChar(word))
    return results;

  const std::vector<std::string> own = pingsOf(word);
  if (own.empty())
    return results;

  // The character's own pings are exactHomo()'s business, not ours.
  const std::unordered_set<std::string> mine(own.begin(), own.end());
  std::vector<std::string> want;
  std::unordered_set<std::string> queued;
  for (const auto &ping : own) {
    const auto group = fuzzyGroups().find(fuzzyPing(ping));
    if (group == fuzzyGroups().end())
      continue;
    for (const auto &other : group->second) {
      if (mine.count(other) == 0 && queued.insert(other).second)
        want.push_back(other);
    }
  }
  if (want.empty())
    return results;

  std::string sql = "SELECT char FROM word_meta WHERE ping IN (";
  for (size_t i = 0; i < want.size(); ++i)
    sql += i == 0 ? "?" : ",?";
  sql += ") AND char <> '' GROUP BY char ORDER BY MAX(freq) DESC";

  std::unordered_set<std::string> seen(skip.begin(), skip.end());
  seen.insert(word);

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
    for (size_t i = 0; i < want.size(); ++i)
      sqlite3_bind_text(stmt, static_cast<int>(i + 1), want[i].c_str(), -1,
                        SQLITE_STATIC);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const unsigned char *text = sqlite3_column_text(stmt, 0);
      if (!text)
        continue;
      std::string s(reinterpret_cast<const char *>(text));
      if (seen.insert(s).second)
        results.push_back(s);
    }
  } else {
    std::cerr << "[Database] nearHomo: prepare failed: " << sqlite3_errmsg(db)
              << std::endl;
  }
  sqlite3_finalize(stmt);
  return results;
}

std::vector<std::string> Database::pingsOf(const std::string &word) {
  std::vector<std::string> results;
  sqlite3_stmt *stmt;
  std::string sql = "SELECT DISTINCT ping FROM word_meta WHERE char = ? AND "
                    "ping IS NOT NULL AND ping <> ''";

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_STATIC);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const unsigned char *text = sqlite3_column_text(stmt, 0);
      if (text)
        results.push_back(std::string(reinterpret_cast<const char *>(text)));
    }
  }
  sqlite3_finalize(stmt);
  return results;
}

const std::unordered_map<std::string, std::vector<std::string>> &
Database::fuzzyGroups() {
  if (fuzzyLoaded_)
    return fuzzy_;
  fuzzyLoaded_ = true; // one attempt, even if the column is not there

  sqlite3_stmt *stmt;
  std::string sql = "SELECT DISTINCT ping FROM word_meta WHERE ping IS NOT "
                    "NULL AND ping <> ''";

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const unsigned char *text = sqlite3_column_text(stmt, 0);
      if (!text)
        continue;
      std::string ping(reinterpret_cast<const char *>(text));
      fuzzy_[fuzzyPing(ping)].push_back(ping);
    }
  } else {
    std::cerr << "[Database] fuzzyGroups: prepare failed: "
              << sqlite3_errmsg(db) << std::endl;
  }
  sqlite3_finalize(stmt);
  std::cerr << "[Database] 懶音 groups: " << fuzzy_.size() << std::endl;
  return fuzzy_;
}

// A `ping` reduced to what the 懶音 rules cannot tell apart. Purely a string
// transform - which characters exist never enters into it, so a rule can be
// added or dropped without touching the word table.
//
// 1. First put both romanisations on the same footing. `word_meta.ping` is
//    mostly Yale (`ji`, `yi`, `cheui`) with a few Jyutping strays (`zi`, `ci`,
//    `ceoi`, `coek`); without this those would not even match as exact
//    homophones. `z-`->`j-`, `c-`->`ch-`, `eoi/eon/eot`->`eui/eun/eut`,
//    `oe`->`eu`.
// 2. Then the lazy-sound rules proper: a dropped `ng-`, `n-`/`l-`, `gw-`/`g-`
//    and `kw-`/`k-`, `aa`/`a`, `-ng`/`-n`, `-k`/`-t`.
//
// Bare `ng` and `m` (五, 唔) are syllabic nasals, not initials, so stripping
// them would leave nothing behind - they are grouped by the table at the end
// instead, together with `o`/`a` so that 我 (`ngo` -> `o`) reaches 啊 (`a`).
std::string Database::fuzzyPing(const std::string &raw) {
  std::string s;
  s.reserve(raw.size());
  for (char c : raw) {
    if (!std::isspace(static_cast<unsigned char>(c)))
      s += static_cast<char>(
          std::tolower(static_cast<unsigned char>(c)));
  }
  if (s.empty())
    return s;

  // ---- 1. romanisation (Jyutping -> Yale) ----
  if (s[0] == 'z')
    s = "j" + s.substr(1);
  else if (s[0] == 'c' && !startsWith(s, "ch"))
    s = "ch" + s.substr(1);
  replaceAll(s, "eoi", "eui");
  replaceAll(s, "eon", "eun");
  replaceAll(s, "eot", "eut");
  replaceAll(s, "oe", "eu");

  // ---- 2. initials ----
  if (startsWith(s, "ng") && s.size() > 2)
    s = s.substr(2); // ngo -> o
  else if (s != "ng" && s[0] == 'n' && s.size() > 1)
    s = "l" + s.substr(1);
  if (startsWith(s, "gw"))
    s = "g" + s.substr(2);
  else if (startsWith(s, "kw"))
    s = "k" + s.substr(2);

  // ---- 3. finals ----
  replaceAll(s, "aa", "a");
  if (endsWith(s, "ng"))
    s = s.substr(0, s.size() - 2) + "n";
  if (endsWith(s, "k"))
    s[s.size() - 1] = 't';

  // Groups the rules above cannot reach. The value must not itself be another
  // key - this is looked up once, not followed as a chain.
  if (s == "o")
    return "a"; // 我 (ngo -> o) has to find 啊 (a)
  if (s == "n")
    return "m"; // syllabic nasals: 五 (ng -> n) and 唔 (m)
  return s;
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
