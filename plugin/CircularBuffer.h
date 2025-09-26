#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <mutex>
#include <vector>
#include <optional>
#include <memory>
#include <span>
#include "AnalysisRegions.h"
#include "Comms.h"
#include "Types.h"

namespace audio_plugin {

class AnalysisRegions;

class WriteTracker {
public:
  WriteTracker(size_t positionLimit = 0);
  void recordWrite(const TimePoint& latestDataStart, int64_t latestDataOffset);
  bool haveWritten();
  size_t getNextWritePosition();
  size_t getLatestWritePosition();
  TimePoint getLatestDataTimePoint();

private:
  size_t positionLimit_{0};
  bool haveWritten_{false};
  size_t latestPosition_{0};
  TimePoint latestDataEnd_;
};

class MonoCircularBuffer {
public:
  MonoCircularBuffer(uint32_t bufferLengthMs, SampleRate sampleRate);
  void updateFrom(const std::vector<float>& srcBuffer,
                  const TimePoint& startTime);
  TimePoint getLatestSamples(std::vector<float>& dstBuffer);
  bool getSamples(const TimePoint& startTime, std::vector<float>& dstBuffer);
  bool getSamples(const TimePoint& startTime, std::span<float>& dstBuffer);
  uint32_t getDurationMs();
  size_t getNumStoredSamples();
  const SampleRate getSampleRate();

protected:
  std::mutex bufferMutex_;
  std::vector<float> buffer_;
  WriteTracker writeTracker_;
  SampleRate sampleRate_;
};

class Buff {
public:
  Buff(SampleRate srcSampleRate,
       uint16_t srcBlockSize,
       SampleRate targetSampleRate,
       std::shared_ptr<ServiceCommunicator> comms);

  void justStarted();
  void justStopped();

  void updateFrom(const juce::AudioBuffer<float>& srcBuffer,
                  const TimePoint& startTime);

  std::shared_ptr<MonoCircularBuffer> getCircularBuffer();
  std::shared_ptr<AnalysisRegions> getAnalysisRegions();
  SampleRate getBufferSampleRate();
  PlaybackRegion getPlaybackRegion();

private:
  float getMonoSample(const juce::AudioBuffer<float>& srcBuffer,
                      int sampleNumber);

  std::shared_ptr<AnalysisRegions> analysisRegions_;
  std::shared_ptr<MonoCircularBuffer> circBuff_;
  SampleRate srcSampleRate_;
  double downsampleRatio_;
  SampleRate buffSampleRate_;

  enum PlaybackState {
    IDLE = 0,
    JUST_STARTED,
    PLAYING,
    JUST_STOPPED
  } playbackState_{IDLE};

  std::optional<SampleCounter> lastUpdateEndPlayheadTime_;

  std::mutex playbackRegionMtx_;
  PlaybackRegion playbackRegion_;

  std::vector<float> latestBlockForResampling_;
  std::vector<float> latestResampledBlock_;
  std::vector<float> unconsumedSamples_;
  juce::LagrangeInterpolator interp_;
  std::optional<uint8_t> interpPrimingSamples_;

};

} // namespace audio_plugin
