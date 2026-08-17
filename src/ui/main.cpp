#include "ConfigLoader.h"
#include "FloatingWindow.h"
#include "stt/SettingsDialog.h"
#include "stt/SttController.h"
#include "stt/SttSettings.h"
#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMap>
#include <QSocketNotifier>
#include <QString>
#include <iostream>
#include <sqlite3.h>
#include <unistd.h>

// LayerShellQt for Wayland always-on-top
#include <LayerShellQt/Shell>

// Global image cache
static QMap<QString, QImage> g_imageCache;

// Global database handle
static sqlite3 *g_db = nullptr;

// Load all images from data/img directory
static void loadAllImages(const QString &basePath) {
  QString imgPath = basePath + "/img";
  std::cerr << "[UI] Loading images from: " << imgPath.toStdString()
            << std::endl;

  int loadedCount = 0;
  for (int i = 0; i <= 9; ++i) {
    for (int j = 1; j <= 9; ++j) {
      QString filename = QString("%1_%2.png").arg(i).arg(j);
      QString fullPath = imgPath + "/" + filename;
      QImage img;
      if (img.load(fullPath)) {
        g_imageCache[filename] = img;
        loadedCount++;
      } else {
        std::cerr << "[UI] Warning: Failed to load image: "
                  << fullPath.toStdString() << std::endl;
      }
    }
  }
  std::cerr << "[UI] Loaded " << loadedCount << " images" << std::endl;
}

// Load SQLite database
static bool loadDatabase(const QString &basePath) {
  QString dbPath = basePath + "/dataset.db";
  std::cerr << "[UI] Loading database from: " << dbPath.toStdString()
            << std::endl;

  if (sqlite3_open(dbPath.toUtf8().constData(), &g_db) != SQLITE_OK) {
    std::cerr << "[UI] Error: Can't open database: " << sqlite3_errmsg(g_db)
              << std::endl;
    return false;
  }

  std::cerr << "[UI] Database loaded successfully" << std::endl;
  return true;
}

// Initialize buttons with default images (0_1.png~0_9.png) - matches C#
// setButtonImg(0)
static void initializeButtons(FloatingWindow &window, const QString &basePath) {
  QString imgPath = basePath + "/img";

  // Set 0_1.png ~ 0_9.png on buttons 1-9 with no text (default/reset state)
  for (int i = 1; i <= 9; ++i) {
    CustomButton *btn = window.getButton(i);
    if (btn) {
      QString imagePath = imgPath + QString("/0_%1.png").arg(i);
      btn->setImage(imagePath);
      btn->setText(""); // No text in default state
      btn->setBackgroundColor(Qt::white);
      btn->setDisabledState(false);
      btn->setOpacity(1);
    }
  }

  // Set button 0 to "標點" and button 10 to "取消" (matching C#
  // setButtonImg(0))
  CustomButton *btn0 = window.getButton(0);
  if (btn0) {
    btn0->setText("標點");
    btn0->setImage("");
    btn0->setBackgroundColor(Qt::white);
    btn0->setDisabledState(false);
    btn0->setOpacity(1);
  }

  CustomButton *btn10 = window.getButton(10);
  if (btn10) {
    btn10->setText("取消");
    btn10->setImage("");
    btn10->setBackgroundColor(Qt::white);
  }

  std::cerr << "[UI] Buttons initialized with default images (0_*.png)"
            << std::endl;
}

// Send a line to the engine on stdout.
static void sendToEngine(const QString &line) {
  std::cout << line.toStdString() << std::endl;
}

// Payloads that may contain newlines travel base64-encoded over the pipe.
static QString encodePayload(const QString &text) {
  return QString::fromLatin1(text.toUtf8().toBase64());
}

static QString decodePayload(const QString &encoded) {
  return QString::fromUtf8(QByteArray::fromBase64(encoded.toLatin1()));
}

