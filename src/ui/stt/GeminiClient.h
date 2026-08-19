#pragma once

#include "SttSettings.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

struct SttResult {
  bool ok = false; // reply parsed as the expected JSON shape
  QString original;
  QString translation;
  QString error;

  // Billing - recorded whether or not the reply was usable. Gemini's prompt
  // token count already covers the audio, so there is one input figure.
  int inputTokens = 0;
  int outputTokens = 0;
  double costUsd = 0.0;
  double audioSeconds = 0.0;
  QString model;
  QString rawText; // model's reply verbatim, for the log

  // Text to feed into the input context, per the translate mode.
  QString outputText(TranslateMode mode) const;
};

// One-shot multimodal call: prompt + WAV -> JSON transcription.
class GeminiClient : public QObject {
  Q_OBJECT

public:
  explicit GeminiClient(QObject *parent = nullptr);

  // Prompt + audio (speech to text).
  void send(const SttSettings &settings, const QString &prompt,
            const QByteArray &wav, double audioSeconds);
  // Prompt only (translate the current selection).
  void sendText(const SttSettings &settings, const QString &prompt);

  static QString fillPrompt(const QString &tmpl, const SttSettings &settings,
                            const QString &context, double audioSeconds);
  static QString fillTranslatePrompt(const QString &tmpl,
                                     const SttSettings &settings,
                                     const QString &text);

Q_SIGNALS:
  void finished(const SttResult &result);

private:
  QNetworkAccessManager *m_net;
};
