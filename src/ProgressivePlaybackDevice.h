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
      : QIODevice(parent), source_(std::move(source)), repeats_(repeat),
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
    return 2 * std::min<qint64>(remaining, pullFrames());
  }
  bool atEnd() const override { return source_->finished() && bytesAvailable() == 0; }

  std::atomic<qint64> consumed{0}, requests{0}, episodes{0}, silence{0}, shortRequests{0};

protected:
  qint64 writeData(const char*, qint64) override { return -1; }
  qint64 readData(char* data, qint64 bytes) override {
    const qint64 pass = passFrames();
    qint64 pos = consumed.load();
    const qint64 count = std::min({bytes / 2, pass * repeats_ - pos, static_cast<qint64>(pullFrames())});
    if (count <= 0 || !pass) return 0;
    ++requests;
    const qint64 ready = source_->ready();
    qint64 missing = 0;
    for (qint64 i = 0; i < count; ++i) {
      qint64 frame = pos % pass - lead_;
      qint16 sample = 0;
      if (frame >= ready) {
        if (!starving_) ++episodes;
        starving_ = true;
        ++missing;
      } else {
        starving_ = false;
        if (frame >= 0)
          sample = static_cast<qint16>(std::lrint(32767 * std::clamp(source_->samples()[frame], -1.0, 1.0)));
        ++pos;
      }
      std::memcpy(data + 2*i, &sample, sizeof(sample));
    }
    if (missing) {
      ++shortRequests;
      silence += missing;
    }
    consumed = pos;
    return 2*count;
  }
private:
  int pullFrames() const { return std::max(1, source_->sampleRate / 10); }
  std::shared_ptr<const auxProgressiveAudio> source_;
  int repeats_;
  qint64 lead_;
  bool starving_ = false;
};
