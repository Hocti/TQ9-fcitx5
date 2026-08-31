#include "CustomEngine.h"
#include <QByteArray>
#include <QString>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

// Shown in the text field while we wait for Gemini to answer.
const char *kSttPlaceholder = "⏳";

// How much text before the cursor we hand to the model.
constexpr int kMaxContextChars = 200;

std::string encodeBase64(const QString &text) {
  return text.toUtf8().toBase64().toStdString();
}

std::string decodeBase64(const std::string &encoded) {
  return QByteArray::fromBase64(QByteArray::fromStdString(encoded))
      .toStdString();
}

// Trim the context to the last kMaxContextChars, starting at a sentence or
// line boundary so the model never sees half a word.
QString trimContext(const QString &text) {
  if (text.size() <= kMaxContextChars)
    return text;

  QString tail = text.right(kMaxContextChars);

  static const QString hardBreaks = QStringLiteral("\n\r。！？…；!?;");
  static const QString softBreaks = QStringLiteral("，、,:：");

  for (const QString &breaks : {hardBreaks, softBreaks}) {
    int idx = -1;
    for (int i = 0; i < tail.size(); ++i) {
      if (breaks.contains(tail.at(i))) {
        idx = i;
        break;
      }
    }
    // Only cut if it still leaves a useful amount of context.
    if (idx != -1 && idx < tail.size() - 20)
      return tail.mid(idx + 1).trimmed();
  }

  return tail;
}

} // namespace

