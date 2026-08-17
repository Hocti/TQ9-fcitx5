# Q9 Linux UI

## Overview
A Qt-based floating window UI for the Q9 input method engine on Linux (supporting X11 and Wayland via LayerShell).

## Source Structure
- `src/`: Core logic and UI
  - `ConfigLoader.cpp` / `.h`: Handles JSON configuration loading and saving. Maintains a distinction between default window settings (`window`) and runtime state (`storage`).
  - `ui/`: UI components
    - `FloatingWindow.cpp` / `.h`: The main frameless, transparent overlay window. Also hosts the top-bar record/settings buttons and the recording overlay.
    - `CustomButton.cpp` / `.h`: Stylized buttons for the input interface.
    - `main.cpp`: Entry point, handles IPC via stdin/stdout.
    - `stt/`: Optional Gemini speech-to-text (see below).
- `data/`: Configuration and assets
  - `config.json`: Main configuration file.
  - `stt_prompt.txt` / `translate_prompt.txt`: Default prompt templates (seed the
    user-editable copies).
  - `img/`: Button assets.
  - `dataset.db`: SQLite database for the engine.

## STT (語音輸入)
Optional; off until the user enters a Gemini API key in the settings window
(⚙ button in the top bar). All of it lives in the UI process except key timing
and text insertion, which need the engine.

Top bar controls, left to right: 🎤 record (hold to talk), 譯 translate
selection, ⚙ settings. Record is shown only when STT is enabled *and* a key is
set; 譯 needs only a key (translating does not depend on the STT toggle);
⚙ is always shown. Hidden buttons are removed from the layout, not greyed out,
and the remaining ones slide left.

- `src/ui/stt/SttSettings`: settings struct + paths. Everything user-specific is
  under `~/.config/fcitx5/tq9/` (`stt.json`, `stt_usage.jsonl`, `stt_prompt.txt`),
  never in the installed data dir.
- `src/ui/stt/AudioRecorder`: QAudioSource capture, PCM→WAV, energy-based speech
  detection, plus `TonePlayer` for the three beeps (start / stop / nothing heard).
- `src/ui/stt/GeminiClient`: multimodal `generateContent` call, JSON parsing and
  cost estimation from `usageMetadata`. `TQ9_GEMINI_ENDPOINT` overrides the URL
  for local testing.
- `src/ui/stt/SttController`: both flows — speech (hold ≥0.5s → record ≤3min →
  speech check → request → result) and translate-selection. One request in
  flight at a time, whichever kind. Also raises the every-N-USD spend alert,
  and dumps each take to `last_recording.wav` (overwritten every time, written
  before the speech check so rejected takes can be listened to).
- `src/ui/stt/SettingsDialog`, `DialogSupport`: settings window; `DialogSupport`
  makes dialogs focusable under the process-wide Wayland layer-shell mode.

Prompt templates are re-read from `~/.config/fcitx5/tq9/` before every request,
so edits take effect without a rebuild. `stt_prompt.txt` placeholders:
`%context%`, `%speech_language%`, `%translate_mode%`, `%translate_language%`,
`%duration%`. (`%target_language%` is kept as an alias for `%speech_language%`
so prompt files seeded before the rename keep working.) `translate_prompt.txt`
takes `%translate_language%` and `%text%`; `%text%` is substituted last so a
selection containing a placeholder cannot inject anything.

Cost note: the Gemini API returns token counts only, never an amount. The token
figures in `stt_usage.jsonl` come straight from `usageMetadata`; the USD column
is a local conversion using the price table in `stt.json`, editable in the
settings window. 重設計費 archives the log to a timestamped `.bak` and zeroes
`alertedUsd`.

Every secondary window (dialogs, message boxes, combo popups, tooltips) is a
layer surface because `main()` calls `useLayerShell()`, and a layer surface with
no `desiredSize` is stretched to fill the output. Hence `DialogSupport` sets a
desired size, the translate selector uses radio buttons instead of a combo box,
and the top-bar buttons carry no tooltips.

