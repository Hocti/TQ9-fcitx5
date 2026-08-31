#pragma once

#include "Database.h"
#include "UserDb.h"
#include <cstdint>
#include <string>
#include <vector>

enum class Q9Key {
  Num0 = 0,
  Num1,
  Num2,
  Num3,
  Num4,
  Num5,
  Num6,
  Num7,
  Num8,
  Num9,
  Cancel,
  Relate,
  Homo,
  Shortcut,  // '-' key when not in select mode (1000/1001-1009)
  OpenClose, // '/' key for bracket pairs
  NextPage,
  PrevPage
};

// Where the list currently on screen came from. Only the two lists the user
// reaches by typing a code count towards the usage statistics; picking off the
// 下個字 list is following a suggestion, not typing a character.
enum class CandidateSource {
  Code,      // db.getWords() - the 選字表 for the code just entered
  Homo,      // db.getHomo() - 同音
  Relate,    // db.getRelate() - the 下個字 list, reached with 關聯
  Shortcut,  // 速選
  OpenClose, // bracket pairs
};

struct Q9State {
  std::string inputCode;
  std::vector<std::string> candidates;
  int page = 0;
  int totalPages = 0;
  bool hasCandidates = false;
  bool candidateMode = false;
  // Which candidate indices correspond to buttons 1-9 on this page
  std::vector<std::string> pageCandidates;

  // Special modes
  bool homoMode = false;
  bool afterHomoMode = false; // Show key code after homo selection
  bool openCloseMode = false;
  bool shortcutMode = false; // In shortcut selection (1000+)
  std::string lastWord;      // For relate feature
  std::string statusPrefix;  // Current status prefix for display
  int imageType = 0; // Which image set to display (0=base, 1-9=second level,
                     // 10=third level, -1=candidates)

  // Related words to display (shown on buttons with images visible)
  std::vector<std::string> relatedWords;

  // Cursor movement request
  bool moveCursorLeft = false;
};

class Q9Logic {
public:
  Q9Logic();
  ~Q9Logic();

  // userDbPath is created on first use; the shipped dataset at dbPath is only
  // ever read.
  bool init(const std::string &dbPath, const std::string &userDbPath);

  // 常用字調前 - whether what the user types is allowed to reorder the lists.
  // Statistics are collected either way, so switching it on takes effect at
  // once instead of starting from nothing.
  void setFrequencyOrder(bool on) { freqOrder_ = on; }
  bool frequencyOrder() const { return freqOrder_; }

  // Returns true if state changed and UI needs update
  bool processKey(int key); // 0-9 for now, extended later

  // Extended input for generic handling
  bool processCommand(Q9Key cmd);
  void reset();

  // The three lists a long press can open. Each returns false, having changed
  // nothing, when there is nothing to show - the caller then treats the press
  // as an ordinary tap rather than leaving the user holding a key for nothing.
  //
  // 同音 for the candidate at `index` on the page now on screen.
  bool showHomoFor(int index);
  // 速選: one of the 1~9 category pages, or the general page for 0.
  bool showShortcutPage(int digit);
  // 下個字 for the character last committed.
  bool showRelated();

  Q9State getState() const;

  // Cheap reads of the bits the engine needs on every keystroke - getState()
  // copies the whole candidate list.
  bool inCandidateMode() const { return m_state.candidateMode; }
  bool hasInputCode() const { return !m_state.inputCode.empty(); }
  bool wantsCursorLeft() const { return m_state.moveCursorLeft; }
  std::string getCommitString() const; // If logic decides to commit
  bool hasCommitString() const;
  void clearCommitString();

private:
  Database db;
  UserDb userDb_;
  Q9State m_state;
  std::string m_commitString;

  bool freqOrder_ = true;
  CandidateSource m_source = CandidateSource::Code;

  // Last character that counted towards the statistics, for the pair table.
  std::string m_prevChar;
  int64_t m_prevCharMs = 0;

  void updateCandidates();
  void updatePage();
  void selectWord(int index);
  void cancel(bool cleanRelate = true);
  void startSelectWord(const std::vector<std::string> &words);
  void addPage(int delta);

  // Usage statistics and the reordering they drive.
  void recordCommit(const std::string &word, bool countable);
  void promoteFrequent(std::vector<std::string> &words) const;
  std::vector<std::string> relatedFor(const std::string &word);
};
