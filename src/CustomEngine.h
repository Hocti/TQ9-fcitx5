#pragma once

#include "ConfigLoader.h"
#include "Database.h"
#include "ui/FloatingWindow.h"
#include <QApplication>
#include <atomic>
#include <fcitx/addonfactory.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <memory>
#include <thread>

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

private:
  void ensureQtApp();
  void logicDot(fcitx::InputContext *ic);
  void logicNumber(fcitx::InputContext *ic, int number);
  void logicSlash(fcitx::InputContext *ic);

  fcitx::Instance *instance_;
  std::unique_ptr<FloatingWindow> window_;
  Database db_;
  AppConfig config_;

  // Qt Loop handling
  static QApplication *myQApp;
  static std::thread qtThread;
  static std::atomic<bool> qtRunning;
};

class CustomEngineFactory : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};
