#include "ConfigLoader.h"
#include "FloatingWindow.h"
#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QSocketNotifier>
#include <QString>
#include <iostream>
#include <unistd.h>

// LayerShellQt for Wayland always-on-top
#include <LayerShellQt/Shell>

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

        std::cerr << "[UI] Received: " << line.toStdString() << std::endl;

        if (line == "SHOW") {
          🤣好好你 if (!window.isVisible()) {
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
