#pragma once

#include <auxe/auxe.h>
#include <QIODevice>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

// QAudioSink pulls PCM. Source position advances only for actual source frames.
class ProgressivePlaybackDevice final : public QIODevice {
public:
  ProgressivePlaybackDevice(std::shared_ptr<const auxProgressiveAudio> source, int repeat, QObject* parent)
      : QIODevice(parent), source_(std::move(source)), right_(aux_progressive_right_samples(*source_)), repeats_(repeat),
        lead_(std::max<qint64>(0, std::llround(source_->startMs * source_->sampleRate / 1000.0))) {
    open(ReadOnly | Unbuffered);
  }

  bool isSequential() const override { return true; }
  qint64 passFrames() const { return lead_ + (source_->canceled() ? source_->ready() : source_->totalFrames); }
  qint64 totalFrames() const { return passFrames() * repeats_; }
  qint64 bytesAvailable() const override {
    // A sequential sink may size its next transfer from bytesAvailable().
    // Advertise one bounded pull, including silence during starvation.
    const qint64 remaining = std::max<qint64>(0, totalFrames() - consumed.load());
    return bytesPerFrame() * std::min<qint64>(remaining, pullFrames());
  }
  bool atEnd() const override { return source_->finished() && bytesAvailable() == 0; }

  std::atomic<qint64> consumed{0}, requests{0}, episodes{0}, silence{0}, shortRequests{0};

protected:
  qint64 writeData(const char*, qint64) override { return -1; }
  qint64 readData(char* data, qint64 bytes) override {
    const qint64 pass = passFrames();
    qint64 pos = consumed.load();
    const qint64 count = std::min({bytes / bytesPerFrame(), pass * repeats_ - pos, static_cast<qint64>(pullFrames())});
    if (count <= 0 || !pass) return 0;
    ++requests;
    const qint64 ready = source_->ready();
    qint64 missing = 0;
    for (qint64 i = 0; i < count; ++i) {
      qint64 frame = pos % pass - lead_;
      qint16 samples[2]{};
      if (frame >= ready) {
        if (!starving_) ++episodes;
        starving_ = true;
        ++missing;
      } else {
        starving_ = false;
        if (frame >= 0) {
          samples[0] = pcm16(source_->samples()[frame]);
          if (right_) samples[1] = pcm16(right_[frame]);
        }
        ++pos;
      }
      std::memcpy(data + bytesPerFrame()*i, samples, bytesPerFrame());
    }
    if (missing) {
      ++shortRequests;
      silence += missing;
    }
    consumed = pos;
    return bytesPerFrame()*count;
  }
private:
  static qint16 pcm16(double sample) {
    return static_cast<qint16>(std::lrint(32767 * std::clamp(sample, -1.0, 1.0)));
  }
  int bytesPerFrame() const { return right_ ? 4 : 2; }
  int pullFrames() const { return std::max(1, source_->sampleRate / 10); }
  std::shared_ptr<const auxProgressiveAudio> source_;
  const double* right_;
  int repeats_;
  qint64 lead_;
  bool starving_ = false;
};
