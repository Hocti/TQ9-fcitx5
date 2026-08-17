#include "SttController.h"
#include "DialogSupport.h"
#include "SttUsageLog.h"

#include <QDateTime>
#include <QFile>
#include <QMessageBox>
#include <cmath>
#include <iostream>

namespace {
constexpr int kHoldThresholdMs = 500;
constexpr int kMaxRecordingMs = 3 * 60 * 1000;
constexpr int kTickIntervalMs = 100;
} // namespace

SttController::SttController(QObject *parent) : QObject(parent) {
  m_settings = SttSettingsIO::load();

  m_holdTimer.setSingleShot(true);
  m_holdTimer.setInterval(kHoldThresholdMs);
  connect(&m_holdTimer, &QTimer::timeout, this, &SttController::startNow);

  m_maxTimer.setSingleShot(true);
  m_maxTimer.setInterval(kMaxRecordingMs);
  connect(&m_maxTimer, &QTimer::timeout, this, &SttController::stopNow);

  m_tickTimer.setInterval(kTickIntervalMs);
  connect(&m_tickTimer, &QTimer::timeout, this, [this]() {
    Q_EMIT recordingTick(m_recorder.elapsedSeconds());
  });

  connect(&m_client, &GeminiClient::finished, this,
          &SttController::onReplyFinished);
}

void SttController::reloadSettings() {
  const bool wasUsable = m_settings.isUsable();
  m_settings = SttSettingsIO::load();
  if (m_settings.isUsable() != wasUsable)
    Q_EMIT enabledChanged(m_settings.isUsable());
}

void SttController::holdBegin() {
  if (!m_settings.enabled) {
    std::cerr << "[STT] hold ignored: STT is disabled in settings" << std::endl;
    return;
  }
  if (m_settings.apiKey.isEmpty()) {
    std::cerr << "[STT] hold ignored: no API key set" << std::endl;
    return;
  }
  if (m_recorder.isRecording()) {
    std::cerr << "[STT] hold ignored: already recording" << std::endl;
    return;
  }
  std::cerr << "[STT] hold started, recording in " << kHoldThresholdMs << "ms"
            << std::endl;
  m_holdTimer.start();
}

void SttController::holdEnd() {
  m_holdTimer.stop();
  if (m_recorder.isRecording())
    stopNow();
}

void SttController::startNow() {
  m_holdTimer.stop();

  if (!m_settings.isUsable()) {
    std::cerr << "[STT] start refused: enabled=" << m_settings.enabled
              << " apiKey=" << (m_settings.apiKey.isEmpty() ? "empty" : "set")
              << std::endl;
    return;
  }
  if (m_recorder.isRecording()) {
    std::cerr << "[STT] start refused: already recording" << std::endl;
    return;
  }

  // One request at a time - otherwise the two loading placeholders would be
  // indistinguishable in the text field.
  if (m_requestPending) {
    std::cerr << "[STT] start refused: a request is still in flight"
              << std::endl;
    TonePlayer::instance()->play(TonePlayer::RecordNoop);
    return;
  }

  m_context.clear();
  Q_EMIT needContext();

  if (!m_recorder.start()) {
    std::cerr << "[STT] start failed: could not open the microphone"
              << std::endl;
    TonePlayer::instance()->play(TonePlayer::RecordNoop);
    return;
  }

  std::cerr << "[STT] recording started" << std::endl;
  TonePlayer::instance()->play(TonePlayer::RecordStart);
  m_maxTimer.start();
  m_tickTimer.start();
  Q_EMIT recordingStarted();
  Q_EMIT recordingTick(0.0);
}

void SttController::stopNow() {
  if (!m_recorder.isRecording())
    return;

  m_maxTimer.stop();
  m_tickTimer.stop();
  onRecordingFinished();
  Q_EMIT recordingStopped();
}

void SttController::setContext(const QString &context) {
  m_context = context;
}

