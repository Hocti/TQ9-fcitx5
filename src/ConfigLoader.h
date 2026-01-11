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
  std::vector<ButtonConfig> buttons;
};

class ConfigLoader {
public:
  static AppConfig load(const QString &path);
};
