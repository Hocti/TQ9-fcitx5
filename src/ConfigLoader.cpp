#include "ConfigLoader.h"
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#ifdef QT_GUI_LIB
#include <QGuiApplication>
#include <QScreen>
#endif

AppConfig ConfigLoader::load(const QString &path) {
  AppConfig config;
  config.configPath = path;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Could not open config file:" << path;
    return config;
  }

  QByteArray data = file.readAll();
  QJsonDocument doc = QJsonDocument::fromJson(data);
  QJsonObject root = doc.object();

  QJsonObject windowObj = root["window"].toObject();
  config.windowWidth = windowObj["width"].toInt(240);
  config.windowHeight = windowObj["height"].toInt(320);
  config.minWidth = windowObj["minWidth"].toInt(120);
  config.maxWidth = windowObj["maxWidth"].toInt(480);

  QJsonObject storageObj = root["storage"].toObject();

  // Default to 100,100 but try to use screen geometry if GUI is available
  int defaultX = 50;
  int defaultY = 50;

#ifdef QT_GUI_LIB
  QScreen *screen = QGuiApplication::primaryScreen();
  if (screen) {
    QRect screenGeom = screen->geometry();
    // Use windowWidth which was just loaded or defaulted to 240
    // Position at top-right corner with 50px margin
    defaultX = screenGeom.width() - config.windowWidth - 50;
  }
#endif

  config.lastX = storageObj["x"].toInt(defaultX);
  config.lastY = storageObj["y"].toInt(defaultY);

  QJsonObject systemObj = root["system"].toObject();
  config.sc_output = systemObj["sc_output"].toBool(false);
  config.use_numpad = systemObj["use_numpad"].toBool(true);

  QJsonArray buttonsArray = root["buttons"].toArray();
  for (const auto &btnVal : buttonsArray) {
    QJsonObject btnObj = btnVal.toObject();
    AppConfig::ButtonConfig btnConf;
    btnConf.id = btnObj["id"].toInt();
    btnConf.rect = QRect(btnObj["x"].toInt(), btnObj["y"].toInt(),
                         btnObj["w"].toInt(), btnObj["h"].toInt());
    btnConf.radius = btnObj["r"].toInt(0);
    config.buttons.push_back(btnConf);
  }

  QJsonObject statusObj = root["status"].toObject();
  config.statusRect =
      QRect(statusObj["x"].toInt(), statusObj["y"].toInt(),
            statusObj["width"].toInt(), statusObj["height"].toInt());

  // Load key mappings (numpad mode)
  QJsonObject keyObj = root["key"].toObject();
  for (auto it = keyObj.begin(); it != keyObj.end(); ++it) {
    config.keys[it.key()] = it.value().toInt();
  }

  // Load altkey mappings (non-numpad mode)
  QJsonObject altKeyObj = root["altkey"].toObject();
  for (auto it = altKeyObj.begin(); it != altKeyObj.end(); ++it) {
    config.altKeys[it.key()] = it.value().toInt();
  }

  return config;
}

void ConfigLoader::save(const QString &path, const AppConfig &config) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    // Read existing first to preserve comments/structure if possible,
    // but QJsonDocument doesn't support comments.
    // We will just read to get base object, or start fresh if fails.
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(data);
  QJsonObject root = doc.object();

  // Update window defaults if we want to save them?
  // Usually we only save "storage" (state) and maybe "system" settings.
  // The prompt says "json should storage the last window position, and the
  // window size"

  // Update Window Size
  QJsonObject windowObj = root["window"].toObject();
  windowObj["width"] = config.windowWidth;
  windowObj["height"] = config.windowHeight;
  root["window"] = windowObj;

  // Update Storage
  QJsonObject storageObj = root["storage"].toObject();
  storageObj["x"] = config.lastX;
  storageObj["y"] = config.lastY;
  root["storage"] = storageObj;

  // Update System
  QJsonObject systemObj = root["system"].toObject();
  systemObj["sc_output"] = config.sc_output;
  systemObj["use_numpad"] = config.use_numpad;
  root["system"] = systemObj;

  // Write back
  if (file.open(QIODevice::WriteOnly)) {
    QJsonDocument newDoc(root);
    file.write(newDoc.toJson());
    file.close();
  }
}