CustomEngine::CustomEngine(fcitx::Instance *instance) : instance_(instance) {
  // Ensure the directory exists (legacy check, still valid)
  std::string userPkgData = fcitx::StandardPath::global().userDirectory(
      fcitx::StandardPath::Type::PkgData);
  std::string cmd = "mkdir -p " + userPkgData + "/tq9";
  system(cmd.c_str());

  // Load config for key mappings FIRST - we derive database path from config
  // location
  std::string cfgPath = configPath();

  if (!cfgPath.empty()) {
    // Derive database path from config path (same directory)
    std::string dataDir = cfgPath.substr(0, cfgPath.rfind('/'));
    std::string dbPath = dataDir + "/dataset.db";

    // What the user types is counted in a database of its own, in the user's
    // data dir: the shipped dataset.db sits in a read-only /usr on a root
    // install and must not be written to in any case.
    std::string userDbPath = userPkgData + "/tq9/user_stats.db";

    std::cerr << "[CustomEngine] Config path: " << cfgPath << std::endl;
    std::cerr << "[CustomEngine] Database path: " << dbPath << std::endl;
    std::cerr << "[CustomEngine] User stats path: " << userDbPath << std::endl;

    // init logic with correct database path
    if (!logic_.init(dbPath, userDbPath)) {
      std::cerr << "Logic DB Init Failed: " << dbPath << std::endl;
    } else {
      std::cerr << "[CustomEngine] Logic DB initialized successfully"
                << std::endl;
    }

    AppConfig config = ConfigLoader::load(QString::fromStdString(cfgPath));
    use_numpad_ = config.use_numpad;
    applyInputConfig(config.input);

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

std::string CustomEngine::configPath() const {
  return fcitx::StandardPath::global().locate(
      fcitx::StandardPath::Type::PkgData, "tq9/config.json");
}

// The settings window writes config.json and then tells us to pick it up, so
// the 選字 and 長按 settings apply without a restart. Key mappings and
// use_numpad are deliberately left alone: those are read once at startup, and
// re-binding keys under a key that is currently down would strand it.
void CustomEngine::reloadConfig() {
  const std::string path = configPath();
  if (path.empty())
    return;
  applyInputConfig(ConfigLoader::load(QString::fromStdString(path)).input);
}

void CustomEngine::applyInputConfig(const InputConfig &cfg) {
  input_ = cfg;
  keyHoldUsec_ = static_cast<uint64_t>(cfg.hold_ms) * 1000;
  logic_.setFrequencyOrder(cfg.freq_order);

  std::cerr << "[CustomEngine] input: freq_order=" << cfg.freq_order
            << " hold_homo=" << cfg.hold_homo
            << " hold_openclose=" << cfg.hold_openclose
            << " hold_shortcut=" << cfg.hold_shortcut
            << " hold_ms=" << cfg.hold_ms << " cancel_hold="
            << cancelHoldToString(cfg.cancel_hold).toStdString() << std::endl;
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
    std::string cfgPath = configPath();

    std::cerr << "[CustomEngine] Config Path: '" << cfgPath << "'" << std::endl;

    if (cfgPath.empty()) {
      std::cerr << "[CustomEngine] ERROR: Config file not found!" << std::endl;
    }

    sendToUI("INIT " + cfgPath);
  }
}

void CustomEngine::sendToUI(const std::string &cmd) {
  if (uiStdinFd_ == -1)
    return;
  std::string line = cmd + "\n";
  write(uiStdinFd_, line.c_str(), line.length());
}

void CustomEngine::handleUIOutput() {
  char buffer[4096];
  ssize_t n = read(uiStdoutFd_, buffer, sizeof(buffer));
  if (n > 0) {
    uiReadBuffer_.append(buffer, n);

    // STT results can be long and arrive split across reads, so accumulate
    // until we have complete lines.
    size_t pos;
    while ((pos = uiReadBuffer_.find('\n')) != std::string::npos) {
      std::string line = uiReadBuffer_.substr(0, pos);
      uiReadBuffer_.erase(0, pos + 1);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (!line.empty())
        handleUILine(line);
    }
  } else if (n == 0) {
    // EOF, child died
    uiPid_ = -1;
    uiStdinFd_ = -1;
    uiStdoutFd_ = -1;
    stdoutSource_.reset();
    uiReadBuffer_.clear();
  }
}

void CustomEngine::handleUILine(const std::string &line) {
  if (line.rfind("CLICK ", 0) == 0) {
    int id = 0;
    try {
      id = std::stoi(line.substr(6));
    } catch (const std::exception &) {
      return;
    }
    if (activeContext_) {
      if (id >= 0 && id <= 9)
        applyKey(id, activeContext_);
      else if (id == 10)
        applyCommand(Q9Key::Cancel, activeContext_);
    }
  } else if (line.rfind("FOCUS_TRUE", 0) == 0) {
    // UI has focus, do not hide.
    // Clear pending flag - we've received the response
    pendingFocusCheck_ = false;
  } else if (line.rfind("FOCUS_FALSE", 0) == 0) {
    // Only hide if we're still waiting for this response
    // This prevents race condition where the window was re-activated
    // between sending CHECK_FOCUS and receiving FOCUS_FALSE
    if (pendingFocusCheck_) {
      pendingFocusCheck_ = false;
      sendToUI("HIDE");
    }
  } else if (line.rfind("STT_ENABLED ", 0) == 0) {
    sttEnabled_ = (line.substr(12) == "1");
    std::cerr << "[CustomEngine] STT enabled=" << sttEnabled_ << std::endl;
  } else if (line.rfind("STT_HOLD_MS ", 0) == 0) {
    // How long 取消 must be held before recording starts, set by the user.
    const long ms = std::strtol(line.substr(12).c_str(), nullptr, 10);
    if (ms > 0) {
      sttHoldUsec_ = static_cast<uint64_t>(ms) * 1000;
      std::cerr << "[CustomEngine] STT hold threshold=" << ms << "ms"
                << std::endl;
    }
  } else if (line == "RELOAD_CONFIG") {
    // The settings window has just written config.json.
    reloadConfig();
  } else if (line == "STT_NEED_CONTEXT") {
    sendSurroundingTextToUI();
  } else if (line == "TR_NEED_SELECTION") {
    sendSelectionToUI();
  } else if (line == "STT_PENDING") {
    showSttPlaceholder();
  } else if (line == "TR_PENDING" || line.rfind("TR_PENDING ", 0) == 0) {
    // "TR_PENDING replace" overwrites the selection; anything else (including
    // the bare form) keeps it and puts the translation after it.
    showTranslatePlaceholder(line.size() > 11 && line.substr(11) == "replace");
  } else if (line == "AI_ABORT") {
    finishStt(std::string());
  } else if (line.rfind("AI_RESULT ", 0) == 0) {
    finishStt(decodeBase64(line.substr(10)));
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

  // Don't leave a push-to-talk, or a key waiting for its release, hanging if
  // focus moves away mid-press.
  dropHeldKey();
  cancelHoldTimer_.reset();
  translateCollapseTimer_.reset();
  cancelKeyDown_ = false;
  cancelFired_ = false;
  if (sttRecording_) {
    sttRecording_ = false;
    sendToUI("STT_STOP");
  }

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
  // Whatever the held key was going to do, it is not going to do it here.
  dropHeldKey();

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

bool CustomEngine::isCancelKey(const fcitx::Key &key) const {
  const int sym = key.sym();
  if (use_numpad_)
    return key.isKeyPad() && sym == FcitxKey_KP_Decimal;

  auto it = altKeyToCmd_.find(sym);
  return it != altKeyToCmd_.end() && it->second == Q9Key::Cancel;
}

// 錄音 is only on offer once STT is configured; with it off the key falls back
// to having no long press at all rather than to a different one.
CancelHold CustomEngine::cancelHoldAction() const {
  if (input_.cancel_hold == CancelHold::Stt && !sttEnabled_)
    return CancelHold::None;
  return input_.cancel_hold;
}

// Press-and-hold on 取消; a short tap is a plain Cancel either way.
void CustomEngine::handleCancelHold(fcitx::KeyEvent &keyEvent) {
  keyEvent.filterAndAccept();

  if (keyEvent.isRelease()) {
    if (!cancelKeyDown_)
      return;
    cancelKeyDown_ = false;
    cancelHoldTimer_.reset();

    if (sttRecording_) {
      std::cerr << "[CustomEngine] cancel released - stopping recording"
                << std::endl;
      sttRecording_ = false;
      sendToUI("STT_STOP");
    } else if (!cancelFired_) {
      // Released before the threshold, or the long press found nothing to
      // show - ordinary Cancel.
      std::cerr << "[CustomEngine] cancel tapped (short) - normal Cancel"
                << std::endl;
      applyCommand(Q9Key::Cancel, keyEvent.inputContext());
    }
    return;
  }

  // Auto-repeat sends press after press; only the first one starts the clock.
  if (cancelKeyDown_)
    return;

  cancelKeyDown_ = true;
  cancelFired_ = false;
  sttRecording_ = false;

  const CancelHold action = cancelHoldAction();
  // 錄音 keeps its own threshold (0.5-2s, set by the user): it starts a
  // recording and is meant to be harder to hit than an ordinary long press.
  const uint64_t hold =
      action == CancelHold::Stt ? sttHoldUsec_ : keyHoldUsec_;

  std::cerr << "[CustomEngine] cancel pressed - hold timer armed" << std::endl;
  uint64_t timeout = fcitx::now(CLOCK_MONOTONIC) + hold;
  cancelHoldTimer_ = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, timeout, 0,
      [this, action](fcitx::EventSourceTime *, uint64_t) {
        cancelHoldTimer_.reset();
        if (!cancelKeyDown_)
          return true;

        switch (action) {
        case CancelHold::Stt:
          std::cerr << "[CustomEngine] cancel held - sending STT_START"
                    << std::endl;
          sttRecording_ = true;
          cancelFired_ = true;
          sendToUI("STT_START");
          break;
        case CancelHold::Relate:
          cancelFired_ = logic_.showRelated();
          break;
        case CancelHold::Shortcut:
          cancelFired_ = logic_.showShortcutPage(0);
          break;
        case CancelHold::None:
          break;
        }

        // Neither 關聯字 nor 速選 commits anything; they only open a list.
        if (cancelFired_ && action != CancelHold::Stt)
          applyLogicResult(true, nullptr);
        return true;
      });
}

// ---- 長按 on 0-9 ------------------------------------------------------------

// What holding this digit would do right now. None means the key keeps acting
// on press, exactly as it did before long presses existed.
CustomEngine::HoldAction CustomEngine::holdActionFor(int num) const {
  // While picking a candidate: 同音 for that candidate. 0 is 下頁 and has no
  // long press of its own.
  if (logic_.inCandidateMode())
    return (num >= 1 && input_.hold_homo) ? HoldAction::Homo : HoldAction::None;

  // Only with nothing typed yet - mid-code every digit is part of the code.
  if (logic_.hasInputCode())
    return HoldAction::None;

  if (num == 0)
    return input_.hold_openclose ? HoldAction::OpenClose : HoldAction::None;
  return input_.hold_shortcut ? HoldAction::Shortcut : HoldAction::None;
}

void CustomEngine::armKeyHold(int num, HoldAction action) {
  heldNum_ = num;
  heldAction_ = action;
  heldFired_ = false;

  uint64_t timeout = fcitx::now(CLOCK_MONOTONIC) + keyHoldUsec_;
  keyHoldTimer_ = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, timeout, 0,
      [this](fcitx::EventSourceTime *, uint64_t) {
        keyHoldTimer_.reset();
        if (heldNum_ >= 0)
          heldFired_ = fireKeyHold();
        return true;
      });
}

// Runs when the hold threshold is reached. False means there was nothing to
// show, and the release goes on to do the ordinary thing - better than leaving
// the user holding a key that swallowed their keystroke.
bool CustomEngine::fireKeyHold() {
  bool ok = false;
  switch (heldAction_) {
  case HoldAction::Homo:
    ok = logic_.showHomoFor(heldNum_ - 1);
    break;
  case HoldAction::OpenClose:
    ok = logic_.processCommand(Q9Key::OpenClose);
    break;
  case HoldAction::Shortcut:
    ok = logic_.showShortcutPage(heldNum_);
    break;
  case HoldAction::None:
    break;
  }

  if (ok) {
    std::cerr << "[CustomEngine] key " << heldNum_ << " held" << std::endl;
    applyLogicResult(true, nullptr);
  }
  return ok;
}

void CustomEngine::flushHeldKey(fcitx::InputContext *ic) {
  if (heldNum_ < 0)
    return;

  const int num = heldNum_;
  const bool fired = heldFired_;
  dropHeldKey();

  // A press that never became a long one was a tap after all.
  if (!fired)
    applyKey(num, ic);
}

void CustomEngine::dropHeldKey() {
  keyHoldTimer_.reset();
  heldNum_ = -1;
  heldAction_ = HoldAction::None;
  heldFired_ = false;
}

// ---- key dispatch ----------------------------------------------------------

CustomEngine::KeyRole CustomEngine::resolveKey(const fcitx::Key &key, int &num,
                                               Q9Key &cmd) const {
  const int sym = key.sym();

  if (use_numpad_) {
    if (!key.isKeyPad())
      return KeyRole::None;

    if (sym >= FcitxKey_KP_0 && sym <= FcitxKey_KP_9) {
      num = sym - FcitxKey_KP_0;
      return KeyRole::Digit;
    }
    switch (sym) {
    case FcitxKey_KP_Decimal:
      cmd = Q9Key::Cancel;
      return KeyRole::Command;
    case FcitxKey_KP_Add:
      cmd = Q9Key::Relate;
      return KeyRole::Command;
    case FcitxKey_KP_Subtract:
      // 速選 when not picking, 上頁 when picking - applyCommand decides.
      cmd = Q9Key::Shortcut;
      return KeyRole::Command;
    case FcitxKey_KP_Multiply:
      cmd = Q9Key::Homo;
      return KeyRole::Command;
    case FcitxKey_KP_Divide:
      cmd = Q9Key::OpenClose;
      return KeyRole::Command;
    default:
      return KeyRole::None;
    }
  }

  // Alt key mode (non-numpad) - for keyboards without a numpad
  auto numIt = altKeyToNum_.find(sym);
  if (numIt != altKeyToNum_.end()) {
    num = numIt->second;
    return KeyRole::Digit;
  }

  auto cmdIt = altKeyToCmd_.find(sym);
  if (cmdIt != altKeyToCmd_.end()) {
    cmd = cmdIt->second;
    return KeyRole::Command;
  }

  // Block the other letter keys so they cannot type through the input method
  // (mirroring the C# behaviour for keyCode 65-90).
  if (sym >= 'a' && sym <= 'z')
    return KeyRole::Swallow;

  return KeyRole::None;
}

void CustomEngine::applyLogicResult(bool changed, fcitx::InputContext *ic) {
  if (!ic)
    ic = activeContext_;

  if (logic_.hasCommitString()) {
    if (ic) {
      ic->commitString(logic_.getCommitString());
      // A bracket pair leaves the cursor between its two halves.
      if (logic_.wantsCursorLeft())
        ic->forwardKey(fcitx::Key(FcitxKey_Left));
    }
    logic_.clearCommitString();
    changed = true;
  }

  if (changed)
    updateUIState();
}

void CustomEngine::applyKey(int num, fcitx::InputContext *ic) {
  applyLogicResult(logic_.processKey(num), ic);
}

void CustomEngine::applyCommand(Q9Key cmd, fcitx::InputContext *ic) {
  // 速選 and 上頁 share one key; which one it is depends on the state now.
  if (cmd == Q9Key::Shortcut && logic_.inCandidateMode())
    cmd = Q9Key::PrevPage;
  applyLogicResult(logic_.processCommand(cmd), ic);
}

void CustomEngine::sendSurroundingTextToUI() {
  QString context;

  if (activeContext_ &&
      activeContext_->capabilityFlags().test(
          fcitx::CapabilityFlag::SurroundingText)) {
    const auto &surrounding = activeContext_->surroundingText();
    if (surrounding.isValid()) {
      const QString all = QString::fromStdString(surrounding.text());
      const int cursor =
          std::min<int>(surrounding.cursor(), static_cast<int>(all.size()));
      context = trimContext(all.left(cursor));
    }
  }

  if (context.isEmpty()) {
    sendToUI("STT_CONTEXT");
  } else {
    sendToUI("STT_CONTEXT " + encodeBase64(context));
  }
}

void CustomEngine::sendSelectionToUI() {
  QString selection;

  if (activeContext_ &&
      activeContext_->capabilityFlags().test(
          fcitx::CapabilityFlag::SurroundingText)) {
    const auto &surrounding = activeContext_->surroundingText();
    // cursor != anchor means there is a selection between the two offsets.
    if (surrounding.isValid() && surrounding.cursor() != surrounding.anchor()) {
      const QString all = QString::fromStdString(surrounding.text());
      int from = static_cast<int>(
          std::min(surrounding.cursor(), surrounding.anchor()));
      int to = static_cast<int>(
          std::max(surrounding.cursor(), surrounding.anchor()));
      from = std::clamp(from, 0, static_cast<int>(all.size()));
      to = std::clamp(to, from, static_cast<int>(all.size()));
      selection = all.mid(from, to - from);
    }
  }

  if (selection.isEmpty()) {
    sendToUI("TR_SELECTION");
  } else {
    sendToUI("TR_SELECTION " + encodeBase64(selection));
  }
}

void CustomEngine::showTranslatePlaceholder(bool replace) {
  if (!activeContext_)
    return;

  // Anything committed while a selection is active replaces it, which is
  // exactly what "replace" wants.
  if (replace) {
    showSttPlaceholder();
    return;
  }

  // To keep the original, the selection has to be dropped first and the
  // caret parked at its right-hand end - otherwise the placeholder itself
  // eats the selected text and the translation ends up replacing it. Right
  // arrow does exactly that, but the client only moves the caret once it has
  // handled the event, so the placeholder waits for that to land.
  activeContext_->forwardKey(fcitx::Key(FcitxKey_Right), false);
  activeContext_->forwardKey(fcitx::Key(FcitxKey_Right), true);

  translateCollapseWaits_ = 0;
  waitForSelectionCollapse();
}

// True only when the client reports a live selection; a client without
// surrounding text support can't tell us, so it counts as collapsed.
bool CustomEngine::hasSelection(fcitx::InputContext *ic) const {
  if (!ic || !ic->capabilityFlags().test(fcitx::CapabilityFlag::SurroundingText))
    return false;

  const auto &surrounding = ic->surroundingText();
  return surrounding.isValid() && surrounding.cursor() != surrounding.anchor();
}

// Re-checks every 40ms, up to ~320ms, then shows the placeholder regardless.
// Only ever forwards the one Right key - re-sending it would push the caret
// past the end of the original when the report is merely stale.
void CustomEngine::waitForSelectionCollapse() {
  fcitx::InputContext *ic = activeContext_;
  uint64_t timeout = fcitx::now(CLOCK_MONOTONIC) + 40000; // 40ms
  translateCollapseTimer_ = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, timeout, 0,
      [this, ic](fcitx::EventSourceTime *, uint64_t) {
        translateCollapseTimer_.reset();
        if (activeContext_ != ic)
          return true;
        if (hasSelection(ic) && ++translateCollapseWaits_ < 8) {
          waitForSelectionCollapse();
          return true;
        }
        showSttPlaceholder();
        return true;
      });
}

