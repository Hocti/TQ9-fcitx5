#pragma once

#include "ConfigLoader.h"
#include "Database.h"
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
  void spawnUI();
  void sendToUI(const std::string &cmd);
  void handleUIOutput();
  void logicDot(fcitx::InputContext *ic);
  void logicNumber(fcitx::InputContext *ic, int number);
  void logicSlash(fcitx::InputContext *ic);

  fcitx::Instance *instance_;
  Database db_;
  AppConfig config_;

  // UI Process Management
  pid_t uiPid_ = -1;
  int uiStdinFd_ = -1;  // Write to UI
  int uiStdoutFd_ = -1; // Read from UI
  std::unique_ptr<fcitx::EventSource> stdoutSource_;
  std::unique_ptr<fcitx::EventSourceTime> hideTimer_;

  fcitx::InputContext *activeContext_ = nullptr;
};

class CustomEngineFactory : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};
