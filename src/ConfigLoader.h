#pragma once

#include <vector>
#include <QString>
#include <QRect>

struct ButtonConfig {
    int id;
    QRect rect;
};

struct AppConfig {
    int windowWidth;
    int windowHeight;
    std::vector<ButtonConfig> buttons;
};

class ConfigLoader {
public:
    static AppConfig load(const QString& path);
};
