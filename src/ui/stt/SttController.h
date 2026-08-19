#pragma once

#include "AudioRecorder.h"
#include "GeminiClient.h"
#include "SttSettings.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>

// Owns the whole STT flow in the UI process:
//   hold (>=holdThresholdMs) -> record (<=3min) -> speech check -> Gemini
//   -> result
class SttController : public QObject {
  Q_OBJECT

public:
  explicit SttController(QObject *parent = nullptr);

  const SttSettings &settings() const { return m_settings; }
  void reloadSettings();

  bool isRecording() const { return m_recorder.isRecording(); }
  bool isBusy() const { return m_requestPending; }

  // A press-and-hold began / ended (record button or the 取消 key).
  void holdBegin();
  void holdEnd();

  // Start/stop immediately, skipping the 0.5s hold threshold. Used when the
  // engine has already done the timing for a held key.
  void startNow();
  void stopNow();

  // Preceding text supplied by the engine, in reply to needContext().
  void setContext(const QString &context);

  // Translate the user's current selection into the configured translate
  // language, leaving the original in place. Text supplied by the engine in
  // reply to needSelection().
  void requestTranslation();
  void translateSelection(const QString &selection);

Q_SIGNALS:
  void needContext();
  void needSelection();
  void recordingStarted();
  void recordingTick(double seconds);
  void recordingStopped();
  // Request is in flight - show the loading placeholder at the cursor.
  void requestPending();
  // Same, but for a translation - `replace` says whether the result should
  // overwrite the selection or be appended after it.
  void translatePending(bool replace);
  // Loading placeholder should be replaced with this text.
  void resultReady(const QString &text);
  // Loading placeholder should be removed with nothing put in its place.
  void requestAborted();
  void enabledChanged(bool enabled);

private:
  void onRecordingFinished();
  void onReplyFinished(const SttResult &result);
  void maybeAlertOnSpend(double lifetimeCost);

  SttSettings m_settings;
  AudioRecorder m_recorder;
  GeminiClient m_client;

  QTimer m_holdTimer;   // settings.holdThresholdMs before recording starts
  QTimer m_maxTimer;    // 3 min hard stop
  QTimer m_tickTimer;   // drives the on-screen elapsed counter
  QString m_context;
  // Names the kept copy of the take, so a failed request is still findable.
  QDateTime m_recordingStarted;

  // One request at a time, whichever kind - two loading placeholders in the
  // text would be indistinguishable.
  enum class RequestKind { None, Speech, Translate };
  RequestKind m_pendingKind = RequestKind::None;
  bool m_requestPending = false;
};
