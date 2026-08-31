#include "ConfigLoader.h"
#include <QDebug>
#include <algorithm>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#ifdef QT_GUI_LIB
#include <QGuiApplication>
#include <QScreen>
#endif

QString cancelHoldToString(CancelHold value) {
  switch (value) {
  case CancelHold::Relate:
    return QStringLiteral("relate");
  case CancelHold::Shortcut:
    return QStringLiteral("shortcut");
  case CancelHold::None:
    return QStringLiteral("none");
  case CancelHold::Stt:
  default:
    return QStringLiteral("stt");
  }
}

CancelHold cancelHoldFromString(const QString &value, CancelHold fallback) {
  if (value == QLatin1String("stt"))
    return CancelHold::Stt;
  if (value == QLatin1String("relate"))
    return CancelHold::Relate;
  if (value == QLatin1String("shortcut"))
    return CancelHold::Shortcut;
  if (value == QLatin1String("none"))
    return CancelHold::None;
  // Missing or unreadable: keep whatever the caller already had.
  return fallback;
}

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
  config.defaultWidth = windowObj["width"].toInt(240);
  config.defaultHeight = windowObj["height"].toInt(340);
  config.minWidth = windowObj["minWidth"].toInt(120);
  config.maxWidth = windowObj["maxWidth"].toInt(480);

  QJsonObject storageObj = root["storage"].toObject();

  // Use storage size if available, otherwise default size
  if (storageObj.contains("width") && storageObj.contains("height")) {
    config.windowWidth = storageObj["width"].toInt(config.defaultWidth);
    config.windowHeight = storageObj["height"].toInt(config.defaultHeight);
  } else {
    config.windowWidth = config.defaultWidth;
    config.windowHeight = config.defaultHeight;
  }

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

  InputConfig &in = config.input;
  in.freq_order = systemObj["freq_order"].toBool(in.freq_order);
  in.hold_homo = systemObj["hold_homo"].toBool(in.hold_homo);
  in.hold_openclose = systemObj["hold_openclose"].toBool(in.hold_openclose);
  in.hold_shortcut = systemObj["hold_shortcut"].toBool(in.hold_shortcut);
  in.hold_ms = std::clamp(systemObj["hold_ms"].toInt(in.hold_ms), kKeyHoldMinMs,
                          kKeyHoldMaxMs);
  in.cancel_hold =
      cancelHoldFromString(systemObj["cancel_hold"].toString(), in.cancel_hold);

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
  QJsonObject root;

  // Attempt to load existing file to preserve other fields
  if (file.open(QIODevice::ReadOnly)) {
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isNull() && doc.isObject()) {
      root = doc.object();
    }
  } else if (file.exists()) {
    // If file exists but we can't read it, abort to avoid overwriting with
    // partial data
    qWarning()
        << "ConfigLoader::save: Could not open existing config for reading:"
        << path;
    return;
  }

  // Update Storage (Save current size and position)
  QJsonObject storageObj = root["storage"].toObject();
  storageObj["x"] = config.lastX;
  storageObj["y"] = config.lastY;
  storageObj["width"] = config.windowWidth;
  storageObj["height"] = config.windowHeight;
  root["storage"] = storageObj;

  // Update System
  QJsonObject systemObj = root["system"].toObject();
  systemObj["sc_output"] = config.sc_output;
  systemObj["use_numpad"] = config.use_numpad;
  systemObj["freq_order"] = config.input.freq_order;
  systemObj["hold_homo"] = config.input.hold_homo;
  systemObj["hold_openclose"] = config.input.hold_openclose;
  systemObj["hold_shortcut"] = config.input.hold_shortcut;
  systemObj["hold_ms"] =
      std::clamp(config.input.hold_ms, kKeyHoldMinMs, kKeyHoldMaxMs);
  systemObj["cancel_hold"] = cancelHoldToString(config.input.cancel_hold);
  root["system"] = systemObj;

  // Write back to file
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QJsonDocument newDoc(root);
    file.write(newDoc.toJson(QJsonDocument::Indented));
    file.close();
    qDebug() << "ConfigLoader::save: Successfully updated config at" << path;
  } else {
    qWarning() << "ConfigLoader::save: Could not open config file for writing:"
               << path;
  }
}
