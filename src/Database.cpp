#include "Database.h"
#include <iostream>

Database::Database() {}

Database::~Database() {
    if (db) {
        sqlite3_close(db);
    }
}

bool Database::init(const std::string& dbPath) {
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    createMockData();
    return true;
}

void Database::createMockData() {
    char* zErrMsg = 0;
    std::string sql = "CREATE TABLE IF NOT EXISTS emoji (id INTEGER PRIMARY KEY AUTOINCREMENT, content TEXT);"
                      "DELETE FROM emoji;" // Clear existing for mock purpose
                      "INSERT INTO emoji (content) VALUES ('😀');"
                      "INSERT INTO emoji (content) VALUES ('😁');"
                      "INSERT INTO emoji (content) VALUES ('😂');"
                      "INSERT INTO emoji (content) VALUES ('🤣');"
                      "INSERT INTO emoji (content) VALUES ('😃');"
                      "INSERT INTO emoji (content) VALUES ('😄');"
                      "INSERT INTO emoji (content) VALUES ('😅');"
                      "INSERT INTO emoji (content) VALUES ('😆');"
                      "INSERT INTO emoji (content) VALUES ('😉');"
                      "INSERT INTO emoji (content) VALUES ('😊');";

    if (sqlite3_exec(db, sql.c_str(), 0, 0, &zErrMsg) != SQLITE_OK) {
        std::cerr << "SQL error: " << zErrMsg << std::endl;
        sqlite3_free(zErrMsg);
    }
}

std::string Database::getRandomEmoji() {
    std::string result = "❓";
    sqlite3_stmt* stmt;
    std::string sql = "SELECT content FROM emoji ORDER BY RANDOM() LIMIT 1";

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}