### STT IPC (added to the existing stdin/stdout protocol)
- Engine → UI: `STT_START`, `STT_STOP`, `STT_CONTEXT [base64]`,
  `TR_SELECTION [base64]`
- UI → Engine: `STT_ENABLED 0|1`, `STT_NEED_CONTEXT`, `TR_NEED_SELECTION`,
  `STT_PENDING`, `TR_PENDING`, `AI_RESULT <base64>`, `AI_ABORT`

`STT_PENDING` and `TR_PENDING` both raise the same `⏳` placeholder; the two
flows share `AI_RESULT` / `AI_ABORT` to take it down again.

The engine times the `取消` long-press itself (short tap is still a plain
Cancel), supplies up to 200 chars of surrounding text cut at a punctuation
boundary, and shows `⏳` as preedit while waiting — falling back to
commit + `deleteSurroundingText` where preedit is unsupported.

For translate, the engine reads the selection from `surroundingText()`
(`cursor != anchor`), then on `TR_PENDING` forwards a Right key first: anything
committed while a selection is live would replace it, so the selection is
collapsed to its right-hand end and the translation lands after the original.

## Configuration (config.json)
- `window`: Default settings (width, height, constraints). Should not be modified at runtime.
- `storage`: Runtime persistent state (last position, current size). Updated by `ConfigLoader::save`.
- `system`: Runtime settings (numpad mode, output options).
- `buttons`: Layout definitions for the interface.
- `key` / `altkey`: Keycode mappings.

## Build & Scripts
- `build_package.sh`: Compiles the release version and packages it into a `.tar.gz`.
- `build_install.sh`: Compiles the release version and executes the installer(執行安裝) without creating a tarball.
- `debug_run.sh`: Orchestration script for debug builds and testing.

### Debug session gotchas (KDE Plasma Wayland)
- **`FCITX_ADDON_DIRS` is not set by default**, so fcitx5 loads `libtq9.so` only
  from `/usr/lib/<triplet>/fcitx5`. A non-root `build_install.sh` puts it in
  `~/.local/lib/...` where nothing looks for it, so the *engine* silently stays
  at whatever is in `/usr` while `~/.local/bin` (which does win on PATH)
  supplies a newer *UI*. Half-old/half-new pairs like this look like features
  simply not working. Either install as root, or set `FCITX_ADDON_DIRS` in
  `~/.config/environment.d/`.
- **KDE Virtual Keyboard must be plain "Fcitx 5"** for `debug_run.sh` to work.
  The "Fcitx 5 Wayland Launcher (Experimental)" entry runs
  `/usr/libexec/fcitx5-wayland-launcher`, which puts the input-method protocol
  on a private wayland socket only its own child can use; a manually started
  `fcitx5 -r` then logs `Using Wayland native input method protocol: 0` and
  receives no keys at all. `debug_run.sh` now detects this mode, warns before
  starting, and restores by re-running the launcher instead of plain fcitx5.
- `debug_run.sh` hands its environment to the debug instance via `env` and never
  exports it, so the restored system fcitx5 cannot inherit a stale `PATH` /
  `XDG_DATA_HOME`. It also runs fcitx5 with `-D` (not `-d`), otherwise the
  daemonised process detaches and `$!` refers to a PID that is already gone -
  which is why Ctrl+C used to leave the debug instance running.

## Recent Changes
- Added optional Gemini STT: taller top bar with 錄音/設定 buttons, hold-to-talk
  on `取消` or the record button, usage/cost logging and spend alerts.
- Created `build_install.sh` to streamline the build-and-install(建置與安裝) workflow.
- Fixed `ConfigLoader::save` to properly perform a read-modify-write operation, preserving all top-level JSON keys (like `buttons` and `key`) while updating `storage` and `system`.
- Added robust error handling in `ConfigLoader::save` to prevent file truncation on read failure.
