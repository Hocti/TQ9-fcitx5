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

  // STT
  bool isCancelKey(const fcitx::Key &key) const;
  bool handleCancelKeyForStt(fcitx::KeyEvent &keyEvent);
  void applyCancelCommand();
  void sendSurroundingTextToUI();
  void sendSelectionToUI();
  void showSttPlaceholder();
  void showTranslatePlaceholder();
  void clearSttPlaceholder();
  void finishStt(const std::string &text);

  // Logic
  Q9Logic logic_;

  // Config - loaded from UI on INIT response
  bool use_numpad_ = true;
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

  // ---- STT state ----
  // Mirrors the UI's setting; when false the 取消 key behaves as before.
  bool sttEnabled_ = false;
  bool cancelKeyDown_ = false;  // guards against X11 auto-repeat
  bool sttRecording_ = false;   // we told the UI to start recording
  std::unique_ptr<fcitx::EventSource> cancelHoldTimer_;

  // How the "waiting for Gemini" marker was shown, so we can take it back
  bool sttPreeditShown_ = false;
  bool sttPlaceholderCommitted_ = false;
  fcitx::InputContext *sttContext_ = nullptr;
};

class CustomEngineFactory : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};
