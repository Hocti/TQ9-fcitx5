#include "GeminiClient.h"
#include "SttRawLog.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <iostream>

namespace {

constexpr int kRequestTimeoutMs = 90000;

QString endpoint(const QString &model) {
  // Dev hook: point the client at a local mock server.
  const QString override = qEnvironmentVariable("TQ9_GEMINI_ENDPOINT");
  if (!override.isEmpty())
    return override;

  return QStringLiteral(
             "https://generativelanguage.googleapis.com/v1beta/models/%1:"
             "generateContent")
      .arg(model);
}

// Gemini reports promptTokenCount as the whole input, audio already converted
// to tokens and included - so there is nothing to split out or add on top.
void readUsage(const QJsonObject &usage, SttResult &result) {
  result.inputTokens = usage["promptTokenCount"].toInt(0);
  result.outputTokens = usage["candidatesTokenCount"].toInt(0) +
                        usage["thoughtsTokenCount"].toInt(0);
}

double computeCost(const SttSettings &settings, const SttResult &result) {
  const ModelPricing price = settings.pricing();
  return result.inputTokens / 1e6 * price.input +
         result.outputTokens / 1e6 * price.output;
}

// The model occasionally wraps JSON in a ```json fence despite the mime type.
QString stripCodeFence(QString text) {
  text = text.trimmed();
  if (!text.startsWith(QLatin1String("```")))
    return text;

  const int firstNewline = text.indexOf('\n');
  if (firstNewline == -1)
    return text;
  text = text.mid(firstNewline + 1);
  if (text.endsWith(QLatin1String("```")))
    text.chop(3);
  return text.trimmed();
}

} // namespace

QString SttResult::outputText(TranslateMode mode) const {
  const QString orig = original.trimmed();
  const QString trans = translation.trimmed();

  switch (mode) {
  case TranslateMode::Only:
    return trans.isEmpty() ? orig : trans;

  case TranslateMode::Both:
    if (orig.isEmpty())
      return trans;
    if (trans.isEmpty())
      return orig;
    // A line break before, between and after the two versions.
    return QStringLiteral("\n%1\n%2\n").arg(orig, trans);

  case TranslateMode::Off:
  default:
    return orig.isEmpty() ? trans : orig;
  }
}

