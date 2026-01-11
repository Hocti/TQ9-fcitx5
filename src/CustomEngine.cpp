#include "CustomEngine.h"
#include <QDir>
#include <QMetaObject>
#include <QTimer>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>

QApplication *CustomEngine::myQApp = nullptr;
std::thread CustomEngine::qtThread;
std::atomic<bool> CustomEngine::qtRunning{false};

CustomEngine::CustomEngine(fcitx::Instance *instance) : instance_(instance) {
  // Ensure the directory exists for our database
  std::string userPkgData = fcitx::StandardPath::global().userDirectory(
      fcitx::StandardPath::Type::PkgData);
  QDir dir(QString::fromStdString(userPkgData));
  dir.mkpath("custom-input");

  std::string dbPath = userPkgData + "/custom-input/emoji.db";
  db_.init(dbPath);

  // Load config from search paths (PkgData should include share/fcitx5/)
  config_ = ConfigLoader::load(fcitx::StandardPath::global()
                                   .locate(fcitx::StandardPath::Type::PkgData,
                                           "custom-input/config.json")
                                   .c_str());

  ensureQtApp();

  // Create Window in Qt Thread
  QMetaObject::invokeMethod(
      myQApp,
      [this]() {
        window_ = std::make_unique<FloatingWindow>();
        window_->initialize(config_);

        QObject::connect(window_.get(), &FloatingWindow::buttonClicked,
                         [this](int id) {
                           // Button clicked, treat as Numpad input
                           // Ideally we need an InputContext to commit strings.
                           // But we don't have the current InputContext easily
                           // here unless we track it or pass it. Fcitx5 allows
                           // interaction via Instance, but committing text
                           // requires an InputContext. When button is clicked,
                           // the focus might be on the window? No, the window
                           // is "Tool" type or non-activating. The InputContext
                           // is the one currently active. We can get focused
                           // InputContext from Instance?
                           // instance_->inputContextManager().focusInputContext()
                           // ? No, public API might vary. Assuming we can get
                           // it from instance state or just use the last one.
                           // Usually addons shouldn't just commit to "current"
                           // without context. BUT for this task, the button
                           // click is an "Input". Let's defer functionality
                           // since `ic` is needed for `logicNumber`. Solution:
                           // We'll store the `lastActiveIC` in `activate` or
                           // `focusIn`. Since we are an Input Engine, we are
                           // mostly active when we are active. We can track the
                           // active input context.
                         });
      },
      Qt::BlockingQueuedConnection);
}

CustomEngine::~CustomEngine() {
  QMetaObject::invokeMethod(
      myQApp, [this]() { window_.reset(); }, Qt::BlockingQueuedConnection);

  // Signal Qt thread to stop if we own it
  if (qtRunning) {
    if (myQApp)
      myQApp->quit();
    if (qtThread.joinable())
      qtThread.join();
  }
}

void CustomEngine::ensureQtApp() {
  if (myQApp)
    return;

  if (QCoreApplication::instance()) {
    myQApp = qobject_cast<QApplication *>(QCoreApplication::instance());
    return;
  }

  // Start Qt thread
  // Start Qt thread
  qtRunning = true;
  std::promise<void> initPromise;
  auto initFuture = initPromise.get_future();

  qtThread = std::thread([&initPromise]() {
    int argc = 1;
    char appName[] = "fcitx5-custom";
    char *argv[] = {appName, nullptr};
    QApplication app(argc, argv);
    myQApp = &app;
    app.setQuitOnLastWindowClosed(false);
    initPromise.set_value();
    app.exec();
    qtRunning = false;
  });

  initFuture.wait();
}

void CustomEngine::activate(const fcitx::InputMethodEntry &entry,
                            fcitx::InputContextEvent &event) {
  Q_UNUSED(entry);
  Q_UNUSED(event);
  QMetaObject::invokeMethod(
      myQApp,
      [this]() {
        if (window_) {
          window_->show();
        }
      },
      Qt::QueuedConnection);
}

