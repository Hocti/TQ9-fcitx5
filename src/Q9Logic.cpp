#include "Q9Logic.h"
#include <iostream>

Q9Logic::Q9Logic() {}

Q9Logic::~Q9Logic() {}

bool Q9Logic::init(const std::string &dbPath) { return db.init(dbPath); }

void Q9Logic::clearCommitString() { m_commitString.clear(); }

bool Q9Logic::hasCommitString() const { return !m_commitString.empty(); }

std::string Q9Logic::getCommitString() const { return m_commitString; }

Q9State Q9Logic::getState() const { return m_state; }

void Q9Logic::reset() {
  m_state = Q9State();
  m_commitString.clear();
}

// Cancel and reset state - mirrors C# cancel(bool cleanRelate)
void Q9Logic::cancel(bool cleanRelate) {
  m_state.candidateMode = false;
  m_state.homoMode = false;
  m_state.afterHomoMode = false;
  m_state.openCloseMode = false;
  m_state.shortcutMode = false;
  m_state.inputCode = "";
  m_state.page = 0;
  m_state.totalPages = 0;
  m_state.candidates.clear();
  m_state.pageCandidates.clear();
  m_state.statusPrefix = "";
  m_state.imageType = 0;

  if (cleanRelate) {
    m_state.relatedWords.clear();
  }
}

// Start candidate selection mode - mirrors C# startSelectWord()
void Q9Logic::startSelectWord(const std::vector<std::string> &words) {
  if (words.empty())
    return;

  m_state.candidates = words;
  m_state.totalPages = (words.size() + 8) / 9; // ceil(size/9)
  m_state.candidateMode = true;
  m_state.inputCode = "";
  m_state.imageType = -1; // Signal to show text, not images
  m_state.page = 0;
  updatePage();
}

// Page navigation - mirrors C# addPage()
void Q9Logic::addPage(int delta) {
  if (m_state.candidates.empty())
    return;

  int newPage = m_state.page + delta;
  if (newPage < 0) {
    newPage = m_state.totalPages - 1; // Wrap to last
  } else if (newPage >= m_state.totalPages) {
    newPage = 0; // Wrap to first
  }
  m_state.page = newPage;
  updatePage();
}

// Main key handler - mirrors C# pressKey(int inputInt)
bool Q9Logic::processKey(int key) {
  if (key < 0 || key > 9)
    return false;

  if (m_state.candidateMode) {
    // In selection mode
    if (key == 0) {
      // Next page
      addPage(1);
      return true;
    } else {
      // Select candidate at position (key-1)
      selectWord(key - 1);
      return true;
    }
  } else {
    // Input mode - accumulate code
    m_state.inputCode += std::to_string(key);
    m_state.statusPrefix = m_state.inputCode;

    if (key == 0) {
      // Key 0 ends input early - query database with current code
      int code = 0;
      try {
        code = std::stoi(m_state.inputCode);
      } catch (...) {
        cancel();
        return true;
      }

      std::vector<std::string> words = db.getWords(code);
      if (!words.empty()) {
        startSelectWord(words);
      } else {
        cancel();
      }
      return true;
    } else {
      // Key 1-9
      size_t codeLen = m_state.inputCode.length();

      if (codeLen == 3) {
        // Full 3-digit code - query and show candidates
        int code = std::stoi(m_state.inputCode);
        std::vector<std::string> words = db.getWords(code);
        if (!words.empty()) {
          startSelectWord(words);
        } else {
          cancel();
        }
      } else if (codeLen == 1) {
        // First digit - show second-level images
        m_state.imageType = key; // 1-9
      } else if (codeLen == 2) {
        // Second digit - show third-level images (semi-transparent in UI)
        m_state.imageType = 10;
      }
      return true;
    }
  }

  return false;
}

void Q9Logic::updatePage() {
  m_state.pageCandidates.clear();
  if (m_state.candidates.empty())
    return;

  size_t start = m_state.page * 9;
  for (size_t i = 0; i < 9; ++i) {
    if (start + i < m_state.candidates.size()) {
      m_state.pageCandidates.push_back(m_state.candidates[start + i]);
    }
  }
  m_state.hasCandidates = !m_state.pageCandidates.empty();
}

