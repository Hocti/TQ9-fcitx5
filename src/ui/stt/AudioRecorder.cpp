#include "AudioRecorder.h"

#include <QAudioSink>
#include <QAudioSource>
#include <QBuffer>
#include <QMediaDevices>
#include <QtEndian>
#include <QtMath>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr int kPreferredRate = 16000;

// Collapse whatever the device gave us into 16-bit mono at its own rate.
QByteArray toPcm16Mono(const QByteArray &raw, const QAudioFormat &format) {
  const int channels = qMax(1, format.channelCount());
  const int bytesPerSample = format.bytesPerSample();
  if (bytesPerSample <= 0)
    return QByteArray();

  const int frameBytes = bytesPerSample * channels;
  const qsizetype frames = raw.size() / frameBytes;

  QByteArray out;
  out.resize(frames * 2);
  auto *dst = reinterpret_cast<qint16 *>(out.data());
  const auto *src = reinterpret_cast<const uchar *>(raw.constData());

  for (qsizetype f = 0; f < frames; ++f) {
    double sum = 0.0;
    for (int c = 0; c < channels; ++c) {
      const uchar *p = src + (f * frameBytes) + (c * bytesPerSample);
      double v = 0.0;
      switch (format.sampleFormat()) {
      case QAudioFormat::UInt8:
        v = (static_cast<double>(*p) - 128.0) / 128.0;
        break;
      case QAudioFormat::Int16: {
        qint16 s;
        std::memcpy(&s, p, sizeof(s));
        v = static_cast<double>(s) / 32768.0;
        break;
      }
      case QAudioFormat::Int32: {
        qint32 s;
        std::memcpy(&s, p, sizeof(s));
        v = static_cast<double>(s) / 2147483648.0;
        break;
      }
      case QAudioFormat::Float: {
        float s;
        std::memcpy(&s, p, sizeof(s));
        v = static_cast<double>(s);
        break;
      }
      default:
        break;
      }
      sum += v;
    }
    double avg = sum / channels;
    avg = qBound(-1.0, avg, 1.0);
    dst[f] = static_cast<qint16>(avg * 32767.0);
  }

  return out;
}

// Nearest-neighbour resample; plenty for speech headed to an ASR model.
QByteArray resamplePcm16(const QByteArray &pcm, int fromRate, int toRate) {
  if (fromRate == toRate || fromRate <= 0 || toRate <= 0)
    return pcm;

  const qsizetype inFrames = pcm.size() / 2;
  const qsizetype outFrames =
      static_cast<qsizetype>(inFrames * static_cast<double>(toRate) / fromRate);
  if (outFrames <= 0)
    return QByteArray();

  QByteArray out;
  out.resize(outFrames * 2);
  const auto *src = reinterpret_cast<const qint16 *>(pcm.constData());
  auto *dst = reinterpret_cast<qint16 *>(out.data());

  for (qsizetype i = 0; i < outFrames; ++i) {
    qsizetype j = static_cast<qsizetype>(i * static_cast<double>(fromRate) / toRate);
    dst[i] = src[qMin(j, inFrames - 1)];
  }
  return out;
}

void appendLE16(QByteArray &out, quint16 value) {
  char buf[2];
  qToLittleEndian(value, buf);
  out.append(buf, 2);
}

void appendLE32(QByteArray &out, quint32 value) {
  char buf[4];
  qToLittleEndian(value, buf);
  out.append(buf, 4);
}

// Sine burst rendered as 16-bit mono, with a short fade to avoid clicks.
void appendTone(QByteArray &out, double freq, int ms, int rate,
                double amplitude) {
  const int samples = rate * ms / 1000;
  const int fade = qMin(samples / 4, rate / 200);
  for (int i = 0; i < samples; ++i) {
    double env = 1.0;
    if (i < fade)
      env = static_cast<double>(i) / fade;
    else if (i > samples - fade)
      env = static_cast<double>(samples - i) / fade;

    double v = qSin(2.0 * M_PI * freq * i / rate) * amplitude * env;
    auto s = static_cast<qint16>(qBound(-1.0, v, 1.0) * 32767.0);
    char buf[2];
    qToLittleEndian(s, buf);
    out.append(buf, 2);
  }
}

void appendSilence(QByteArray &out, int ms, int rate) {
  out.append(QByteArray(rate * ms / 1000 * 2, '\0'));
}

} // namespace

AudioRecorder::AudioRecorder(QObject *parent) : QObject(parent) {}

AudioRecorder::~AudioRecorder() = default;

bool AudioRecorder::start() {
  if (m_recording)
    return true;

  const QAudioDevice device = QMediaDevices::defaultAudioInput();
  if (device.isNull()) {
    std::cerr << "[STT] No audio input device available" << std::endl;
    return false;
  }

  QAudioFormat format;
  format.setSampleRate(kPreferredRate);
  format.setChannelCount(1);
  format.setSampleFormat(QAudioFormat::Int16);

  if (!device.isFormatSupported(format)) {
    format = device.preferredFormat();
    std::cerr << "[STT] 16k mono not supported, falling back to "
              << format.sampleRate() << "Hz x" << format.channelCount()
              << std::endl;
  }

  m_format = format;
  m_outRate = format.sampleRate();
  m_raw.clear();

  m_source = std::make_unique<QAudioSource>(device, format);
  m_input = m_source->start();
  if (!m_input) {
    std::cerr << "[STT] Failed to start audio input" << std::endl;
    m_source.reset();
    return false;
  }

  connect(m_input, &QIODevice::readyRead, this, &AudioRecorder::drainInput);
  m_recording = true;
  m_timer.start();
  return true;
}

