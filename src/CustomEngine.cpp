#include "CustomEngine.h"
#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

CustomEngine::CustomEngine(fcitx::Instance *instance) : instance_(instance) {
  // Ensure the directory exists (legacy check, still valid)
  std::string userPkgData = fcitx::StandardPath::global().userDirectory(
      fcitx::StandardPath::Type::PkgData);
  // We don't need QDir here. Use system commands or filesystem if needed.
  // Actually we need to ensure the dir exists.
  // Fcitx has StandardPath utils? Or plain C++ std::filesystem (C++17).
  // CMake set C++20.
  // Using std::filesystem just in case, or system mkdir.
  std::string cmd = "mkdir -p " + userPkgData + "/custom-input";
  system(cmd.c_str());

  std::string dbPath = userPkgData + "/custom-input/emoji.db";
  db_.init(dbPath);

  // We load config to pass path to UI?
  // UI needs full path.
  // We can find the config path here and pass it via INIT.
  std::string configPath = fcitx::StandardPath::global().locate(
      fcitx::StandardPath::Type::PkgData, "custom-input/config.json");

  // Store configPath to send later?
  // Or just load it.
  // ConfigLoader uses QString... CustomEngine (this file) links to
  // ConfigLoader? Yes but we want to avoid Qt in CustomEngine if possible to
  // check headers. But CMake links Qt5::Core to CustomEngine. So we technically
  // CAN use QString and ConfigLoader. But we removed ConfigLoader.h include?
  // No, it's there. If we assume ConfigLoader.h uses Qt types, we need Qt
  // headers. CustomEngine.h includes ConfigLoader.h. ConfigLoader.h likely
  // includes <QString> etc. So CustomEngine needs Qt. Wait, if CustomEngine
  // includes Qt headers, it might drag in Qt dependencies? Yes. This is fine
  // essentially, as long as we don't start QApplication. But ideally we
  // decouple. Whatever, we just spawn UI. We tell UI to load the config from
  // `configPath`.
}

CustomEngine::~CustomEngine() {
  if (uiPid_ != -1) {
    sendToUI("QUIT");
    close(uiStdinFd_);
    close(uiStdoutFd_);
    waitpid(uiPid_, nullptr, 0);
  }
}

void CustomEngine::spawnUI() {
  if (uiPid_ != -1)
    return;

  int in_pipe[2];
  int out_pipe[2];

  if (pipe(in_pipe) == -1 || pipe(out_pipe) == -1) {
    perror("pipe");
    return;
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    return;
  }

  if (pid == 0) {
    // Child
    dup2(in_pipe[0], STDIN_FILENO);
    dup2(out_pipe[1], STDOUT_FILENO);

    close(in_pipe[1]);
    close(out_pipe[0]);
    close(in_pipe[0]);
    close(out_pipe[1]);

    execlp("fcitx5-custom-ui", "fcitx5-custom-ui", nullptr);
    perror("execlp");
    exit(1);
  } else {
    // Parent
    close(in_pipe[0]);
    close(out_pipe[1]);

    uiStdinFd_ = in_pipe[1];
    uiStdoutFd_ = out_pipe[0];
    uiPid_ = pid;

    std::cerr << "[CustomEngine] UI Spawned with PID: " << pid << std::endl;

    stdoutSource_ = instance_->eventLoop().addIOEvent(
        uiStdoutFd_, fcitx::IOEventFlag::In,
        [this](fcitx::EventSourceIO *source, int fd,
               fcitx::IOEventFlags flags) {
          handleUIOutput();
          return true;
        });

    // Send Config Init
    std::string configPath = fcitx::StandardPath::global().locate(
        fcitx::StandardPath::Type::PkgData, "custom-input/config.json");

    std::cerr << "[CustomEngine] Config Path: '" << configPath << "'"
              << std::endl;

    if (configPath.empty()) {
      std::cerr << "[CustomEngine] ERROR: Config file not found!" << std::endl;
    }

    sendToUI("INIT " + configPath);
  }
}

