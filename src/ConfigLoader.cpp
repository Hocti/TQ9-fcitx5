#include "ConfigLoader.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

AppConfig ConfigLoader::load(const QString& path) {
    AppConfig config;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open config file:" << path;
        return config;
    }

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();

    QJsonObject window = root["window"].toObject();
    config.windowWidth = window["width"].toInt(400);
    config.windowHeight = window["height"].toInt(300);

    QJsonArray buttons = root["buttons"].toArray();
    for (const auto& val : buttons) {
        QJsonObject btn = val.toObject();
        ButtonConfig btnConfig;
        btnConfig.id = btn["id"].toInt();
        btnConfig.rect = QRect(
            btn["x"].toInt(),
            btn["y"].toInt(),
            btn["w"].toInt(),
            btn["h"].toInt()
        );
        config.buttons.push_back(btnConfig);
    }

    return config;
}
