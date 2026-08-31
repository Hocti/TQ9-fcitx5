#include "Q9Logic.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <unordered_set>

namespace {

// Candidates per page. The first page is what the reordering must never
// disturb.
constexpr size_t kPageSize = 9;

// Two characters only count as "typed one after the other" within this gap;
// past it the user has moved on and the pair says nothing.
constexpr int64_t kPairMaxGapMs = 5000;

int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// True for exactly one Han character - which is also what rules punctuation
// out, CJK punctuation living outside these blocks.
bool isSingleChineseChar(const std::string &s) {
  const unsigned char *p = reinterpret_cast<const unsigned char *>(s.data());
  const size_t n = s.size();
  uint32_t cp = 0;
  size_t len = 0;

  if (n >= 1 && p[0] < 0x80) {
    cp = p[0];
    len = 1;
  } else if (n >= 2 && (p[0] & 0xE0) == 0xC0) {
    cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    len = 2;
  } else if (n >= 3 && (p[0] & 0xF0) == 0xE0) {
    cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    len = 3;
  } else if (n >= 4 && (p[0] & 0xF8) == 0xF0) {
    cp = ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) |
         ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
    len = 4;
  } else {
    return false;
  }

  if (len != n)
    return false; // more than one character

  return (cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK Unified Ideographs
         (cp >= 0x3400 && cp <= 0x4DBF) ||   // Extension A
         (cp >= 0xF900 && cp <= 0xFAFF) ||   // Compatibility Ideographs
         (cp >= 0x20000 && cp <= 0x2FA1F);   // Extension B and beyond
}

} // namespace

Q9Logic::Q9Logic() {}

Q9Logic::~Q9Logic() {}

bool Q9Logic::init(const std::string &dbPath, const std::string &userDbPath) {
  // The statistics are a convenience: if they cannot be opened the engine
  // still works, it just never reorders anything.
  userDb_.init(userDbPath);
  return db.init(dbPath);
}

void Q9Logic::clearCommitString() { m_commitString.clear(); }

bool Q9Logic::hasCommitString() const { return !m_commitString.empty(); }

std::string Q9Logic::getCommitString() const { return m_commitString; }

Q9State Q9Logic::getState() const { return m_state; }

void Q9Logic::reset() {
  m_state = Q9State();
  m_commitString.clear();
  // Focus moved or the input was thrown away: whatever comes next is not
  // "the character after" the last one.
  m_prevChar.clear();
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
  m_state.moveCursorLeft = false;

  if (cleanRelate) {
    m_state.relatedWords.clear();
  }
}

// Start candidate selection mode - mirrors C# startSelectWord()
void Q9Logic::startSelectWord(const std::vector<std::string> &words) {
  if (words.empty())
    return;

  m_state.candidates = words;
  promoteFrequent(m_state.candidates);
  m_state.totalPages = (m_state.candidates.size() + 8) / 9; // ceil(size/9)
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
        m_source = CandidateSource::Code;
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
          m_source = CandidateSource::Code;
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
    if (m_state.homoMode) {
      m_state.statusPrefix = "[同音]" + m_state.statusPrefix;
    } else {
      std::string target = "[同音]";
      size_t pos = m_state.statusPrefix.find(target);
      if (pos != std::string::npos) {
        m_state.statusPrefix.erase(pos, target.length());
      }
    }
    return true;

  case Q9Key::Relate:
    showRelated();
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
      m_source = CandidateSource::OpenClose;
      startSelectWord(pairs);
    }
    return true;
  }

  case Q9Key::Shortcut:
    // Quick selection shortcuts: the general page when nothing has been typed,
    // that digit's page after a single digit.
    if (!m_state.candidateMode) {
      if (m_state.inputCode.empty())
        showShortcutPage(0);
      else if (m_state.inputCode.length() == 1)
        showShortcutPage(m_state.inputCode[0] - '0');
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

// 同音 for one candidate without committing it - what a long press on 1~9
// does. Same as picking it with 同音 armed, minus the toggle.
bool Q9Logic::showHomoFor(int index) {
  if (index < 0 || index >= (int)m_state.pageCandidates.size())
    return false;

  const std::string word = m_state.pageCandidates[index];
  if (word.empty() || word == "*")
    return false;

  std::vector<std::string> homos = db.getHomo(word);
  if (homos.empty())
    return false;

  m_state.homoMode = false;
  m_state.afterHomoMode = true; // so the 字碼 shows once one is picked
  m_state.statusPrefix = "同音[" + word + "]";
  m_source = CandidateSource::Homo;
  startSelectWord(homos);
  return true;
}

// 速選: 1000 is the general page, 1001-1009 the per-digit ones.
bool Q9Logic::showShortcutPage(int digit) {
  if (digit < 0 || digit > 9)
    return false;

  std::vector<std::string> words = db.getWords(1000 + digit);
  if (words.empty())
    return false;

  m_state.statusPrefix =
      digit == 0 ? "速選" : "速選" + std::to_string(digit);
  m_state.shortcutMode = true;
  m_source = CandidateSource::Shortcut;
  startSelectWord(words);
  return true;
}

// 下個字 for the last committed character.
bool Q9Logic::showRelated() {
  if (m_state.lastWord.empty())
    return false;

  std::vector<std::string> relates = relatedFor(m_state.lastWord);
  if (relates.empty())
    return false;

  m_state.homoMode = false;
  m_state.statusPrefix = "[" + m_state.lastWord + "]關聯";
  m_source = CandidateSource::Relate;
  startSelectWord(relates);
  return true;
}

// Select word at index - mirrors C# selectWord(int inputInt)
void Q9Logic::selectWord(int index) {
  if (index < 0 || index >= (int)m_state.pageCandidates.size())
    return;

  std::string selectedWord = m_state.pageCandidates[index];

  if (selectedWord == "*") {
    return;
  }

  if (m_state.homoMode) {
    // Query homophones for this word, stay in selection mode
    m_state.homoMode = false;
    m_state.afterHomoMode = true;
    m_state.statusPrefix = "同音[" + selectedWord + "]";
    std::vector<std::string> homos = db.getHomo(selectedWord);
    if (!homos.empty()) {
      m_source = CandidateSource::Homo;
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
    recordCommit(selectedWord, false);
    cancel();
    m_state.moveCursorLeft = true;
    return;
  }

  // Normal selection - commit word
  m_commitString = selectedWord;

  // Only the 選字表 and 同音 count: picking off the 下個字 list (or 速選) is
  // taking a suggestion, and feeding that back would just entrench it.
  recordCommit(selectedWord, m_source == CandidateSource::Code ||
                                 m_source == CandidateSource::Homo);

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
    relates = relatedFor(m_state.lastWord);
  }

  std::string nextPrefix;

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
      nextPrefix = selectedWord + ":" + codesStr;
    }
  }

  // Reset state but keep related words
  if (!relates.empty()) {
    m_state.relatedWords = relates;
    cancel(false);
  } else {
    cancel(true);
  }

  if (!nextPrefix.empty()) {
    m_state.statusPrefix = nextPrefix;
    std::cout << "nextPrefix: " << nextPrefix << std::endl;
  }
}

