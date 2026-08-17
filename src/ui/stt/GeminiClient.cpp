#include "GeminiClient.h"

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

// Pull the per-modality prompt token split out of usageMetadata; Gemini bills
// audio input at a different rate from text.
void readUsage(const QJsonObject &usage, SttResult &result) {
  const int promptTotal = usage["promptTokenCount"].toInt(0);
  result.outputTokens = usage["candidatesTokenCount"].toInt(0) +
                        usage["thoughtsTokenCount"].toInt(0);

  int audio = 0;
  int text = 0;
  const QJsonArray details = usage["promptTokensDetails"].toArray();
  for (const auto &value : details) {
    const QJsonObject detail = value.toObject();
    const QString modality = detail["modality"].toString();
    const int count = detail["tokenCount"].toInt(0);
    if (modality == QLatin1String("AUDIO"))
      audio += count;
    else
      text += count;
  }

  if (details.isEmpty()) {
    // No breakdown given - charge it all at the text rate.
    text = promptTotal;
  } else if (text + audio < promptTotal) {
    text += promptTotal - (text + audio);
  }

  result.textInputTokens = text;
  result.audioInputTokens = audio;
}

double computeCost(const SttSettings &settings, const SttResult &result) {
  return result.textInputTokens / 1e6 * settings.priceTextInput +
         result.audioInputTokens / 1e6 * settings.priceAudioInput +
         result.outputTokens / 1e6 * settings.priceOutput;
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

  QNetworkRequest request{QUrl(endpoint(settings.model))};
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/json"));
  request.setRawHeader("x-goog-api-key", settings.apiKey.toUtf8());
  request.setTransferTimeout(kRequestTimeoutMs);

  QNetworkReply *reply =
      m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

  connect(reply, &QNetworkReply::finished, this,
          [this, reply, settings, audioSeconds]() {
            reply->deleteLater();

            SttResult result;
            result.audioSeconds = audioSeconds;
            result.model = settings.model;

            const QByteArray payload = reply->readAll();
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
