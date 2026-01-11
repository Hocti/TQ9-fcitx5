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

// Chinese numerals for buttons 1-9
static const QString chineseNumerals[] = {"一", "二", "三", "四", "五",
                                          "六", "七", "八", "九"};

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

// Initialize buttons with images and Chinese text
static void initializeButtons(FloatingWindow &window, const QString &basePath) {
  QString imgPath = basePath + "/img";

  // Set 10_1.png ~ 10_9.png on buttons 1-9 and set Chinese text
  for (int i = 1; i <= 9; ++i) {
    CustomButton *btn = window.getButton(i);
    if (btn) {
      // Set image 10_x.png
      QString imagePath = imgPath + QString("/10_%1.png").arg(i);
      btn->setImage(imagePath);

      // Set Chinese numeral text (一 to 九)
      btn->setText(chineseNumerals[i - 1]);

      std::cerr << "[UI] Button " << i << " initialized with image and text"
                << std::endl;
    }
  }
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
            window.show();
            window.raise();
          }
        } else if (line == "HIDE") {
          std::cerr << "[UI] Hiding window" << std::endl;
          window.hide();
        } else if (line == "RESET") {
          window.reset();
          window.raise();
        } else if (line == "QUIT") {
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