int main(int argc, char *argv[]) {
  // Initialize LayerShellQt before QApplication
  // This sets the environment for Wayland layer-shell integration
  LayerShellQt::Shell::useLayerShell();

  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);

  FloatingWindow window;
  SttController stt;

  window.setSttAvailable(stt.settings().isUsable());

  // ---- STT wiring -------------------------------------------------------
  QObject::connect(&stt, &SttController::needContext,
                   []() { sendToEngine("STT_NEED_CONTEXT"); });
  QObject::connect(&stt, &SttController::needSelection,
                   []() { sendToEngine("TR_NEED_SELECTION"); });
  QObject::connect(&stt, &SttController::recordingStarted, [&window]() {
    window.setRecording(true);
    window.setStatusText("錄音中…");
  });
  QObject::connect(&stt, &SttController::recordingTick, [&window](double s) {
    window.setRecordingSeconds(s);
  });
  QObject::connect(&stt, &SttController::recordingStopped, [&window]() {
    window.setRecording(false);
    window.setStatusText("九万");
  });
  QObject::connect(&stt, &SttController::requestPending, [&window]() {
    window.setStatusText("辨識中…");
    sendToEngine("STT_PENDING");
  });
  QObject::connect(&stt, &SttController::translatePending, [&window]() {
    window.setStatusText("翻譯中…");
    sendToEngine("TR_PENDING");
  });
  QObject::connect(&stt, &SttController::resultReady,
                   [&window](const QString &text) {
                     window.setStatusText("九万");
                     sendToEngine("AI_RESULT " + encodePayload(text));
                   });
  QObject::connect(&stt, &SttController::requestAborted, [&window]() {
    window.setStatusText("九万");
    sendToEngine("AI_ABORT");
  });
  // Keeps the top-bar buttons and the engine in step with the settings.
  auto applyAvailability = [&window, &stt]() {
    const bool sttOn = stt.settings().isUsable();
    // Translating only needs a key, not the STT toggle.
    const bool translateOn = !stt.settings().apiKey.isEmpty();
    window.setSttAvailable(sttOn);
    window.setTranslateAvailable(translateOn);
    sendToEngine(QString("STT_ENABLED %1").arg(sttOn ? 1 : 0));
  };
  QObject::connect(&stt, &SttController::enabledChanged,
                   [applyAvailability](bool) { applyAvailability(); });

  QObject::connect(&window, &FloatingWindow::recordPressed,
                   [&stt]() { stt.holdBegin(); });
  QObject::connect(&window, &FloatingWindow::recordReleased,
                   [&stt]() { stt.holdEnd(); });
  QObject::connect(&window, &FloatingWindow::translateRequested,
                   [&stt]() { stt.requestTranslation(); });
  QObject::connect(&window, &FloatingWindow::settingsRequested,
                   [&stt, applyAvailability]() {
                     SettingsDialog dialog;
                     if (dialog.exec() == QDialog::Accepted) {
                       stt.reloadSettings();
                       applyAvailability();
                     }
                   });

  QSocketNotifier notifier(STDIN_FILENO, QSocketNotifier::Read);
  QObject::connect(&notifier, &QSocketNotifier::activated,
                   [&window, &stt, applyAvailability](int) {
    static QByteArray buffer;
    char tmp[4096];
    ssize_t n = read(STDIN_FILENO, tmp, sizeof(tmp));
    if (n > 0) {
      buffer.append(tmp, n);
      while (true) {
        int idx = buffer.indexOf('\n');
        if (idx == -1)
          break;
        QByteArray lineBytes = buffer.left(idx);
        buffer = buffer.mid(idx + 1);
        QString line = QString::fromUtf8(lineBytes).trimmed();

        // std::cerr << "[UI] Received: " << line.toStdString() << std::endl;

        if (line == "SHOW") {
          if (!window.isVisible()) {
            std::cerr << "[UI] Showing window" << std::endl;
            window
                .showWindow(); // Use showWindow() for proper surface recreation
          }
        } else if (line == "HIDE") {
          std::cerr << "[UI] Hiding window" << std::endl;
          window.hide();
        } else if (line == "RESET") {
          // Reset buttons to default state (0_*.png images, matching C#
          // cancel()/setButtonImg(0))
          QFileInfo configInfo(window.getConfigPath());
          QString dataPath = configInfo.absolutePath();
          initializeButtons(window, dataPath);
          window.reset();
          window.raise();
        } else if (line == "QUIT") {
          window.saveConfig();
          window.hide();
          QApplication::quit();
        } else if (line.startsWith("INIT ")) {
          QString path = line.mid(5).trimmed();
          std::cerr << "[UI] Initializing with config: " << path.toStdString()
                    << std::endl;
          AppConfig config = ConfigLoader::load(path);
          window.initialize(config);

          // Extract data directory path from config file path
          // Config is in data/config.json, we need data/ path
          QFileInfo configInfo(path);
          QString dataPath = configInfo.absolutePath();

          // Load all images
          loadAllImages(dataPath);

          // Load database
          loadDatabase(dataPath);

          // Initialize buttons with images and Chinese text
          initializeButtons(window, dataPath);

          // STT: the installed default prompt lives beside config.json.
          // Seed the editable copy now so it is there to be edited.
          SttPaths::setDataDir(dataPath);
          SttSettingsIO::loadPromptTemplate();
          SttSettingsIO::loadTranslatePromptTemplate();
          stt.reloadSettings();
          applyAvailability();
        } else if (line == "STT_START") {
          // Engine already timed the 取消 long-press for us.
          stt.startNow();
        } else if (line == "STT_STOP") {
          stt.stopNow();
        } else if (line.startsWith("STT_CONTEXT ")) {
          stt.setContext(decodePayload(line.mid(12).trimmed()));
        } else if (line == "STT_CONTEXT") {
          stt.setContext(QString());
        } else if (line.startsWith("TR_SELECTION ")) {
          stt.translateSelection(decodePayload(line.mid(13).trimmed()));
        } else if (line == "TR_SELECTION") {
          stt.translateSelection(QString());
        } else if (line == "CHECK_FOCUS") {
          bool active = window.isActiveWindow();
          std::cout << (active ? "FOCUS_TRUE" : "FOCUS_FALSE") << std::endl;
        } else if (line.startsWith("UPDATE_BUTTONS")) {
          QString content = line.mid(14).trimmed(); // UPDATE_BUTTONS (len 14)
          if (!content.isEmpty()) {
            QStringList items = content.split('|');
            for (const QString &item : items) {
              if (item.isEmpty())
                continue;
              int colIdx = item.indexOf(':');
              if (colIdx != -1) {
                bool ok;
                int id = item.left(colIdx).trimmed().toInt(&ok);
                if (ok) {
                  QString text = item.mid(colIdx + 1);
                  CustomButton *btn = window.getButton(id);
                  if (btn) {
                    btn->setImage(""); // Clear image for text display
                    btn->setOpacity(1);
                    if (text.isEmpty() || text == "*") {
                      btn->setText("");
                      btn->setBackgroundColor(Qt::gray);
                      btn->setDisabledState(true);
                    } else {
                      btn->setText(text);
                      btn->setBackgroundColor(Qt::white);
                      btn->setDisabledState(false);
                    }
                  }
                }
              }
            }
          }
        } else if (line.startsWith("SET_IMAGES ")) {
          // SET_IMAGES <type> - set button 1-9 images to type_1.png through
          // type_9.png
          QString content = line.mid(11).trimmed();
          bool ok;
          int imageType = content.toInt(&ok);
          bool isTen = false;
          if (ok && imageType == 10) {
            isTen = true;
            imageType = 0;
          }
          if (ok && imageType >= 0) {
            QFileInfo configInfo(window.getConfigPath());
            QString imgPath = configInfo.absolutePath() + "/img";

            for (int i = 1; i <= 9; ++i) {
              CustomButton *btn = window.getButton(i);
              if (btn) {
                QString imagePath =
                    imgPath + QString("/%1_%2.png").arg(imageType).arg(i);
                btn->setImage(imagePath);
                btn->setText(""); // No text overlay during input mode
                btn->setBackgroundColor(Qt::white);
                btn->setDisabledState(false);
                if (isTen) {
                  btn->setOpacity(0.5);
                } else {
                  btn->setOpacity(1);
                }
              }
            }
          }
        } else if (line.startsWith("SET_RELATED")) {
          // SET_RELATED id:text|id:text... - show related words with images
          // visible
          QString content = line.mid(11).trimmed();
          QFileInfo configInfo(window.getConfigPath());
          QString imgPath = configInfo.absolutePath() + "/img";

          // First reset images to base (type 10 shows through)
          for (int i = 1; i <= 9; ++i) {
            CustomButton *btn = window.getButton(i);
            if (btn) {
              QString imagePath = imgPath + QString("/0_%1.png").arg(i);
              btn->setImage(imagePath);
              btn->setBackgroundColor(Qt::white);
              btn->setOpacity(1);
            }
          }

          // Then overlay with related text
          if (!content.isEmpty()) {
            QStringList items = content.split('|');
            for (const QString &item : items) {
              if (item.isEmpty())
                continue;
              int colIdx = item.indexOf(':');
              if (colIdx != -1) {
                bool ok;
                int id = item.left(colIdx).trimmed().toInt(&ok);
                if (ok && id >= 1 && id <= 9) {
                  QString text = item.mid(colIdx + 1);
                  CustomButton *btn = window.getButton(id);
                  if (btn) {
                    // Show text over image - using small top-left alignment
                    btn->setText(text);
                  }
                }
              }
            }
          }

          // Handle button 0 and 10 text
          QStringList items = content.split('|');
          for (const QString &item : items) {
            if (item.isEmpty())
              continue;
            int colIdx = item.indexOf(':');
            if (colIdx != -1) {
              bool ok;
              int id = item.left(colIdx).trimmed().toInt(&ok);
              if (ok && (id == 0 || id == 10)) {
                QString text = item.mid(colIdx + 1);
                CustomButton *btn = window.getButton(id);
                if (btn) {
                  btn->setText(text);
                  btn->setImage("");
                  btn->setBackgroundColor(Qt::white);
                }
              }
            }
          }
        } else if (line.startsWith("SET_STATUS ")) {
          // SET_STATUS <text> - set window title and status label
          QString statusText = line.mid(11).trimmed();
          window.setWindowTitle(statusText);
          window.setStatusText(statusText);
        }
      }
    } else if (n == 0) {
      QApplication::quit();
    }
  });

  QObject::connect(&window, &FloatingWindow::buttonClicked, [](int id) {
    // Flush formatting to ensure line is sent immediately
    std::cout << "CLICK " << id << std::endl;
  });

  return app.exec();
}
