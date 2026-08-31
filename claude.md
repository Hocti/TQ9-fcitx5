# Q9 Linux UI

## Overview
A Qt-based floating window UI for the Q9 input method engine on Linux (supporting X11 and Wayland via LayerShell).

## Source Structure
- `src/`: Core logic and UI
  - `Database.cpp` / `.h`: every read of the shipped `dataset.db` - the 字碼
    tables, 關聯字, 同音字 and 懶音字 (see below), 繁簡.
  - `Q9Logic.cpp` / `.h`: the state machine behind the keypad. `showHomoFor` /
    `showShortcutPage` / `showRelated` are the three lists a 長按 can open.
  - `CustomEngine.cpp` / `.h`: the fcitx5 addon - key events (including the
    長按 timers), the UI process and the IPC with it.
  - `UserDb.cpp` / `.h`: The user's own typing statistics (see 常用字調前
    below). Separate from `Database`, which only ever reads the shipped
    `dataset.db`.
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
  - `default90.png`: the 90 key images as one 9x10 grid - row 0 on top is
    `0_1`..`0_9`, row 9 at the bottom is `9_1`..`9_9`. `main.cpp` slices it at
    startup; any resolution works as long as it is a whole multiple of 9x10.
  - `dataset.db`: SQLite database for the engine. Read-only - on a root
    install it lives in `/usr/share` and could not be written anyway.

## 同音字 + 懶音字 (homophones, near and exact)
`Database::getHomo` returns the exact homophones first and the lazy-sound ones
after them, so the characters the user already picks never get pushed off the
first page.

- `exactHomo`: the rows whose `word_meta.ping` is identical, same tone first.
  This is the list 同音 has always shown; it now de-duplicates, since a
  character with several 字碼 has one row per code and the self-join multiplies
  them out again.
- `nearHomo`: the rows whose `ping` only matches once both sides have been
  through `fuzzyPing`, ordered by `MAX(freq)`, minus everything `exactHomo`
  already returned.

`fuzzyPing` works on the romanisation alone - **there is no per-character
table anywhere**, so a rule can be added or dropped without touching the word
list. It does two jobs in order:

1. Puts both romanisations on the same footing. `ping` is mostly Yale (`ji`,
   `yi`, `cheui`) with a few Jyutping strays (`zi`, `ci`, `ceoi`, `coek`);
   without this those would not even match as *exact* homophones.
   `z-`→`j-`, `c-`→`ch-`, `eoi/eon/eot`→`eui/eun/eut`, `oe`→`eu`.
2. The 懶音 rules proper: a dropped `ng-` (`ngo`↔`o`), `n-`/`l-` (`naa`↔`laa`),
   `gw-`/`g-` and `kw-`/`k-`, `aa`/`a`, `-ng`/`-n` (`sang`↔`san`), `-k`/`-t`
   (`baak`↔`baat`).

Bare `ng` and `m` (五, 唔) are syllabic nasals, not initials, so stripping them
would leave nothing behind; they are joined by a two-entry table at the end
instead, together with `o`/`a` so that 我 (`ngo`→`o`) reaches 啊 (`a`). A value
in that table must not itself be another key - it is looked up once, not
followed as a chain.

The `fuzzyPing(ping) -> pings` map is built from the ~700 distinct `ping`
values on first use and kept for the life of the process. A `dataset.db`
without a usable `ping` column simply yields no near homophones, leaving 同音
exactly as it was.

## 常用字調前 (frequency ordering)
Optional, on by default, toggled in the 選字 group of the settings window
(⚙ button). Everything happens in the engine; the UI only owns the checkbox.

What the user types is counted in `~/.local/share/fcitx5/tq9/user_stats.db`,
created on first use - never `dataset.db`. Two tables, both small enough to be
held in memory by `UserDb` and written through on every commit (WAL,
`synchronous=NORMAL`, so a commit never costs a disk flush):

| table | holds |
| --- | --- |
| `char_freq` | how often each character was typed |
| `pair_freq` | how often one character was typed straight after another |

A character counts only when it was picked off a list a code led to - the 選字表
or 同音. Picking off the 下個字 list (the 關聯 key), off 速選, or a bracket pair
is following a suggestion, and feeding that back would only entrench it.
Anything that is not a single Han character - punctuation included, which lives
outside the CJK ideograph blocks - is not counted either, and *ends* the run
rather than joining it, so no pair straddles it. Two characters form a pair only
when they were committed within `kPairMaxGapMs` (5s) of each other; past that
the user has moved on. `Q9Logic::reset()` (focus change, input thrown away)
breaks the run too.

Nothing is promoted below `UserDb::kMinCount` (2), so one stray selection never
reshuffles anything. Above it, two separate promotions:

- **選字表** (`promoteFrequent`): characters with a count move to the front of
  the *second* page, most-typed first. The first nine keep their order and their
  keys - that is what muscle memory knows, and shuffling it would cost more than
  it saves. Only the 選字表 and 同音 lists are touched.
- **下個字** (`relatedFor`): characters the user has typed after this one go
  straight to the front, first page included, and are *added* when the shipped
  `related_candidates_table` never had them.

Statistics are collected whether the setting is on or off - the setting only
decides whether the order may change - so switching it on takes effect at once
instead of starting from nothing.

The setting itself is `system.freq_order` in `config.json`, read by the engine
at startup beside `use_numpad`. The settings window edits it through
`ConfigLoader::save` and then sends `RELOAD_CONFIG`, so it also applies without
a restart. Like the window position, it only persists where `config.json` is
writable.

## 長按 (long press)
Four independently switchable long presses, all timed by the engine, all under
`system` in `config.json` (`InputConfig`), all edited in the 長按 group of the
settings window. `hold_ms` (0.15-1s, default 0.35s) is the threshold for the
keypad; 錄音 keeps its own, longer one (`holdThresholdMs`, 0.5-2s) because it
starts a recording and is meant to be harder to hit.

| when | key | tap | 長按 | setting |
| --- | --- | --- | --- | --- |
| 選字中 | 1~9 | 出字 | that candidate's 同音 list | `hold_homo` |
| 未打碼 | 0 | 標點 | 開關標點 (the bracket pairs) | `hold_openclose` |
| 未打碼 | 1~9 | 開始打碼 | that digit's 速選 page | `hold_shortcut` |
| any | 取消 | 取消 | 錄音 / 關聯字 / 速選 / 無效 | `cancel_hold` |

**A key with a long press acts on release, not on press** - until the key is
up there is no telling which of the two it was, and a character cannot be
un-committed. `holdActionFor` works this out when the key goes down and returns
`None` where the setting is off, in which case the key acts on press exactly as
it did before. Mid-code digits are always part of the code and never hold.

The long press runs the moment the threshold is reached, so the list appears
while the key is still down; the release is then spent. If it had nothing to
show - `showHomoFor` on a character with no homophones, `showShortcutPage` on
an id the dataset lacks - it changes nothing and reports so, and the release
goes on to do the ordinary thing rather than swallowing the keystroke.

One digit is held at a time (`heldNum_`). Any *other* key arriving first
settles it, so a rolled-over press cannot overtake the one before it, and a
press for the key already down is auto-repeat and ignored. A focus change or a
reset drops it without acting.

`cancel_hold` picks what holding 取消 does; a short tap is always a plain
Cancel. 錄音 needs STT configured and falls back to 無效 (long press does
nothing, tap still cancels) when it is not - it never silently turns into one
of the other actions.

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
  `AI_RESULT <base64>`, `AI_ABORT`, `RELOAD_CONFIG`

`RELOAD_CONFIG` is not an STT message: the settings window sends it after
writing `config.json` and the engine re-reads the `system` block from the file
itself, rather than the two sides duplicating a line per setting. Key mappings
and `use_numpad` are deliberately not re-applied - those are read once at
startup, and re-binding a key that is currently down would strand it.

`STT_PENDING` and `TR_PENDING` both raise the same `⏳` placeholder; the two
flows share `AI_RESULT` / `AI_ABORT` to take it down again.

The hold threshold is a setting (0.5–2.0s), so the engine cannot hard-code it —
`STT_HOLD_MS` is sent alongside `STT_ENABLED` whenever the settings change.

The engine times the `取消` long-press itself (short tap is still a plain
Cancel, and 錄音 is only one of the four things the hold can be set to - see
長按 above), supplies up to 200 chars of surrounding text cut at a punctuation
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
- `system`: Runtime settings - numpad mode, output options, and the engine's
  `InputConfig`: `freq_order`, `hold_homo`, `hold_openclose`, `hold_shortcut`,
  `hold_ms`, `cancel_hold` (`stt` | `relate` | `shortcut` | `none`).
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
- Added 懶音字: 同音 now lists the near homophones after the exact ones, matched
  by collapsing the romanisation (`fuzzyPing`) rather than by any hand-written
  character table.
- Added four long presses (選字中 1~9 → 同音, 未打碼 0 → 開關標點, 未打碼
  1~9 → 速選, and a choice of four things for 取消), each switchable in the new
  長按 group of the settings window. Keys that can hold now act on release.
- The settings window tells the engine to re-read `config.json`
  (`RELOAD_CONFIG`) instead of sending one line per setting.
- Added 常用字調前: the engine counts what the user types in a database of its
  own and uses it to reorder the second page of the 選字表 and the front of the
  下個字 list. Toggled in the new 選字 group of the settings window.
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