void CustomEngine::deactivate(const fcitx::InputMethodEntry &entry,
                              fcitx::InputContextEvent &event) {
  Q_UNUSED(entry);
  Q_UNUSED(event);
  QMetaObject::invokeMethod(
      myQApp,
      [this]() {
        if (window_)
          window_->hide();
      },
      Qt::QueuedConnection);
}

void CustomEngine::reset(const fcitx::InputMethodEntry &entry,
                         fcitx::InputContextEvent &event) {
  Q_UNUSED(entry);
  Q_UNUSED(event);
  // Logic to reset state if needed
}

void CustomEngine::keyEvent(const fcitx::InputMethodEntry &entry,
                            fcitx::KeyEvent &keyEvent) {
  Q_UNUSED(entry);
  if (keyEvent.isRelease())
    return;

  auto key = keyEvent.key();
  if (!key.isKeyPad())
    return;

  keyEvent.filterAndAccept(); // Consume the key

  int sym = key.sym();
  int number = -1;

  if (sym >= FcitxKey_KP_0 && sym <= FcitxKey_KP_9) {
    number = sym - FcitxKey_KP_0;
  } else if (sym == FcitxKey_KP_Decimal) {
    logicDot(keyEvent.inputContext());
    return;
  } else if (sym == FcitxKey_KP_Divide) {
    logicSlash(keyEvent.inputContext());
    return;
  }

  if (number != -1) {
    logicNumber(keyEvent.inputContext(), number);
  }
}

void CustomEngine::logicDot(fcitx::InputContext *ic) {
  Q_UNUSED(ic);
  QMetaObject::invokeMethod(
      myQApp,
      [this]() {
        if (window_)
          window_->reset();
      },
      Qt::QueuedConnection);
}

void CustomEngine::logicNumber(fcitx::InputContext *ic, int number) {
  if (number <= 6) {
    // Change button state
    QMetaObject::invokeMethod(
        myQApp,
        [this, number]() {
          if (window_) {
            auto *btn = window_->getButton(number);
            if (btn) {
              btn->setBackgroundColor(Qt::yellow);
              btn->setText("Clicked!");
            }
          }
        },
        Qt::QueuedConnection);
  } else if (number == 7) {
    // Type 1 chinese word
    ic->commitString("你");
  } else if (number == 8) {
    // Type 10 chinese words
    ic->commitString("你好你好你好你好你好你好你好你好你好你好");
  } else if (number == 9) {
    // Emoji
    std::string emoji = db_.getRandomEmoji();
    ic->commitString(emoji);
  }
}

void CustomEngine::logicSlash(fcitx::InputContext *ic) {
  // Quote wrap
  // Simple version: insert “” and move cursor back
  ic->commitString("“”");
  // Move cursor left by 1. Fcitx5 doesn't have direct "move cursor" in API
  // easily without surrounding text support? Check if forwardKey works.
  // Simulating Left Arrow Key.
  // need to construct a KeyEvent.
  // ic->forwardKey(fcitx::Key(FcitxKey_Left));  // Hypothetical API check
  // Actually fcitx5 input context has no simple "move cursor" method exposed to
  // Engine directly without handling surrounding text? But we can forward a
  // Left key event! But we correspond to "input key" event. We can't trigger a
  // new key event easily from inside keyEvent handler synchronously? Use an
  // InputContext::forwardKey if available? No. We accept this event, and then
  // we want to simulate another event. ic->inputPanel()->... ? Let's stick to
  // just typing quotes for now if cursor move is complex, or try surrounding
  // text deletion.

  // Optional: "if selected some text...".
  // auto surrounding = ic->surroundingText();
  // if (surrounding.isValid() && !surrounding.selection().isEmpty()) ...
  // This requires the app to support it.
  // Implementing standard "“”" insert for now.
}

std::vector<fcitx::InputMethodEntry> CustomEngine::listInputMethods() {
  std::vector<fcitx::InputMethodEntry> entries;
  entries.emplace_back("custom_input", "Custom Input", "zh", "custom-input");
  return entries;
}

fcitx::AddonInstance *
CustomEngineFactory::create(fcitx::AddonManager *manager) {
  Q_UNUSED(manager);
  return new CustomEngine(manager->instance());
}