void CustomEngine::showSttPlaceholder() {
  sttContext_ = activeContext_;
  if (!sttContext_)
    return;

  if (sttContext_->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
    fcitx::Text preedit(kSttPlaceholder);
    preedit.setCursor(0);
    sttContext_->inputPanel().setClientPreedit(preedit);
    sttContext_->updatePreedit();
    sttPreeditShown_ = true;
  } else {
    // No preedit support: commit the marker and delete it again later.
    sttContext_->commitString(kSttPlaceholder);
    sttPlaceholderCommitted_ = true;
  }
}

void CustomEngine::clearSttPlaceholder() {
  if (!sttContext_)
    return;

  if (sttPreeditShown_) {
    sttContext_->inputPanel().setClientPreedit(fcitx::Text());
    sttContext_->updatePreedit();
    sttPreeditShown_ = false;
  }

  if (sttPlaceholderCommitted_) {
    if (sttContext_->capabilityFlags().test(
            fcitx::CapabilityFlag::SurroundingText)) {
      // kSttPlaceholder is a single codepoint.
      sttContext_->deleteSurroundingText(-1, 1);
    }
    sttPlaceholderCommitted_ = false;
  }
}

void CustomEngine::finishStt(const std::string &text) {
  // A reply that beats the collapse wait must not leave the timer to plant a
  // placeholder nobody will clear.
  translateCollapseTimer_.reset();
  clearSttPlaceholder();

  if (!text.empty() && sttContext_)
    sttContext_->commitString(text);

  sttContext_ = nullptr;
}

