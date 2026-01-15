#include "ConfigLoader.h"
#include "FloatingWindow.h"
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
  for (int i = 0; i <= 10; ++i) {
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
    }
  }

  // Set button 0 to "標點" and button 10 to "取消" (matching C#
  // setButtonImg(0))
  CustomButton *btn0 = window.getButton(0);
  if (btn0) {
    btn0->setText("標點");
    btn0->setImage("");
    btn0->setBackgroundColor(Qt::white);
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

int main(int argc, char *argv[]) {
  // Initialize LayerShellQt before QApplication
  // This sets the environment for Wayland layer-shell integration
  LayerShellQt::Shell::useLayerShell();

  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);

  FloatingWindow window;

  QSocketNotifier notifier(STDIN_FILENO, QSocketNotifier::Read);
  QObject::connect(&notifier, &QSocketNotifier::activated, [&window](int) {
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
                    btn->setText(text);
                    btn->setImage(""); // Clear image for text display
                    if (text.isEmpty()) {
                      btn->setBackgroundColor(Qt::gray);
                      btn->setDisabledState(true);
                    } else {
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
              QString imagePath = imgPath + QString("/10_%1.png").arg(i);
              btn->setImage(imagePath);
              btn->setBackgroundColor(Qt::white);
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
          // SET_STATUS <text> - set window title
          QString statusText = line.mid(11).trimmed();
          window.setWindowTitle(statusText);
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
