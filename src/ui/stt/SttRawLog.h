#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>

struct SttRawEntry {
  QDateTime time; // when the request went out; elapsedMs covers the rest
  QString kind = QStringLiteral("stt"); // "stt" or "translate"
  QString model;
  QString url;
  QJsonObject request; // body as sent, already through stripAudio()
  double audioSeconds = 0.0; // 0 for translate; the WAV itself is never logged
  int httpStatus = 0;  // 0 when the request never reached the server
  QString transportError; // Qt-level error, empty when HTTP answered
  QByteArray response;    // reply body verbatim
  qint64 elapsedMs = 0;
};

// Append-only JSONL at ~/.config/fcitx5/tq9/stt_raw.jsonl: the verbatim
// request/response pair for every Gemini call, kept apart from the billing
// figures in stt_usage.jsonl. A debugging aid - prompts and replies in full,
// so it is the file to look at when the model misbehaves.
//
// The API key never appears: it travels in the x-goog-api-key header, not in
// the body or the URL. The audio part is dropped from the request entirely -
// one three-minute take is megabytes of base64 on a single line, and none of it
// is readable anyway; `audioSeconds` records what was attached.
class SttRawLog {
public:
  static void append(const SttRawEntry &entry);

  // The body without its audio parts. Applied by the caller at send time so
  // the megabytes are gone before the entry is carried around waiting for the
  // reply.
  static QJsonObject stripAudio(QJsonObject body);
};