void SttController::onRecordingFinished() {
  const double seconds = m_recorder.elapsedSeconds();
  const QByteArray pcm = m_recorder.stop();
  const int rate = m_recorder.sampleRate();

  const QByteArray wav = AudioRecorder::toWav(pcm, rate);

  // Dev aid: keep the last take on disk (overwritten each time) so a bad
  // result can be listened back to. Written before the speech check so
  // rejected takes can be inspected too.
  QFile dump(SttPaths::lastRecordingFile());
  if (dump.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    dump.write(wav);
    dump.close();
  } else {
    std::cerr << "[STT] Could not write "
              << SttPaths::lastRecordingFile().toStdString() << std::endl;
  }

  double speechRatio = 0.0;
  if (!AudioRecorder::hasSpeech(pcm, rate, seconds, &speechRatio)) {
    std::cerr << "[STT] No speech detected (" << seconds << "s, ratio "
              << speechRatio << ") - not sending" << std::endl;
    TonePlayer::instance()->play(TonePlayer::RecordNoop);
    return;
  }

  std::cerr << "[STT] recording stopped: " << seconds << "s, speech ratio "
            << speechRatio << " - sending to " << m_settings.model.toStdString()
            << std::endl;
  TonePlayer::instance()->play(TonePlayer::RecordStop);

  const QString prompt = GeminiClient::fillPrompt(
      SttSettingsIO::loadPromptTemplate(), m_settings, m_context, seconds);

  m_requestPending = true;
  m_pendingKind = RequestKind::Speech;
  Q_EMIT requestPending();
  m_client.send(m_settings, prompt, wav, seconds);
}

void SttController::requestTranslation() {
  if (m_settings.apiKey.isEmpty())
    return;

  if (m_requestPending || m_recorder.isRecording()) {
    TonePlayer::instance()->play(TonePlayer::RecordNoop);
    return;
  }
  Q_EMIT needSelection();
}

void SttController::translateSelection(const QString &selection) {
  const QString text = selection.trimmed();
  if (text.isEmpty()) {
    // Nothing selected, or the app can't tell us what is selected.
    std::cerr << "[STT] Translate: no selection available" << std::endl;
    TonePlayer::instance()->play(TonePlayer::RecordNoop);
    return;
  }

  if (m_settings.apiKey.isEmpty() || m_requestPending)
    return;

  const QString prompt = GeminiClient::fillTranslatePrompt(
      SttSettingsIO::loadTranslatePromptTemplate(), m_settings, text);

  m_requestPending = true;
  m_pendingKind = RequestKind::Translate;
  Q_EMIT translatePending();
  m_client.sendText(m_settings, prompt);
}

void SttController::onReplyFinished(const SttResult &result) {
  const RequestKind kind = m_pendingKind;
  m_requestPending = false;
  m_pendingKind = RequestKind::None;

  // A translate request only ever wants the translated text.
  const QString produced =
      (kind == RequestKind::Translate)
          ? result.outputText(TranslateMode::Only)
          : result.outputText(m_settings.translateMode);

  SttUsageEntry entry;
  entry.time = QDateTime::currentDateTime();
  entry.model = result.model;
  entry.ok = result.ok;
  entry.error = result.error;
  entry.audioSeconds = result.audioSeconds;
  entry.textInputTokens = result.textInputTokens;
  entry.audioInputTokens = result.audioInputTokens;
  entry.outputTokens = result.outputTokens;
  entry.costUsd = result.costUsd;
  entry.kind = (kind == RequestKind::Translate) ? QStringLiteral("translate")
                                                : QStringLiteral("stt");
  entry.text = result.ok ? produced : result.rawText;
  SttUsageLog::append(entry);

  const SttUsageTotals totals = SttUsageLog::totals();

  if (!result.ok) {
    std::cerr << "[STT] Request failed: " << result.error.toStdString()
              << std::endl;
    TonePlayer::instance()->play(TonePlayer::RecordNoop);
    Q_EMIT requestAborted();
    maybeAlertOnSpend(totals.lifetime);
    return;
  }

  if (produced.trimmed().isEmpty()) {
    TonePlayer::instance()->play(TonePlayer::RecordNoop);
    Q_EMIT requestAborted();
  } else {
    Q_EMIT resultReady(produced);
  }

  maybeAlertOnSpend(totals.lifetime);
}

void SttController::maybeAlertOnSpend(double lifetimeCost) {
  const double step = m_settings.alertEveryUsd;
  if (step <= 0.0)
    return;

  const double crossed = std::floor(lifetimeCost / step) * step;
  if (crossed <= m_settings.alertedUsd + 1e-9)
    return;

  m_settings.alertedUsd = crossed;
  SttSettingsIO::save(m_settings);

  const SttUsageTotals totals = SttUsageLog::totals();
  QMessageBox box;
  box.setWindowTitle(QStringLiteral("九万 STT 費用提示"));
  box.setIcon(QMessageBox::Information);
  box.setText(
      QStringLiteral("Gemini STT 累計估算費用已超過 US$%1。")
          .arg(crossed, 0, 'f', 2));
  box.setInformativeText(
      QStringLiteral("累計：US$%1\n今日：US$%2\n過去 30 日：US$%3")
          .arg(lifetimeCost, 0, 'f', 4)
          .arg(totals.today, 0, 'f', 4)
          .arg(totals.last30Days, 0, 'f', 4));
  configureDialogForLayerShell(&box);
  box.exec();
}