GeminiClient::GeminiClient(QObject *parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

QString GeminiClient::fillPrompt(const QString &tmpl,
                                 const SttSettings &settings,
                                 const QString &context, double audioSeconds) {
  QString out = tmpl;
  out.replace(QStringLiteral("%context%"), context);
  out.replace(QStringLiteral("%speech_language%"), settings.speechLanguage);
  // Older prompt files used this name for the same field.
  out.replace(QStringLiteral("%target_language%"), settings.speechLanguage);
  // Empty output language means "leave it in whatever was spoken".
  const QString output = settings.outputLanguage.trimmed();
  out.replace(QStringLiteral("%output_language%"),
              output.isEmpty()
                  ? QStringLiteral("the same language and register as spoken")
                  : output);
  const QString vocabulary = settings.vocabulary.trimmed();
  out.replace(QStringLiteral("%vocabulary%"),
              vocabulary.isEmpty() ? QStringLiteral("(none given)")
                                   : vocabulary);
  // Each mode has its own prompt file now, so nothing has to branch on this;
  // still substituted so a hand-edited file mentioning it stays readable.
  out.replace(QStringLiteral("%translate_mode%"),
              translateModeToString(settings.translateMode));
  out.replace(QStringLiteral("%translate_language%"),
              settings.translateLanguage);
  out.replace(QStringLiteral("%duration%"),
              QString::number(audioSeconds, 'f', 1));
  return out;
}

QString GeminiClient::fillTranslatePrompt(const QString &tmpl,
                                          const SttSettings &settings,
                                          const QString &text) {
  QString out = tmpl;
  out.replace(QStringLiteral("%translate_language%"),
              settings.translateLanguage);
  // Substituted last so text containing a placeholder can't inject anything.
  out.replace(QStringLiteral("%text%"), text);
  return out;
}

void GeminiClient::sendText(const SttSettings &settings,
                            const QString &prompt) {
  send(settings, prompt, QByteArray(), 0.0);
}

void GeminiClient::send(const SttSettings &settings, const QString &prompt,
                        const QByteArray &wav, double audioSeconds) {
  QJsonObject textPart;
  textPart["text"] = prompt;

  QJsonArray parts;
  parts.append(textPart);

  if (!wav.isEmpty()) {
    QJsonObject inlineData;
    inlineData["mime_type"] = QStringLiteral("audio/wav");
    inlineData["data"] = QString::fromLatin1(wav.toBase64());

    QJsonObject audioPart;
    audioPart["inline_data"] = inlineData;
    parts.append(audioPart);
  }

  QJsonObject content;
  content["parts"] = parts;

  QJsonObject generationConfig;
  generationConfig["responseMimeType"] = QStringLiteral("application/json");
  generationConfig["temperature"] = 0.2;

  QJsonObject body;
  body["contents"] = QJsonArray{content};
  body["generationConfig"] = generationConfig;

  const QString url = endpoint(settings.model);

  // Everything the raw log needs about the outgoing half, captured with the
  // audio already dropped so the entry stays small while it waits.
  SttRawEntry raw;
  raw.time = QDateTime::currentDateTime();
  raw.kind = wav.isEmpty() ? QStringLiteral("translate") : QStringLiteral("stt");
  raw.model = settings.model;
  raw.url = url;
  raw.audioSeconds = audioSeconds;
  raw.request = SttRawLog::stripAudio(body);

  QNetworkRequest request{QUrl(url)};
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/json"));
  request.setRawHeader("x-goog-api-key", settings.apiKey.toUtf8());
  request.setTransferTimeout(kRequestTimeoutMs);

  QElapsedTimer clock;
  clock.start();

  QNetworkReply *reply =
      m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

  connect(reply, &QNetworkReply::finished, this,
          [this, reply, settings, audioSeconds, raw, clock]() mutable {
            reply->deleteLater();

            SttResult result;
            result.audioSeconds = audioSeconds;
            result.model = settings.model;

            const QByteArray payload = reply->readAll();

            raw.httpStatus =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                    .toInt();
            if (reply->error() != QNetworkReply::NoError)
              raw.transportError = reply->errorString();
            raw.response = payload;
            raw.elapsedMs = clock.elapsed();
            SttRawLog::append(raw);

            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            const QJsonObject root = doc.object();

            if (reply->error() != QNetworkReply::NoError) {
              const QString apiMessage =
                  root["error"].toObject()["message"].toString();
              result.error = apiMessage.isEmpty() ? reply->errorString()
                                                  : apiMessage;
              result.rawText = QString::fromUtf8(payload.left(2000));
              // Usage may still be reported on a partial failure.
              readUsage(root["usageMetadata"].toObject(), result);
              result.costUsd = computeCost(settings, result);
              Q_EMIT finished(result);
              return;
            }

            readUsage(root["usageMetadata"].toObject(), result);
            result.costUsd = computeCost(settings, result);

            const QJsonArray candidates = root["candidates"].toArray();
            if (candidates.isEmpty()) {
              result.error = QStringLiteral("no candidates in reply");
              result.rawText = QString::fromUtf8(payload.left(2000));
              Q_EMIT finished(result);
              return;
            }

            QString text;
            const QJsonArray replyParts = candidates[0]
                                              .toObject()["content"]
                                              .toObject()["parts"]
                                              .toArray();
            for (const auto &part : replyParts)
              text += part.toObject()["text"].toString();

            result.rawText = text;

            const QJsonDocument inner =
                QJsonDocument::fromJson(stripCodeFence(text).toUtf8());
            if (!inner.isObject()) {
              result.error = QStringLiteral("reply was not valid JSON");
              Q_EMIT finished(result);
              return;
            }

            const QJsonObject obj = inner.object();
            if (!obj.contains(QStringLiteral("original")) &&
                !obj.contains(QStringLiteral("translation"))) {
              result.error =
                  QStringLiteral("JSON had neither original nor translation");
              Q_EMIT finished(result);
              return;
            }

            result.original = obj["original"].toString();
            result.translation = obj["translation"].toString();
            result.ok = true;
            Q_EMIT finished(result);
          });
}
