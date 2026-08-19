#pragma once

#include <QMap>
#include <QString>

// Translation mode stored in stt.json ("translate.mode")
enum class TranslateMode {
  Off,  // 關 - only the transcription
  Only, // 開 - only the translation
  Both, // 同時輸出雙語
};

QString translateModeToString(TranslateMode mode);
TranslateMode translateModeFromString(const QString &value);

// Where the translation of a selection goes ("translate.insert").
enum class TranslateInsert {
  After,   // keep the original, put the translation after it (default)
  Replace, // overwrite the selection with the translation
};

QString translateInsertToString(TranslateInsert value);
TranslateInsert translateInsertFromString(const QString &value);

// USD per 1M tokens for one model. Gemini's promptTokenCount already includes
// the tokens the audio was turned into, so one input rate covers both.
struct ModelPricing {
  double input = 0.10;
  double output = 0.40;
};

// How long 取消 / the record button must be held before recording starts.
constexpr int kHoldThresholdMinMs = 500;
constexpr int kHoldThresholdMaxMs = 2000;

struct SttSettings {
  bool enabled = false;
  QString apiKey;
  // What the user speaks into the mic.
  QString speechLanguage = QStringLiteral("港式粵語，中英夾雜");
  // What the transcription should come out as. Setting a different language or
  // register here rewrites the speech even with translation off; empty means
  // "whatever was spoken".
  QString outputLanguage;
  // Names and jargon the user says often, given to the model as a hint so they
  // come back spelled the way they expect.
  QString vocabulary;

  TranslateMode translateMode = TranslateMode::Off;
  QString translateLanguage = QStringLiteral("English with cefr B2~C1 vocab");
  TranslateInsert translateInsert = TranslateInsert::After;

  // Keep every take in ~/.config/fcitx5/tq9/recordings, named after the moment
  // recording began - so a failed request still leaves the audio behind.
  bool saveRecordings = false;

  int holdThresholdMs = kHoldThresholdMinMs;

  // Show an alert every time the lifetime spend crosses another N USD.
  double alertEveryUsd = 10.0;
  // Highest multiple of alertEveryUsd already reported to the user.
  double alertedUsd = 0.0;

  QString model = QStringLiteral("gemini-3.5-flash-lite");
  // Saved model/price sets, keyed by model name, so switching model switches
  // its pricing with it.
  QMap<QString, ModelPricing> models;

  // Prices for the model currently selected (defaults when it has no entry).
  ModelPricing pricing() const { return pricingFor(model); }
  ModelPricing pricingFor(const QString &name) const;

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
  static QString rawLogFile();          // stt_raw.jsonl
  // Editable copy of the speech prompt for one translate mode:
  // stt_prompt_off.txt / stt_prompt_only.txt / stt_prompt_both.txt.
  static QString userPromptFile(TranslateMode mode);
  static QString translatePromptFile(); // translate_prompt.txt (editable copy)
  // Pre-split single prompt file, retired on first use.
  static QString legacyPromptFile();    // stt_prompt.txt
  // Dev aid: the most recent recording, overwritten every time.
  static QString lastRecordingFile();   // last_recording.wav
  // Kept takes (saveRecordings), created on demand.
  static QString recordingsDir();       // recordings/

  // Installed default prompts, e.g. /usr/share/fcitx5/tq9/.
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
  // A copy that predates a placeholder the code now needs is backed up and
  // re-seeded, otherwise the new setting would silently do nothing.
  // Each translate mode has its own file - the prompt says what to produce
  // instead of the model picking a branch out of one combined prompt.
  static QString loadPromptTemplate(TranslateMode mode);
  static QString loadTranslatePromptTemplate();
};
