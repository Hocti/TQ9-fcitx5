#pragma once

#include <QRect>
#include <QString>
#include <vector>

struct ButtonConfig {
  int id;
  QRect rect;
};

struct AppConfig {
  int windowWidth;
  int windowHeight;
  int minWidth = 100;
  int maxWidth = 800;

  // Storage
  int lastX = 100;
  int lastY = 100;

  // System
  bool sc_output = false;
  bool use_numpad = true;

  struct ButtonConfig {
    int id;
    QRect rect;
  };
  std::vector<ButtonConfig> buttons;

  QString configPath; // Store path for saving
};

class ConfigLoader {
public:
  static AppConfig load(const QString &path);
  static void save(const QString &path, const AppConfig &config);
};
