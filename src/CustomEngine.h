#pragma once

#include "ConfigLoader.h"
#include "Database.h"
#include "Q9Logic.h"
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx/addonfactory.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <memory>
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

  // UI Process Management
  pid_t uiPid_ = -1;
  int uiStdinFd_ = -1;  // Write to UI
  int uiStdoutFd_ = -1; // Read from UI
  std::unique_ptr<fcitx::EventSource> stdoutSource_;
  std::unique_ptr<fcitx::EventSource> hideTimer_;

  fcitx::InputContext *activeContext_ = nullptr;
};

class CustomEngineFactory : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};
