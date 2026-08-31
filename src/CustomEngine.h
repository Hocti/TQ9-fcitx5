#pragma once

#include "ConfigLoader.h"
#include "Database.h"
#include "Q9Logic.h"
#include <fcitx-utils/event.h>
#include <fcitx/addonfactory.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <memory>
#include <unordered_map>
#include <vector>

class CustomEngine : public fcitx::InputMethodEngineV2 {
public:
  CustomEngine(fcitx::Instance *instance);
  ~CustomEngine();

  void activate(const fcitx::InputMethodEntry &entry,
                fcitx::InputContextEvent &event) override;
  void deactivate(const fcitx::InputMethodEntry &entry,
                  fcitx::InputContextEvent &event) override;
  void keyEvent(const fcitx::InputMethodEntry &entry,
                fcitx::KeyEvent &keyEvent) override;
  void reset(const fcitx::InputMethodEntry &entry,
             fcitx::InputContextEvent &event) override;

  std::vector<fcitx::InputMethodEntry> listInputMethods() override;

private:
  fcitx::Instance *instance_;

  void spawnUI();
  void sendToUI(const std::string &cmd);
  void handleUIOutput();
  void handleUILine(const std::string &line);
  void updateUIState();

  // ---- key handling ----
  // What one key means to us. Swallow is the non-numpad mode blocking the
  // letter keys so they cannot type through the input method.
  enum class KeyRole { None, Digit, Command, Swallow };
  KeyRole resolveKey(const fcitx::Key &key, int &num, Q9Key &cmd) const;

  // Commit, cursor move and UI refresh for whatever the logic just did. One
  // path for a key, for the deferred release of a held key, and for a click on
  // the floating window. `ic` falls back to the active context.
  void applyLogicResult(bool changed, fcitx::InputContext *ic);
  void applyKey(int num, fcitx::InputContext *ic);
  void applyCommand(Q9Key cmd, fcitx::InputContext *ic);

  // ---- 長按 on the keypad ----
  // What holding a digit would do, decided when the key goes down.
  enum class HoldAction { None, Homo, OpenClose, Shortcut };
  HoldAction holdActionFor(int num) const;
  void armKeyHold(int num, HoldAction action);
  bool fireKeyHold();
  // The key is up, or another key arrived: a press that never became a long
  // one still owes its ordinary action.
  void flushHeldKey(fcitx::InputContext *ic);
  void dropHeldKey();

  // 長按 取消: 錄音 unless the settings say otherwise, and never 錄音 while
  // STT is off.
  CancelHold cancelHoldAction() const;
  bool isCancelKey(const fcitx::Key &key) const;
  void handleCancelHold(fcitx::KeyEvent &keyEvent);

  // ---- config ----
  std::string configPath() const;
  void reloadConfig();
  void applyInputConfig(const InputConfig &cfg);

  // STT
  void sendSurroundingTextToUI();
  void sendSelectionToUI();
  void showSttPlaceholder();
  void showTranslatePlaceholder(bool replace);
  bool hasSelection(fcitx::InputContext *ic) const;
  void waitForSelectionCollapse();
  void clearSttPlaceholder();
  void finishStt(const std::string &text);

  // Logic
  Q9Logic logic_;

  // Config - loaded from UI on INIT response
  bool use_numpad_ = true;
  // 選字 / 長按 settings, re-read on RELOAD_CONFIG.
  InputConfig input_;
  uint64_t keyHoldUsec_ = 350000;
  std::unordered_map<int, int> altKeyToNum_;   // Maps key code -> num (0-9)
  std::unordered_map<int, Q9Key> altKeyToCmd_; // Maps key code -> command

  // UI Process Management
  pid_t uiPid_ = -1;
  int uiStdinFd_ = -1;  // Write to UI
  int uiStdoutFd_ = -1; // Read from UI
  std::unique_ptr<fcitx::EventSource> stdoutSource_;
  std::unique_ptr<fcitx::EventSource> hideTimer_;

  fcitx::InputContext *activeContext_ = nullptr;

  // Track if UI is already in base state (to avoid repeated RESET)
  bool lastUIStateWasBase_ = false;

  // Track if we're waiting for a focus check response
  // Used to prevent race condition where FOCUS_FALSE arrives after
  // re-activation
  bool pendingFocusCheck_ = false;

  // Accumulates partial reads from the UI's stdout until a full line arrives
  std::string uiReadBuffer_;

  // ---- 長按 state ----
  // The one digit key currently down, -1 when none. While it is set the key's
  // ordinary action has not run yet - it waits for the release.
  int heldNum_ = -1;
  HoldAction heldAction_ = HoldAction::None;
  bool heldFired_ = false; // the long press already ran; the release is spent
  std::unique_ptr<fcitx::EventSource> keyHoldTimer_;

  // ---- STT state ----
  // Mirrors the UI's setting; when false 錄音 is not an option for 取消.
  bool sttEnabled_ = false;
  bool cancelKeyDown_ = false;  // guards against X11 auto-repeat
  bool cancelFired_ = false;    // the 取消 long press already ran
  uint64_t sttHoldUsec_ = 500000; // hold before recording, set by the UI
  bool sttRecording_ = false;   // we told the UI to start recording
  std::unique_ptr<fcitx::EventSource> cancelHoldTimer_;

  // How the "waiting for Gemini" marker was shown, so we can take it back
  bool sttPreeditShown_ = false;
  bool sttPlaceholderCommitted_ = false;
  fcitx::InputContext *sttContext_ = nullptr;

  // Waiting for the client to drop the selection before the "insert after"
  // translation placeholder goes in.
  std::unique_ptr<fcitx::EventSource> translateCollapseTimer_;
  int translateCollapseWaits_ = 0;
};

class CustomEngineFactory : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};
