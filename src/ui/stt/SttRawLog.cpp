#include "SttRawLog.h"
#include "SttSettings.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>

namespace {

// Two of these are the most disk this aid should ever cost.
constexpr qint64 kMaxLogBytes = 4 * 1024 * 1024;
// A reply that cannot be parsed is kept as text, but not without a limit.
constexpr int kMaxResponseChars = 256 * 1024;

} // namespace

// The WAV is the whole point of not logging the body verbatim: three minutes
// of audio is megabytes of base64 on one line, and unreadable at that. It goes
// out to Gemini as always - it just never reaches this file. Everything else,
// the prompt above all, is kept exactly as it was sent.
QJsonObject SttRawLog::stripAudio(QJsonObject body) {
  QJsonArray contents = body["contents"].toArray();
  for (int i = 0; i < contents.size(); ++i) {
    QJsonObject content = contents[i].toObject();

    QJsonArray kept;
    const QJsonArray parts = content["parts"].toArray();
    for (const auto &part : parts) {
      if (!part.toObject().contains(QStringLiteral("inline_data")))
        kept.append(part);
    }

    content["parts"] = kept;
    contents[i] = content;
  }
  body["contents"] = contents;
  return body;
}

namespace {

// Keep the newest lines by moving the full log aside rather than trimming it
// in place; the previous generation stays available as .1 until the next roll.
void rollIfFull(const QString &path) {
  if (QFileInfo(path).size() < kMaxLogBytes)
    return;

  const QString previous = path + QStringLiteral(".1");
  QFile::remove(previous);
  if (!QFile::rename(path, previous))
    std::cerr << "[STT] Could not roll raw log to " << previous.toStdString()
              << std::endl;
}

} // namespace

void SttRawLog::append(const SttRawEntry &entry) {
  QJsonObject obj;
  obj["time"] = entry.time.toString(Qt::ISODate);
  obj["kind"] = entry.kind;
  obj["model"] = entry.model;
  obj["url"] = entry.url;
  obj["elapsedMs"] = entry.elapsedMs;
  obj["httpStatus"] = entry.httpStatus;
  if (!entry.transportError.isEmpty())
    obj["transportError"] = entry.transportError;
  if (entry.audioSeconds > 0.0)
    obj["audioSeconds"] = entry.audioSeconds;
  obj["request"] = entry.request;

  const QJsonDocument reply = QJsonDocument::fromJson(entry.response);
  if (reply.isObject())
    obj["response"] = reply.object();
  else
    obj["responseText"] =
        QString::fromUtf8(entry.response.left(kMaxResponseChars));

  const QString path = SttPaths::rawLogFile();
  rollIfFull(path);

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
    std::cerr << "[STT] Could not append raw log to " << path.toStdString()
              << std::endl;
    return;
  }
  file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
  file.write("\n");
  file.close();
}
