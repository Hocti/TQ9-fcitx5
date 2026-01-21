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
  std::string cmd = "mkdir -p " + userPkgData + "/tq9";
  system(cmd.c_str());

  // Load config for key mappings FIRST - we derive database path from config
  // location
  std::string configPath = fcitx::StandardPath::global().locate(
      fcitx::StandardPath::Type::PkgData, "tq9/config.json");

  if (!configPath.empty()) {
    // Derive database path from config path (same directory)
    std::string dataDir = configPath.substr(0, configPath.rfind('/'));
    std::string dbPath = dataDir + "/dataset.db";

    std::cerr << "[CustomEngine] Config path: " << configPath << std::endl;
    std::cerr << "[CustomEngine] Database path: " << dbPath << std::endl;

    // init logic with correct database path
    if (!logic_.init(dbPath)) {
      std::cerr << "Logic DB Init Failed: " << dbPath << std::endl;
    } else {
      std::cerr << "[CustomEngine] Logic DB initialized successfully"
                << std::endl;
    }

    AppConfig config = ConfigLoader::load(QString::fromStdString(configPath));
    use_numpad_ = config.use_numpad;

    // Build altkey -> num mapping (for num0~num9)
    // Config stores Windows VK codes (uppercase ASCII for letters: A=65, X=88,
    // etc) Fcitx/X11 uses keysyms where lowercase a=97, x=120, etc We need to
    // convert: VK code -> lowercase letter keysym
    for (int i = 0; i <= 9; ++i) {
      QString keyName = QString("num%1").arg(i);
      if (config.altKeys.contains(keyName)) {
        int vkCode = config.altKeys[keyName];
        // Convert uppercase VK code (A=65..Z=90) to lowercase keysym
        // (a=97..z=122)
        int keysym = (vkCode >= 65 && vkCode <= 90) ? (vkCode + 32) : vkCode;
        altKeyToNum_[keysym] = i;
        std::cerr << "[CustomEngine] altKey num" << i << " = " << vkCode
                  << " -> keysym " << keysym << std::endl;
      }
    }

    // Build altkey -> command mapping
    auto addCmd = [&](const QString &name, Q9Key cmd) {
      if (config.altKeys.contains(name)) {
        int vkCode = config.altKeys[name];
        int keysym = (vkCode >= 65 && vkCode <= 90) ? (vkCode + 32) : vkCode;
        altKeyToCmd_[keysym] = cmd;
        std::cerr << "[CustomEngine] altKey " << name.toStdString() << " = "
                  << vkCode << " -> keysym " << keysym << std::endl;
      }
    };

    addCmd("cancel", Q9Key::Cancel);
    addCmd("relate", Q9Key::Relate);
    addCmd("homo", Q9Key::Homo);
    addCmd("openclose", Q9Key::OpenClose);
    // prev and shortcut share same key, handled based on candidateMode
    if (config.altKeys.contains("prev")) {
      int vkCode = config.altKeys["prev"];
      int keysym = (vkCode >= 65 && vkCode <= 90) ? (vkCode + 32) : vkCode;
      // Store as Shortcut - we'll check candidateMode at runtime
      altKeyToCmd_[keysym] =
          Q9Key::Shortcut; // Treated as shortcut, but also prev
      std::cerr << "[CustomEngine] altKey prev/shortcut = " << vkCode
                << " -> keysym " << keysym << std::endl;
    }

    std::cerr << "[CustomEngine] use_numpad=" << use_numpad_ << std::endl;
  }
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

    execlp("fcitx5-tq9-ui", "fcitx5-tq9-ui", nullptr);
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
        fcitx::StandardPath::Type::PkgData, "tq9/config.json");

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
        bool changed = false;
        if (id <= 9) {
          changed = logic_.processKey(id);
        } else if (id == 10) {
          changed = logic_.processCommand(Q9Key::Cancel);
        } else if (id == 0) {
          // Button 0 -> Page Down in Candidate Mode?
          // Logic handles 0 as NextPage or similar if mapped?
          changed = logic_.processKey(0);
        }

        if (logic_.hasCommitString()) {
          activeContext_->commitString(logic_.getCommitString());
          logic_.clearCommitString();
          changed = true;
        }

        if (changed) {
          updateUIState();
        }
      }
    } else if (data.rfind("FOCUS_TRUE", 0) == 0) {
      // UI has focus, do not hide.
      // Clear pending flag - we've received the response
      pendingFocusCheck_ = false;
    } else if (data.rfind("FOCUS_FALSE", 0) == 0) {
      // Only hide if we're still waiting for this response
      // This prevents race condition where the window was re-activated
      // between sending CHECK_FOCUS and receiving FOCUS_FALSE
      if (pendingFocusCheck_) {
        pendingFocusCheck_ = false;
        sendToUI("HIDE");
      }
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

  // Cancel any pending focus check to prevent race condition
  // where FOCUS_FALSE arrives after we've re-activated
  pendingFocusCheck_ = false;

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
        pendingFocusCheck_ = true; // Mark that we're expecting a response
        sendToUI("CHECK_FOCUS");
        hideTimer_.reset();
        return true;
      });
}