void CustomEngine::keyEvent(const fcitx::InputMethodEntry &entry,
                            fcitx::KeyEvent &keyEvent) {
  auto key = keyEvent.key();

  // 取消 needs both edges of the key whenever holding it means something.
  if (isCancelKey(key) && cancelHoldAction() != CancelHold::None) {
    // A digit still waiting for its release is settled first, so its character
    // cannot land after the Cancel that was meant to follow it.
    if (!keyEvent.isRelease())
      flushHeldKey(keyEvent.inputContext());
    handleCancelHold(keyEvent);
    return;
  }

  int num = -1;
  Q9Key cmd = Q9Key::Cancel;
  const KeyRole role = resolveKey(key, num, cmd);
  if (role == KeyRole::None)
    return;

  // Both edges of a key we take are swallowed, so the client never sees half
  // of one - the press of a long-pressable digit does nothing by itself.
  keyEvent.filterAndAccept();

  if (keyEvent.isRelease()) {
    if (role == KeyRole::Digit && num == heldNum_)
      flushHeldKey(keyEvent.inputContext());
    return;
  }

  // Auto-repeat sends press after press; the first one has been taken already.
  if (role == KeyRole::Digit && num == heldNum_)
    return;

  // Any other key settles whatever is still down first, so a rolled-over
  // press cannot overtake the one before it.
  flushHeldKey(keyEvent.inputContext());

  switch (role) {
  case KeyRole::Swallow:
    return;
  case KeyRole::Command:
    applyCommand(cmd, keyEvent.inputContext());
    return;
  case KeyRole::Digit:
    break;
  default:
    return;
  }

  const HoldAction hold = holdActionFor(num);
  if (hold == HoldAction::None) {
    applyKey(num, keyEvent.inputContext());
    return;
  }

  // The key can still turn into a long press, so its ordinary action waits for
  // the release: a candidate must not be committed before we know which it is.
  armKeyHold(num, hold);
}

