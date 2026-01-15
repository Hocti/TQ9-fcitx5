#pragma once

#include "Database.h"
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
};

class Q9Logic {
public:
  Q9Logic();
  ~Q9Logic();

  bool init(const std::string &dbPath);

  // Returns true if state changed and UI needs update
  bool processKey(int key); // 0-9 for now, extended later

  // Extended input for generic handling
  bool processCommand(Q9Key cmd);
  void reset();

  Q9State getState() const;
  std::string getCommitString() const; // If logic decides to commit
  bool hasCommitString() const;
  void clearCommitString();

private:
  Database db;
  Q9State m_state;
  std::string m_commitString;

  void updateCandidates();
  void updatePage();
  void selectWord(int index);
  void cancel(bool cleanRelate = true);
  void startSelectWord(const std::vector<std::string> &words);
  void addPage(int delta);
};
