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
  - `stt_prompt_off.txt` / `stt_prompt_only.txt` / `stt_prompt_both.txt` /
    `translate_prompt.txt`: the four default prompt templates (seed the
    user-editable copies) - one per 語音 translate mode, plus 譯 selection.
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
  under `~/.config/fcitx5/tq9/` (`stt.json`, `stt_usage.jsonl`, `stt_raw.jsonl`,
  the four `*prompt*.txt`, `recordings/`), never in the installed data dir.
- `src/ui/stt/AudioRecorder`: QAudioSource capture, PCM→WAV, energy-based speech
  detection, plus `TonePlayer` for the three beeps (start / stop / nothing heard).
- `src/ui/stt/GeminiClient`: multimodal `generateContent` call, JSON parsing and
  cost estimation from `usageMetadata`. `TQ9_GEMINI_ENDPOINT` overrides the URL
  for local testing.
- `src/ui/stt/SttRawLog`: the verbatim request/response pair for every call, one
  JSON object per line in `stt_raw.jsonl` - separate from the billing figures in
  `stt_usage.jsonl`, and the file to read when the model misbehaves. Written by
  `GeminiClient` itself, so a request that never reached the server is recorded
  too (`httpStatus: 0` plus `transportError`). Two things are not verbatim: the
  audio part is dropped from the logged request (it still goes to Gemini - three
  minutes of it is just megabytes of unreadable base64 on one line, so only
  `audioSeconds` is kept), and an unparseable reply becomes truncated
  `responseText` instead of a `response` object. The API key cannot appear - it
  travels in the `x-goog-api-key` header, not in the body or the URL. At 4 MB the
  log rolls over to `stt_raw.jsonl.1`, so it costs at most two of those.
- `src/ui/stt/SttController`: both flows — speech (hold ≥`holdThresholdMs` →
  record ≤3min → speech check → request → result) and translate-selection. One
  request in flight at a time, whichever kind. Also raises the every-N-USD spend
  alert, and dumps each take to `last_recording.wav` (overwritten every time,
  written before the speech check so rejected takes can be listened to). With
  `saveRecordings` on, the same WAV is also kept in `recordings/` named after
  the moment recording began (`yyyyMMdd-HHmmss.wav`) — written before the
  request, so a failed transcription still leaves the audio behind.
- `src/ui/stt/SettingsDialog`, `DialogSupport`: settings window; `DialogSupport`
  makes dialogs focusable under the process-wide Wayland layer-shell mode.

Speech input and text output are two separate fields. `speechLanguage` is what
the user says, `outputLanguage` is what should come out - set them differently
(say 廣東話口語 in, 繁體中文書面語 out) and the transcription is rewritten on
the way, with translation still off. An empty `outputLanguage` means "whatever
was spoken". `vocabulary` is a free-text list of names and jargon handed to the
model as a spelling hint.

There are four prompt files, one per job, so each can be tuned without
disturbing the others and none of them asks the model to pick a branch:

| file | used for | asks for |
| --- | --- | --- |
| `stt_prompt_off.txt` | 語音，翻譯「關」 | `{"original"}` |
| `stt_prompt_only.txt` | 語音，翻譯「開」 | `{"translation"}` |
| `stt_prompt_both.txt` | 語音，「同時輸出雙語」 | `{"original","translation"}` |
| `translate_prompt.txt` | 譯 selected text | `{"translation"}` |

The mode in the filename is the same string `stt.json` stores, and
`SttController` loads the file for the mode currently set. All four are seeded
at startup so they exist to be edited, and re-read from
`~/.config/fcitx5/tq9/` before every request, so edits take effect without a
rebuild.

Placeholders in the three speech prompts: `%context%`, `%speech_language%`,
`%output_language%`, `%vocabulary%`, `%translate_language%`, `%duration%` -
`_off` has no use for `%translate_language%`, `_only` none for
`%output_language%`. (`%target_language%` is kept as an alias for
`%speech_language%`, and `%translate_mode%` is still substituted, so prompt
files written before those changes keep working.) `translate_prompt.txt` takes
`%translate_language%` and `%text%`; `%text%` is substituted last so a selection
containing a placeholder cannot inject anything.

A user copy that lacks a placeholder the code now needs cannot honour the
setting behind it, so `loadSeededPrompt` archives it to a timestamped `.bak` and
re-seeds from the installed default rather than leaving it quietly stale
(checked: `%output_language%` in `_off`, `%translate_language%` in `_only` and
`_both`). The single pre-split `stt_prompt.txt` is retired the same way on
first use - its edits cannot be split across three files automatically, so the
`.bak` is left for the user to copy from.

Cost note: the Gemini API returns token counts only, never an amount. The token
figures in `stt_usage.jsonl` come straight from `usageMetadata`; the USD column
is a local conversion using the prices in `stt.json`, editable in the settings
window. `promptTokenCount` is the whole input with the audio already converted
to tokens and included, so there is one `inputTokens` figure and one input
price - no separate audio rate. Prices live per model under `models` (the model
name is the key), so switching model switches its pricing with it. 重設計費
archives the log to a timestamped `.bak` and zeroes `alertedUsd`.

Every secondary window (dialogs, message boxes, combo popups, tooltips) is a
layer surface because `main()` calls `useLayerShell()`, and a layer surface with
no `desiredSize` is stretched to fill the output. Hence `DialogSupport` sets a
desired size, the translate selector uses radio buttons instead of a combo box,
and the top-bar buttons carry no tooltips.

### STT IPC (added to the existing stdin/stdout protocol)
- Engine → UI: `STT_START`, `STT_STOP`, `STT_CONTEXT [base64]`,
  `TR_SELECTION [base64]`