void CustomEngine::sendToUI(const std::string &cmd) {
  if (uiStdinFd_ == -1)
    return;
  std::string line = cmd + "\n";
  write(uiStdinFd_, line.c_str(), line.length());
}

void CustomEngine::handleUIOutput() {
  char buffer[256];
  ssize_t n = read(uiStdoutFd_, buffer, sizeof(buffer) - 1);
  if (n > 0) {
    buffer[n] = '\0';
    std::string data(buffer);
    // Naive line parsing (partial lines possible in real world, handling
    // strictly here) Ideally buffer accumulation. Assuming "CLICK <n>\n" comes
    // in one read for now.
    if (data.rfind("CLICK ", 0) == 0) {
      int id = std::stoi(data.substr(6));
      if (activeContext_) {
        logicNumber(activeContext_, id);
      }
    } else if (data.rfind("FOCUS_TRUE", 0) == 0) {
      // UI has focus, do not hide.
      // Cancel any pending hide logic if we had any (though we rely on
      // response)
    } else if (data.rfind("FOCUS_FALSE", 0) == 0) {
      // UI does not have focus, and we are in deactivate check?
      // Check if we are still "active" in Fcitx sense?
      // Actually, if we received FOCUS_FALSE, it means we sent CHECK_FOCUS.
      // We only send CHECK_FOCUS from deactivate.
      // So if false, we hide.
      sendToUI("HIDE");
    }
  } else if (n == 0) {
    // EOF, child died
    uiPid_ = -1;
    uiStdinFd_ = -1;
    uiStdoutFd_ = -1;
    stdoutSource_.reset();
  }
}

void CustomEngine::activate(const fcitx::InputMethodEntry &entry,
                            fcitx::InputContextEvent &event) {
  activeContext_ = event.inputContext();

  if (hideTimer_) {
    hideTimer_.reset();
  }

  spawnUI();
  sendToUI("SHOW");
}

void CustomEngine::deactivate(const fcitx::InputMethodEntry &entry,
                              fcitx::InputContextEvent &event) {
  // activeContext_ = nullptr; // Commented out to allow committing to
  // background app if floating window takes focus

  uint64_t timeout = fcitx::now(CLOCK_MONOTONIC) + 100000; // 100ms
  hideTimer_ = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, timeout, 0, // One-shot
      [this](fcitx::EventSourceTime *, uint64_t) {
        sendToUI("CHECK_FOCUS");
        hideTimer_.reset();
        return true;
      });
}

void CustomEngine::reset(const fcitx::InputMethodEntry &entry,
                         fcitx::InputContextEvent &event) {
  sendToUI("RESET");
}

void CustomEngine::keyEvent(const fcitx::InputMethodEntry &entry,
                            fcitx::KeyEvent &keyEvent) {
  if (keyEvent.isRelease())
    return;
  auto key = keyEvent.key();
  if (!key.isKeyPad())
    return;

  keyEvent.filterAndAccept();

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

void CustomEngine::logicDot(fcitx::InputContext *ic) { sendToUI("RESET"); }

void CustomEngine::logicNumber(fcitx::InputContext *ic, int number) {
  if (number <= 6) {
    // TODO: Send "BUTTON_HIGHLIGHT <n>" to UI?
    // UI assumes control? UI click triggers highlight?
    // "Change button state"
  } else if (number == 7) {
    ic->commitString("你");
  } else if (number == 8) {
    ic->commitString("好好");
  } else if (number == 9) {
    std::string emoji = db_.getRandomEmoji();
    ic->commitString(emoji);
  }
}

void CustomEngine::logicSlash(fcitx::InputContext *ic) {
  ic->commitString("“”");
}

std::vector<fcitx::InputMethodEntry> CustomEngine::listInputMethods() {
  std::vector<fcitx::InputMethodEntry> entries;
  entries.emplace_back("custom_input", "Q9", "zh", "custom-input");
  return entries;
}

fcitx::AddonInstance *
CustomEngineFactory::create(fcitx::AddonManager *manager) {
  return new CustomEngine(manager->instance());
}
