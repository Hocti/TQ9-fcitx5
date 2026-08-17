#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <memory>

class QAudioSource;
class QAudioSink;
class QBuffer;
class QIODevice;

// Captures microphone audio into memory and hands back a WAV file.
class AudioRecorder : public QObject {
  Q_OBJECT

public:
  explicit AudioRecorder(QObject *parent = nullptr);
  ~AudioRecorder() override;

  bool start();
  // Stops capture and returns the recording as 16-bit mono PCM.
  QByteArray stop();

  bool isRecording() const { return m_recording; }
  double elapsedSeconds() const;
  int sampleRate() const { return m_outRate; }

  // Crude energy-based check for "did anyone actually say something".
  // Reports the fraction of frames above the noise floor via speechRatio.
  static bool hasSpeech(const QByteArray &pcm16Mono, int sampleRate,
                        double durationSeconds, double *speechRatio = nullptr);

  static QByteArray toWav(const QByteArray &pcm16Mono, int sampleRate);

private:
  void drainInput();

  std::unique_ptr<QAudioSource> m_source;
  QIODevice *m_input = nullptr; // owned by m_source
  QAudioFormat m_format;
  QByteArray m_raw;      // bytes exactly as the device produced them
  int m_outRate = 16000; // rate of the converted mono PCM
  bool m_recording = false;
  QElapsedTimer m_timer;
};

// Short generated sine beeps. Three distinct sounds: recording started,
// recording stopped with speech, recording stopped with nothing usable.
class TonePlayer : public QObject {
  Q_OBJECT

public:
  enum Tone {
    RecordStart,  // high blip
    RecordStop,   // low blip
    RecordNoop,   // low double buzz - nothing was heard / request failed
  };

  static TonePlayer *instance();
  ~TonePlayer() override;
  void play(Tone tone);

private:
  explicit TonePlayer(QObject *parent = nullptr);

  std::unique_ptr<QAudioSink> m_sink;
  std::unique_ptr<QBuffer> m_buffer;
  QByteArray m_pcm;
};