void CustomEngine::reset(const fcitx::InputMethodEntry &entry,
                         fcitx::InputContextEvent &event) {
  Q9State state = logic_.getState();

  // Only reset if there's actual input state (candidateMode or inputCode)
  // Preserve the state if we're just showing related words after a commit
  if (state.candidateMode || !state.inputCode.empty()) {
    sendToUI("RESET");
    logic_.reset();
    updateUIState();
  } else if (!state.relatedWords.empty()) {
    // We have related words to show - don't reset, but update UI
    std::cerr
        << "[CustomEngine] reset() skipped - preserving relatedWords display"
        << std::endl;
    updateUIState();
  }
  // If nothing to reset, do nothing (already in base state)
}

void CustomEngine::keyEvent(const fcitx::InputMethodEntry &entry,
                            fcitx::KeyEvent &keyEvent) {
  if (keyEvent.isRelease())
    return;
  auto key = keyEvent.key();

  bool handled = false;
  bool changed = false;
  int sym = key.sym();

  // When use_numpad is true, handle numpad keys
  // When use_numpad is false, handle alt keys (regular letter keys)

  if (use_numpad_) {
    // Numpad mode - original behavior
    if (key.isKeyPad()) {
      // Numpad 0-9
      if (sym >= FcitxKey_KP_0 && sym <= FcitxKey_KP_9) {
        int num = sym - FcitxKey_KP_0;
        changed = logic_.processKey(num);
        handled = true;
      }
      // Numpad . (decimal) = Cancel
      else if (sym == FcitxKey_KP_Decimal) {
        changed = logic_.processCommand(Q9Key::Cancel);
        handled = true;
      }
      // Numpad + = Relate
      else if (sym == FcitxKey_KP_Add) {
        changed = logic_.processCommand(Q9Key::Relate);
        handled = true;
      }
      // Numpad - = Shortcut (when not in select mode) or PrevPage (in select
      // mode)
      else if (sym == FcitxKey_KP_Subtract) {
        Q9State state = logic_.getState();
        if (state.candidateMode) {
          changed = logic_.processCommand(Q9Key::PrevPage);
        } else {
          changed = logic_.processCommand(Q9Key::Shortcut);
        }
        handled = true;
      }
      // Numpad * = Homo (toggle homophone mode)
      else if (sym == FcitxKey_KP_Multiply) {
        changed = logic_.processCommand(Q9Key::Homo);
        handled = true;
      }
      // Numpad / = OpenClose (bracket pairs)
      else if (sym == FcitxKey_KP_Divide) {
        changed = logic_.processCommand(Q9Key::OpenClose);
        handled = true;
      }
    }
  } else {
    // Alt key mode (non-numpad) - for keyboards without numpad
    // Check for num0~num9 alt keys
    auto numIt = altKeyToNum_.find(sym);
    if (numIt != altKeyToNum_.end()) {
      int num = numIt->second;
      changed = logic_.processKey(num);
      handled = true;
    } else {
      // Check for command alt keys
      auto cmdIt = altKeyToCmd_.find(sym);
      if (cmdIt != altKeyToCmd_.end()) {
        Q9Key cmd = cmdIt->second;

        // Special handling for shortcut/prev - same key, different behavior
        if (cmd == Q9Key::Shortcut) {
          Q9State state = logic_.getState();
          if (state.candidateMode) {
            changed = logic_.processCommand(Q9Key::PrevPage);
          } else {
            changed = logic_.processCommand(Q9Key::Shortcut);
          }
        } else {
          changed = logic_.processCommand(cmd);
        }
        handled = true;
      } else if (sym >= 'a' && sym <= 'z') {
        // Block other letter keys when in non-numpad mode to prevent typing
        // (mirroring C# behavior that returns true for keyCode >= 65 && keyCode
        // <= 90)
        handled = true;
      }
    }
  }

  if (handled) {
    keyEvent.filterAndAccept();

    // Check for commit
    if (logic_.hasCommitString()) {
      std::string commitStr = logic_.getCommitString();

      // Commit the string
      keyEvent.inputContext()->commitString(commitStr);

      // Check if we need to move cursor left (e.g. for bracket pairs)
      Q9State state = logic_.getState();
      if (state.moveCursorLeft) {
        keyEvent.inputContext()->forwardKey(fcitx::Key(FcitxKey_Left));
      }
      logic_.clearCommitString();
      changed = true;
    }

    if (changed) {
      updateUIState();
    }
  }
}

