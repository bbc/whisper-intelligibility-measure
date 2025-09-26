#pragma once

#include <juce_events/juce_events.h>
#include <mutex>
#include <set>
#include <optional>
#include <vector>
#include <map>
#include <memory>
#include <atomic>
#include "CircularBuffer.h"
#include "Comms.h"
#include "Types.h"

namespace audio_plugin {

class MonoCircularBuffer;
class ServiceCommunicator;

struct Region {
  Region() {}
  // ctor to support std::set
  Region(TimePoint startTime,
         TimePoint endTime,
         uint16_t counter,
         bool duringPlayback)
      : start(startTime),
        end(endTime),
        count(counter),
        wasDuringPlayback(duringPlayback) {}
  enum State { 
    PENDING,        // Region added - no other action taken
    IN_PROGRESS,    // Region has been sent for analysis
    COMPLETE,       // Region result received
    TIMEOUT,        // Region result didn't return within time limit
    FAILURE,        // Region analysis failed
  };
  TimePoint start;
  TimePoint end;
  uint16_t count{0};
  bool wasDuringPlayback{false};
  // This struct is stored in a set which is iterated by const
  // so need to mark non-order-changing members as mutable
  mutable State analysisState{PENDING};
  mutable bool stale{false};
  mutable float analysisResult{0.f};
  bool operator<(const Region& other) const {
    return start.sampleCounter <
            other.start.sampleCounter;  // Sorting for quick search
  }
};

class PlaybackResults {
public:
  struct Results {
    std::set<PlayheadTime> playheadStartTimes;
    std::set<SampleCounter> playthroughOffsets;
    std::map<SampleCounter, std::map<PlayheadTime, Region>> regions;
  };

  void addResult(const Region& resultantRegion);
  Results getResults();
  uint64_t getUpdateCounter();
  void setConfigFromRegion(PlayheadTime aligningRegionStart,
                           PlayheadTime aligningRegionEnd,
                           SampleCounter regionFrequency);
  void setConfig(SampleCounter alignmentOffset,
                 SampleCounter regionSize,
                 SampleCounter regionFrequency);
  void clear();

private:
  std::atomic<uint64_t> updateCounter_{0};
  std::atomic<SampleCounter> alignmentOffset_{0};
  std::atomic<SampleCounter> regionSize_{0};
  std::atomic<SampleCounter> regionFrequency_{0};
  std::mutex resultsLock_;
  Results results_;
};

class AnalysisRegions : private juce::Timer {
public:
  AnalysisRegions(std::shared_ptr<MonoCircularBuffer> readBuff,
                  std::shared_ptr<ServiceCommunicator> comms);
  ~AnalysisRegions();

  enum Alignment {
    NONE,
    TIME_ZERO,
    PLAYBACK_BEGIN
  };

  void updateFrom(const TimePoint& blockStartTime,
                  const TimePoint& curTime,
                  const PlaybackRegion& currentPlaybackRegion);
  std::set<Region> getRegions(SampleCounter rangeStart, SampleCounter rangeEnd);
  size_t getNumRegionsInState(Region::State state);
  SampleRate getReferenceSampleRate();
  uint32_t getRegionSizeMs();
  uint32_t getRegionSizeSamples();
  void setRegionSizeMs(uint32_t ms);
  uint32_t getRegionFreqMs();
  uint32_t getRegionFreqSamples();
  void setRegionFreqMs(uint32_t ms);
  Alignment getAlignment();
  void setAlignment(Alignment alignment);
  void restartRegions();
  void updateAsStale();
  void abortInProgress();
  void generateRegions(bool enable);
  PlaybackResults::Results getResults();
  uint64_t getResultsUpdateCount();
  void resetResults();

private:
  void timerCallback() override;

  std::optional<std::pair<SampleCounter, SampleCounter>>
  getLastAddedRegionSampleCounters();
  bool addNewRegion(SampleCounter startTime);
  bool addNewRegionIfRequired();
  void updateRegions();

  // using weak_ptrs so we don't end up with cyclic shared_ptrs
  std::weak_ptr<ServiceCommunicator> comms_;
  std::weak_ptr<MonoCircularBuffer> readBuff_;

  std::mutex regionsLock_;
  std::set<Region> regions_;

  PlaybackResults playbackResults_;

  SampleRate refSampleRate_{16000};
  SampleCounter regionSize_{16000 * 5};           // 5 sec
  SampleCounter regionFrequency_{(16000 * 5) / 2};  // 2.5 sec
  std::vector<float> analysisBlock_; // Avoid repeated alloc
  TimePoint curTime_;
  PlaybackRegion lastKnownPlaybackRegion_;
  SampleCounter maxRegionAge_;
  const size_t maxPendingRegions_{3}; // Prevent overwhelming service when connected
  std::atomic<Alignment> alignment_{TIME_ZERO};
  std::atomic<bool> generateRegions_{true};
};

} // namespace audio_plugin
