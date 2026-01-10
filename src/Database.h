#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>

class Database {
public:
    Database();
    ~Database();

    bool init(const std::string& dbPath);
    std::string getRandomEmoji();
    
private:
    sqlite3* db = nullptr;
    void createMockData();
};
