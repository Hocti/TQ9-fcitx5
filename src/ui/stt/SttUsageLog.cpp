#include "SttUsageLog.h"
#include "SttSettings.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <iostream>

namespace {
// Keep single log lines from growing without bound.
constexpr int kMaxLoggedTextChars = 2000;
} // namespace

void SttUsageLog::append(const SttUsageEntry &entry) {
  QJsonObject obj;
  obj["time"] = entry.time.toString(Qt::ISODate);
  obj["model"] = entry.model;
  obj["kind"] = entry.kind;
  obj["ok"] = entry.ok;
  if (!entry.error.isEmpty())
    obj["error"] = entry.error;
  obj["audioSeconds"] = entry.audioSeconds;
  obj["textInputTokens"] = entry.textInputTokens;
  obj["audioInputTokens"] = entry.audioInputTokens;
  obj["outputTokens"] = entry.outputTokens;
  obj["costUsd"] = entry.costUsd;
  obj["text"] = entry.text.left(kMaxLoggedTextChars);

  QFile file(SttPaths::usageLogFile());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
    std::cerr << "[STT] Could not append usage log to "
              << SttPaths::usageLogFile().toStdString() << std::endl;
    return;
  }
  file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
  file.write("\n");
  file.close();
}

QString SttUsageLog::reset() {
  const QString path = SttPaths::usageLogFile();
  if (!QFile::exists(path))
    return QString();

  const QString archive =
      QStringLiteral("%1.%2.bak")
          .arg(path,
               QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));

  if (!QFile::rename(path, archive)) {
    std::cerr << "[STT] Could not archive usage log to "
              << archive.toStdString() << std::endl;
    return QString();
  }
  return archive;
}

SttUsageTotals SttUsageLog::totals() {
  SttUsageTotals totals;

  QFile file(SttPaths::usageLogFile());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return totals;

  const QDateTime now = QDateTime::currentDateTime();
  const QDate todayDate = now.date();
  const QDateTime cutoff30 = now.addDays(-30);

  QTextStream in(&file);
  while (!in.atEnd()) {
    const QByteArray line = in.readLine().toUtf8();
    if (line.trimmed().isEmpty())
      continue;

    QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject())
      continue;
    QJsonObject obj = doc.object();

    const double cost = obj["costUsd"].toDouble(0.0);
    totals.lifetime += cost;

    const QDateTime when =
        QDateTime::fromString(obj["time"].toString(), Qt::ISODate);
    if (!when.isValid())
      continue;

    if (when.date() == todayDate) {
      totals.today += cost;
      totals.todayCalls++;
    }
    if (when >= cutoff30) {
      totals.last30Days += cost;
      totals.last30DaysCalls++;
    }
  }

  return totals;
}
