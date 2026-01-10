#include "Database.h"
#include <cassert>
#include <iostream>

int main() {
  Database db;
  std::cout << "Initializing DB..." << std::endl;
  if (db.init("test_emoji.db")) {
    std::cout << "DB Init Success" << std::endl;
    for (int i = 0; i < 5; ++i) {
      std::string emoji = db.getRandomEmoji();
      std::cout << "Random Emoji [" << i << "]: " << emoji << std::endl;
      if (emoji == "❓") {
        std::cerr << "Warning: Got fallback emoji!" << std::endl;
      }
    }
  } else {
    std::cerr << "DB Init Failed" << std::endl;
    return 1;
  }
  return 0;
}
