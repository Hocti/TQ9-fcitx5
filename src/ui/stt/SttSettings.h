#pragma once

#include <QString>

// Translation mode stored in stt.json ("translate.mode")
enum class TranslateMode {
  Off,  // 關 - only the transcription
  Only, // 開 - only the translation
  Both, // 同時輸出雙語
};

QString translateModeToString(TranslateMode mode);
TranslateMode translateModeFromString(const QString &value);

struct SttSettings {
  bool enabled = false;
  QString apiKey;
  // Language the user actually speaks into the mic.
  QString speechLanguage = QStringLiteral("港式粵語，中英夾雜");

  TranslateMode translateMode = TranslateMode::Off;
  QString translateLanguage = QStringLiteral("English with cefr B2~C1 vocab");

  // Show an alert every time the lifetime spend crosses another N USD.
  double alertEveryUsd = 10.0;
  // Highest multiple of alertEveryUsd already reported to the user.
  double alertedUsd = 0.0;

  QString model = QStringLiteral("gemini-3.5-flash-lite");

  // USD per 1M tokens - kept in the file so pricing changes need no rebuild.
  double priceTextInput = 0.10;
  double priceAudioInput = 0.30;
  double priceOutput = 0.40;

  bool isUsable() const { return enabled && !apiKey.isEmpty(); }
};

// All STT state lives in the user's config dir, never in the (possibly
// read-only, possibly root-owned) installed data dir.
class SttPaths {
public:
  // ~/.config/fcitx5/tq9
  static QString configDir();
  static QString settingsFile();   // stt.json
  static QString usageLogFile();        // stt_usage.jsonl
  static QString userPromptFile();      // stt_prompt.txt (editable copy)
  static QString translatePromptFile(); // translate_prompt.txt (editable copy)
  // Dev aid: the most recent recording, overwritten every time.
  static QString lastRecordingFile();   // last_recording.wav

  // Installed default prompt, e.g. /usr/share/fcitx5/tq9/stt_prompt.txt.
  // Set once at startup from the INIT config path.
  static void setDataDir(const QString &dir);
  static QString dataDir();
};

class SttSettingsIO {
public:
  static SttSettings load();
  static void save(const SttSettings &settings);

  // Reads the user prompt templates, seeding them from the installed defaults
  // the first time. Re-read before every request so edits take effect live.
  static QString loadPromptTemplate();
  static QString loadTranslatePromptTemplate();
};