- UI → Engine: `STT_ENABLED 0|1`, `STT_HOLD_MS <n>`, `STT_NEED_CONTEXT`,
  `TR_NEED_SELECTION`, `STT_PENDING`, `TR_PENDING after|replace`,
  `AI_RESULT <base64>`, `AI_ABORT`

`STT_PENDING` and `TR_PENDING` both raise the same `⏳` placeholder; the two
flows share `AI_RESULT` / `AI_ABORT` to take it down again.

The hold threshold is a setting (0.5–2.0s), so the engine cannot hard-code it —
`STT_HOLD_MS` is sent alongside `STT_ENABLED` whenever the settings change.

The engine times the `取消` long-press itself (short tap is still a plain
Cancel), supplies up to 200 chars of surrounding text cut at a punctuation
boundary, and shows `⏳` as preedit while waiting — falling back to
commit + `deleteSurroundingText` where preedit is unsupported.

For translate, the engine reads the selection from `surroundingText()`
(`cursor != anchor`). Anything committed while a selection is live replaces it,
which is what `TR_PENDING replace` wants; for `TR_PENDING after` (the default)
the engine forwards a Right key first, collapsing the selection to its
right-hand end so the translation lands after the original.

## Configuration (config.json)
- `window`: Default settings (width, height, constraints). Should not be modified at runtime.
- `storage`: Runtime persistent state (last position, current size). Updated by `ConfigLoader::save`.
- `system`: Runtime settings (numpad mode, output options).
- `buttons`: Layout definitions for the interface.
- `key` / `altkey`: Keycode mappings.

## Build & Scripts
- `build_package.sh`: Compiles the release version and packages it into a `.tar.gz`.
- `build_install.sh`: Compiles the release version and executes the installer(執行安裝) without creating a tarball.
- `debug_run.sh`: Debug build + hands it to KWin as the session's input method
  for the length of the run (see the gotchas below); needs no sudo.

### Debug session gotchas (KDE Plasma Wayland)
- **`FCITX_ADDON_DIRS` is not set by default**, so fcitx5 loads `libtq9.so` only
  from `/usr/lib/<triplet>/fcitx5`. A non-root `build_install.sh` puts it in
  `~/.local/lib/...` where nothing looks for it, so the *engine* silently stays
  at whatever is in `/usr` while `~/.local/bin` (which does win on PATH)
  supplies a newer *UI*. Half-old/half-new pairs like this look like features
  simply not working. Either install as root, or set `FCITX_ADDON_DIRS` in
  `~/.config/environment.d/`.
- **KWin, not you, must start the input method.** KWin hands the input-method
  protocol to the process it spawns *itself*, as an already-open file descriptor
  (the child's environment shows `WAYLAND_SOCKET=<fd>` beside the usual
  `WAYLAND_DISPLAY`). A `fcitx5 -r` started from a terminal has no such fd, logs
  `Using Wayland native input method protocol: 0` and never receives a single
  key: TQ9 appears in the tray and can be selected, but nothing reaches the
  engine so the floating UI never comes up. This is true of plain "Fcitx 5" as
  well - the "Fcitx 5 Wayland Launcher (Experimental)" entry is only a second
  reason for the same symptom.
  `debug_run.sh` therefore no longer launches fcitx5. It generates
  `install_debug/bin/fcitx5-debug` (a wrapper that sets the debug environment and
  `exec`s fcitx5, so the fd survives) plus a `tq9-debug.desktop`, points kwinrc's
  `[Wayland] InputMethod=` at that desktop file, and flips
  `org.kde.kwin.VirtualKeyboard.enabled` off and on over D-Bus to make KWin
  re-read it and respawn. The previous `InputMethod=` value is restored on exit -
  including the empty case, which means KDE Virtual Keyboard was set to None.
  The wrapper also redirects its own output to `fcitx5_debug.log`, since a
  KWin-spawned child's stdout would otherwise land in the journal.
- **Never `sudo ./debug_run.sh`.** A root fcitx5 does not get
  `org.fcitx.Fcitx5` on your session bus, so it dies at
  `Failed to create addon: dbus Unable to request dbus name` before anything
  useful loads, and the debug config dir plus recordings end up root-owned. The
  script refuses to start as root.

## Recent Changes
- Split the speech prompt into one file per translate mode
  (`stt_prompt_off/only/both.txt`); with `translate_prompt.txt` that makes four
  prompt files, each asking for exactly the JSON fields it needs.
- Rewrote `debug_run.sh`: it hands the debug build to KWin through kwinrc's
  `InputMethod=` instead of starting fcitx5 itself, which is the only way a debug
  instance can receive keys on a wayland session.
- Added `stt_raw.jsonl`: every Gemini request/response pair logged verbatim
  beside the existing usage log (audio redacted, key never present).
- STT settings gained: a separate 文字輸出 language beside 聲音輸入 (rewrites
  the transcription without translation), a 常用字眼 hint list, saved
  model+price sets keyed by model name, 譯文位置 (insert after / replace,
  default insert after), 保存每次錄音 plus a button that opens the folder, and
  a configurable 0.5-2.0s hold threshold. Audio input tokens are no longer
  billed separately - `promptTokenCount` already includes them.
- Added optional Gemini STT: taller top bar with 錄音/設定 buttons, hold-to-talk
  on `取消` or the record button, usage/cost logging and spend alerts.
- Created `build_install.sh` to streamline the build-and-install(建置與安裝) workflow.
- Fixed `ConfigLoader::save` to properly perform a read-modify-write operation, preserving all top-level JSON keys (like `buttons` and `key`) while updating `storage` and `system`.
- Added robust error handling in `ConfigLoader::save` to prevent file truncation on read failure.
