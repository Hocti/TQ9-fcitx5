#pragma once

#include <QDateTime>
#include <QString>

struct SttUsageEntry {
  QDateTime time;
  QString model;
  QString kind = QStringLiteral("stt"); // "stt" or "translate"
  bool ok = false; // request returned well-formed JSON
  QString error;   // set when !ok
  double audioSeconds = 0.0;
  int inputTokens = 0;
  int outputTokens = 0;
  double costUsd = 0.0;
  QString text; // what was produced (or the raw reply when !ok)
};

struct SttUsageTotals {
  double today = 0.0;
  double last30Days = 0.0;
  double lifetime = 0.0;
  int todayCalls = 0;
  int last30DaysCalls = 0;
};

// Append-only JSONL at ~/.config/fcitx5/tq9/stt_usage.jsonl. Every request is
// recorded, successful or not - only the output is withheld on failure.
class SttUsageLog {
public:
  static void append(const SttUsageEntry &entry);
  static SttUsageTotals totals();

  // Starts the counters over. The old log is renamed rather than deleted;
  // returns the archive path, or an empty string if there was nothing to move.
  static QString reset();
};
