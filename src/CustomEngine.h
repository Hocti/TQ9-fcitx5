#pragma once

#include "ConfigLoader.h"
#include "Database.h"
#include "Q9Logic.h"
#include <fcitx-utils/eventloopinterface.h>
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
  void updateUIState();

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
};

class CustomEngineFactory : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};