void CustomEngine::updateUIState() {
  Q9State state = logic_.getState();

  std::cerr << "[CustomEngine] updateUIState: candidateMode="
            << state.candidateMode << " inputCode='" << state.inputCode << "'"
            << " relatedWords.size=" << state.relatedWords.size()
            << " pageCandidates.size=" << state.pageCandidates.size()
            << std::endl;

  if (state.candidateMode) {
    // Candidate mode - show text on buttons 1-9
    std::string cmd = "UPDATE_BUTTONS";
    for (size_t i = 0; i < state.pageCandidates.size(); ++i) {
      cmd += " " + std::to_string(i + 1) + ":" + state.pageCandidates[i] + "|";
    }
    // Clear remaining buttons
    for (size_t i = state.pageCandidates.size(); i < 9; ++i) {
      cmd += " " + std::to_string(i + 1) + ":|";
    }

    // Button 0: show "下頁" if multiple pages, else empty
    if (state.totalPages > 1) {
      cmd += " 0:下頁|";
    } else {
      cmd += " 0:|";
    }
    cmd += "10:取消|";

    std::cerr << "[CustomEngine] Sending: " << cmd << std::endl;
    sendToUI(cmd);

    // Send status text with page info
    if (state.totalPages > 1) {
      std::string status = "SET_STATUS " + state.statusPrefix + " " +
                           std::to_string(state.page + 1) + "/" +
                           std::to_string(state.totalPages) + "頁";
      sendToUI(status);
    } else if (!state.statusPrefix.empty()) {
      sendToUI("SET_STATUS " + state.statusPrefix);
    }
    lastUIStateWasBase_ = false;
  } else if (!state.inputCode.empty()) {
    // Input mode - show images based on input progress
    std::string cmd = "SET_IMAGES " + std::to_string(state.imageType);
    std::cerr << "[CustomEngine] Sending: " << cmd << std::endl;
    sendToUI(cmd);

    // Update button 0 and 10 text
    if (state.inputCode.length() == 1) {
      sendToUI("UPDATE_BUTTONS 0:選字|10:取消|");
    } else if (state.inputCode.length() == 2) {
      sendToUI("UPDATE_BUTTONS 0:選字|10:取消|");
    }

    // Show status
    if (!state.statusPrefix.empty()) {
      sendToUI("SET_STATUS 九万 " + state.statusPrefix);
    }
    lastUIStateWasBase_ = false;
  } else if (!state.relatedWords.empty()) {
    // Show related words with base images visible
    std::string cmd = "SET_RELATED";
    for (size_t i = 0; i < state.relatedWords.size() && i < 9; ++i) {
      cmd += " " + std::to_string(i + 1) + ":" + state.relatedWords[i] + "|";
    }
    cmd += " 0:標點|10:取消|";
    std::cerr << "[CustomEngine] Sending: " << cmd << std::endl;
    sendToUI(cmd);
    lastUIStateWasBase_ = false;
  } else {
    // Base state - reset to images (only if not already in base state)
    if (!lastUIStateWasBase_) {
      std::cerr << "[CustomEngine] Sending: RESET" << std::endl;
      sendToUI("RESET");
      sendToUI("SET_STATUS 九万");
      lastUIStateWasBase_ = true;
    }
  }
}

std::vector<fcitx::InputMethodEntry> CustomEngine::listInputMethods() {
  std::vector<fcitx::InputMethodEntry> entries;
  auto &entry = entries.emplace_back("tq9", "TQ9", "zh_HK", "tq9");
  // entry.setIcon("tq9");
  entry.setLabel("HK");
  return entries;
}

fcitx::AddonInstance *
CustomEngineFactory::create(fcitx::AddonManager *manager) {
  return new CustomEngine(manager->instance());
}