// Legacy - not used, keeping for compatibility
void Q9Logic::updateCandidates() { updatePage(); }

// One committed character, as far as the statistics are concerned. Anything
// that is not a single Han character - punctuation, a bracket pair, a 速選
// phrase - ends the run rather than joining it, so no pair straddles it.
void Q9Logic::recordCommit(const std::string &word, bool countable) {
  if (!userDb_.ready())
    return;

  if (!countable || !isSingleChineseChar(word)) {
    m_prevChar.clear();
    return;
  }

  const int64_t now = nowMs();
  userDb_.addChar(word);
  if (!m_prevChar.empty() && now - m_prevCharMs <= kPairMaxGapMs)
    userDb_.addPair(m_prevChar, word);

  m_prevChar = word;
  m_prevCharMs = now;
}

// Move the characters this user actually types to the front of the second
// page. The first nine keep their order and their keys: those are the ones
// muscle memory knows, and shuffling them would cost more than it saves.
void Q9Logic::promoteFrequent(std::vector<std::string> &words) const {
  if (!freqOrder_ || !userDb_.ready() || words.size() <= kPageSize)
    return;

  // Only the lists a code leads to. The 下個字 list has its own ordering (see
  // relatedFor), and 速選 / bracket pairs are symbol tables with no 常用字 to
  // speak of.
  if (m_source != CandidateSource::Code && m_source != CandidateSource::Homo)
    return;

  std::vector<std::string> hot, rest;
  for (size_t i = kPageSize; i < words.size(); ++i) {
    if (userDb_.charCount(words[i]) >= UserDb::kMinCount)
      hot.push_back(words[i]);
    else
      rest.push_back(words[i]);
  }

  if (hot.empty())
    return;

  // Stable, so equally-used characters stay in the order the dataset had them.
  std::stable_sort(hot.begin(), hot.end(),
                   [this](const std::string &a, const std::string &b) {
                     return userDb_.charCount(a) > userDb_.charCount(b);
                   });

  words.erase(words.begin() + kPageSize, words.end());
  words.insert(words.end(), hot.begin(), hot.end());
  words.insert(words.end(), rest.begin(), rest.end());
}

// The 下個字 list for `word`: the characters this user has typed after it go
// first - straight onto the first page, unlike the 常用字 promotion - and are
// added outright when the shipped list never had them.
std::vector<std::string> Q9Logic::relatedFor(const std::string &word) {
  std::vector<std::string> base = db.getRelate(word);
  if (!freqOrder_ || !userDb_.ready() || word.empty())
    return base;

  std::vector<std::string> merged = userDb_.followers(word);
  std::unordered_set<std::string> seen(merged.begin(), merged.end());
  for (const auto &candidate : base) {
    if (seen.insert(candidate).second)
      merged.push_back(candidate);
  }
  return merged;
}