void AudioRecorder::drainInput() {
  if (m_input)
    m_raw.append(m_input->readAll());
}

QByteArray AudioRecorder::stop() {
  if (!m_recording)
    return QByteArray();

  drainInput();
  m_recording = false;

  if (m_source) {
    m_source->stop();
    m_source.reset();
  }
  m_input = nullptr;

  QByteArray mono = toPcm16Mono(m_raw, m_format);
  m_raw.clear();

  // Standardise on 16 kHz when the device forced something else on us.
  if (m_format.sampleRate() != kPreferredRate) {
    mono = resamplePcm16(mono, m_format.sampleRate(), kPreferredRate);
    m_outRate = kPreferredRate;
  }
  return mono;
}

double AudioRecorder::elapsedSeconds() const {
  return m_recording ? m_timer.elapsed() / 1000.0 : 0.0;
}

bool AudioRecorder::hasSpeech(const QByteArray &pcm, int sampleRate,
                              double durationSeconds, double *speechRatio) {
  if (speechRatio)
    *speechRatio = 0.0;

  // Anything this short is a mis-press, not an utterance.
  if (durationSeconds < 0.4)
    return false;

  const qsizetype total = pcm.size() / 2;
  const int frameLen = qMax(1, sampleRate / 50); // 20 ms
  const qsizetype frameCount = total / frameLen;
  if (frameCount < 8)
    return false;

  const auto *samples = reinterpret_cast<const qint16 *>(pcm.constData());

  std::vector<double> rms;
  rms.reserve(frameCount);
  for (qsizetype f = 0; f < frameCount; ++f) {
    double sum = 0.0;
    for (int i = 0; i < frameLen; ++i) {
      const double s = samples[f * frameLen + i];
      sum += s * s;
    }
    rms.push_back(qSqrt(sum / frameLen));
  }

  // The 20th percentile stands in for the room's noise floor.
  std::vector<double> sorted = rms;
  std::sort(sorted.begin(), sorted.end());
  const double noiseFloor = sorted[sorted.size() / 5];
  const double threshold = qMax(noiseFloor * 3.0, 300.0);

  qsizetype voiced = 0;
  for (double v : rms) {
    if (v > threshold)
      voiced++;
  }

  const double ratio = static_cast<double>(voiced) / frameCount;
  if (speechRatio)
    *speechRatio = ratio;

  // At least ~160 ms of voiced frames, and not just one stray spike.
  return voiced >= 8 && ratio > 0.06;
}

QByteArray AudioRecorder::toWav(const QByteArray &pcm, int sampleRate) {
  const quint16 channels = 1;
  const quint16 bitsPerSample = 16;
  const quint32 byteRate = sampleRate * channels * bitsPerSample / 8;
  const quint16 blockAlign = channels * bitsPerSample / 8;

  QByteArray wav;
  wav.reserve(pcm.size() + 44);
  wav.append("RIFF", 4);
  appendLE32(wav, static_cast<quint32>(36 + pcm.size()));
  wav.append("WAVE", 4);
  wav.append("fmt ", 4);
  appendLE32(wav, 16);            // PCM header size
  appendLE16(wav, 1);             // format = PCM
  appendLE16(wav, channels);
  appendLE32(wav, static_cast<quint32>(sampleRate));
  appendLE32(wav, byteRate);
  appendLE16(wav, blockAlign);
  appendLE16(wav, bitsPerSample);
  wav.append("data", 4);
  appendLE32(wav, static_cast<quint32>(pcm.size()));
  wav.append(pcm);
  return wav;
}

TonePlayer::TonePlayer(QObject *parent) : QObject(parent) {}

TonePlayer::~TonePlayer() = default;

TonePlayer *TonePlayer::instance() {
  static TonePlayer *player = new TonePlayer();
  return player;
}

void TonePlayer::play(Tone tone) {
  const int rate = 44100;

  m_pcm.clear();
  switch (tone) {
  case RecordStart:
    appendTone(m_pcm, 1046.5, 110, rate, 0.35); // C6
    break;
  case RecordStop:
    appendTone(m_pcm, 523.25, 130, rate, 0.35); // C5
    break;
  case RecordNoop:
    appendTone(m_pcm, 233.08, 90, rate, 0.30); // Bb3, twice
    appendSilence(m_pcm, 60, rate);
    appendTone(m_pcm, 233.08, 90, rate, 0.30);
    break;
  }

  QAudioFormat format;
  format.setSampleRate(rate);
  format.setChannelCount(1);
  format.setSampleFormat(QAudioFormat::Int16);

  const QAudioDevice device = QMediaDevices::defaultAudioOutput();
  if (device.isNull())
    return;

  // Tear down any beep still playing - they are short enough that the newest
  // one always wins.
  if (m_sink)
    m_sink->stop();

  m_buffer = std::make_unique<QBuffer>();
  m_buffer->setData(m_pcm);
  m_buffer->open(QIODevice::ReadOnly);

  m_sink = std::make_unique<QAudioSink>(device, format);
  m_sink->start(m_buffer.get());
}