void CustomEngine::updateUIState() {
  Q9State state = logic_.getState();

  std::cerr << "[CustomEngine] updateUIState: candidateMode="
            << state.candidateMode << " inputCode='" << state.inputCode << "'"
            << " relatedWords.size=" << state.relatedWords.size()
            << " pageCandidates.size=" << state.pageCandidates.size()
            << " state.statusPrefix='" << state.statusPrefix << "'"
            << " state.page=" << state.page
            << " state.totalPages=" << state.totalPages << std::endl;

  // Every branch below feeds this one SET_STATUS at the end: sending it
  // unconditionally is what clears a prefix the logic has already dropped
  // (e.g. toggling [同音] off while the base state is already on screen).
  std::string status = state.statusPrefix.empty() ? "三三" : state.statusPrefix;

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

    // Status text with page info
    if (state.totalPages > 1) {
      std::string pageInfo = std::to_string(state.page + 1) + "/" +
                             std::to_string(state.totalPages) + "頁";
      status = state.statusPrefix.empty() ? pageInfo
                                          : state.statusPrefix + " " + pageInfo;
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
      status = "三三" + state.statusPrefix;
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
      lastUIStateWasBase_ = true;
    }
  }

  sendToUI("SET_STATUS " + status);
}

std::vector<fcitx::InputMethodEntry> CustomEngine::listInputMethods() {
  std::vector<fcitx::InputMethodEntry> entries;
  auto &entry = entries.emplace_back("tq9", "TQ9", "zh_HK", "tq9");
  entry.setLabel("HK");
  return entries;
}

fcitx::AddonInstance *
CustomEngineFactory::create(fcitx::AddonManager *manager) {
  return new CustomEngine(manager->instance());
}