bool Q9Logic::processCommand(Q9Key cmd) {
  switch (cmd) {
  case Q9Key::Cancel:
    cancel();
    return true;

  case Q9Key::Homo:
    // Toggle homo mode - next selection will query homophones
    m_state.homoMode = !m_state.homoMode;
    return true;

  case Q9Key::Relate:
    // Show related characters for last word
    if (!m_state.lastWord.empty()) {
      m_state.homoMode = false;
      m_state.statusPrefix = "[" + m_state.lastWord + "]關聯";
      std::vector<std::string> relates = db.getRelate(m_state.lastWord);
      if (!relates.empty()) {
        startSelectWord(relates);
      }
    }
    return true;

  case Q9Key::OpenClose: {
    // Bracket pairs - query code=1 and show pairs
    m_state.homoMode = false;
    m_state.afterHomoMode = false;
    m_state.openCloseMode = true;
    m_state.statusPrefix = "「」";

    std::vector<std::string> allChars = db.getWords(1);
    if (!allChars.empty()) {
      // Combine chars into pairs (every 2 chars)
      std::string combined;
      for (const auto &c : allChars) {
        combined += c;
      }
      // Split into pairs of 2 characters
      std::vector<std::string> pairs;
      size_t i = 0;
      while (i < combined.length()) {
        // Get one UTF-8 character
        int c = (unsigned char)combined[i];
        int len1 = 1;
        if ((c & 0x80) == 0)
          len1 = 1;
        else if ((c & 0xE0) == 0xC0)
          len1 = 2;
        else if ((c & 0xF0) == 0xE0)
          len1 = 3;
        else if ((c & 0xF8) == 0xF0)
          len1 = 4;

        if (i + len1 >= combined.length())
          break;

        // Get second UTF-8 character
        c = (unsigned char)combined[i + len1];
        int len2 = 1;
        if ((c & 0x80) == 0)
          len2 = 1;
        else if ((c & 0xE0) == 0xC0)
          len2 = 2;
        else if ((c & 0xF0) == 0xE0)
          len2 = 3;
        else if ((c & 0xF8) == 0xF0)
          len2 = 4;

        pairs.push_back(combined.substr(i, len1 + len2));
        i += len1 + len2;
      }
      startSelectWord(pairs);
    }
    return true;
  }

  case Q9Key::Shortcut:
    // Quick selection shortcuts
    if (!m_state.candidateMode) {
      if (m_state.inputCode.empty()) {
        // Show general shortcuts (code 1000)
        m_state.statusPrefix = "速選";
        std::vector<std::string> words = db.getWords(1000);
        if (!words.empty()) {
          m_state.shortcutMode = true;
          startSelectWord(words);
        }
      } else if (m_state.inputCode.length() == 1) {
        // Show category shortcuts (code 1001-1009)
        int digit = m_state.inputCode[0] - '0';
        m_state.statusPrefix = "速選" + m_state.inputCode;
        std::vector<std::string> words = db.getWords(1000 + digit);
        if (!words.empty()) {
          m_state.shortcutMode = true;
          startSelectWord(words);
        }
      }
    }
    return true;

  case Q9Key::PrevPage:
    if (m_state.candidateMode) {
      addPage(-1);
    }
    return true;

  case Q9Key::NextPage:
    if (m_state.candidateMode) {
      addPage(1);
    }
    return true;

  default:
    return false;
  }
}

// Select word at index - mirrors C# selectWord(int inputInt)
void Q9Logic::selectWord(int index) {
  if (index < 0 || index >= (int)m_state.pageCandidates.size())
    return;

  std::string selectedWord = m_state.pageCandidates[index];

  if (m_state.homoMode) {
    // Query homophones for this word, stay in selection mode
    m_state.homoMode = false;
    m_state.afterHomoMode = true;
    m_state.statusPrefix = "同音[" + selectedWord + "]";
    std::vector<std::string> homos = db.getHomo(selectedWord);
    if (!homos.empty()) {
      startSelectWord(homos);
    }
    return;
  }

  if (m_state.openCloseMode) {
    // Bracket pair selected - commit with special marker for cursor positioning
    m_state.openCloseMode = false;
    // The UI/engine should handle positioning cursor between brackets
    // We commit the pair and the engine inserts + moves cursor left
    m_commitString = selectedWord;
    cancel();
    return;
  }

  // Normal selection - commit word
  m_commitString = selectedWord;

  // Store for relate feature (single character only)
  // UTF-8: typical CJK char is 3 bytes
  if (selectedWord.length() <= 4) {
    m_state.lastWord = selectedWord;
  } else {
    m_state.lastWord = "";
  }

  // Query related words for display
  std::vector<std::string> relates;
  if (!m_state.lastWord.empty()) {
    relates = db.getRelate(m_state.lastWord);
  }

  // Show key code if coming from homo mode
  if (m_state.afterHomoMode) {
    m_state.afterHomoMode = false;
    std::vector<int> codes = db.getCode(selectedWord);
    if (!codes.empty()) {
      std::string codesStr;
      for (size_t i = 0; i < codes.size() && i < 5; ++i) {
        if (i > 0)
          codesStr += ",";
        codesStr += std::to_string(codes[i]);
      }
      m_state.statusPrefix = selectedWord + "key:" + codesStr;
    }
  }

  // Reset state but keep related words
  if (!relates.empty()) {
    m_state.relatedWords = relates;
    cancel(false);
  } else {
    cancel(true);
  }
}

// Legacy - not used, keeping for compatibility
void Q9Logic::updateCandidates() { updatePage(); }
