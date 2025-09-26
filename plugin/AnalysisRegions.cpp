#include "AnalysisRegions.h"
#include <algorithm>
#include "Utils.h"
#include <cstring>  // For memcpy
#include <chrono>
#include <span>

namespace {

TimePoint roundUpToAlignmentBoundary(const PlaybackTimePoint& inTimePoint,
                                     SampleCounter boundarySpacing) {
  if (inTimePoint.playheadTime < 0) {
    // First alignment should be at zero - shift it forward to 0.
    auto nextAlignmentBoundary =
        toTimePoint(inTimePoint) - inTimePoint.playheadTime;
    return nextAlignmentBoundary;
  }

  auto fullRegions = inTimePoint.playheadTime / boundarySpacing;
  auto regionRemainder = inTimePoint.playheadTime % boundarySpacing;
  if (regionRemainder == 0) {
    return toTimePoint(inTimePoint);
  }
  auto additionalSamplesToBoundary =
      regionRemainder < 0 ? -regionRemainder : boundarySpacing - regionRemainder;
  auto nextAlignmentBoundary =
      toTimePoint(inTimePoint) + additionalSamplesToBoundary;
  return nextAlignmentBoundary;
}

SampleCounter calcPlaybackAlignmentOffsetFromZero(PlayheadTime playheadTime,
                                                  SampleCounter regionFrequency) {
  return playheadTime % regionFrequency;
}

};

namespace audio_plugin {

AnalysisRegions::AnalysisRegions(std::shared_ptr<MonoCircularBuffer> readBuff,
                                 std::shared_ptr<ServiceCommunicator> comms) {
  assert(readBuff && comms);
  readBuff_ = readBuff;
  comms_ = comms;
  refSampleRate_ = readBuff->getSampleRate();
  analysisBlock_.resize(regionSize_, 0.f);
  maxRegionAge_ = readBuff->getNumStoredSamples();
  startTimerHz(10);
}

AnalysisRegions::~AnalysisRegions() {}

void AnalysisRegions::updateFrom(const TimePoint& blockStartTime,
                                 const TimePoint& curTime,
                                 const PlaybackRegion& currentPlaybackRegion) {
  curTime_ = curTime.asSampleRate(refSampleRate_);
  lastKnownPlaybackRegion_ = currentPlaybackRegion.asSampleRate(refSampleRate_);
  // Add regions if we can
  if (generateRegions_) {
    addNewRegionIfRequired();
  }
}

bool AnalysisRegions::addNewRegion(SampleCounter startTime) {
  TimePoint nextRegionStartTime{refSampleRate_, startTime, std::nullopt};
  TimePoint nextRegionEndTime = nextRegionStartTime + regionSize_;

  // Make sure region doesn't extend in to the future
  if (nextRegionEndTime.sampleCounter >= curTime_.sampleCounter) {
    return false;
  }

  // Add playhead times if this region was within a playback moment
  bool wasDuringPlayback{false};
  bool startedDuringPlayback{false};
  bool endedDuringPlayback{false};
  if (lastKnownPlaybackRegion_.start.has_value()) {
    auto playheadSyncStart = lastKnownPlaybackRegion_.start.value();
    auto playheadDelta =
        playheadSyncStart.sampleCounter - playheadSyncStart.playheadTime;
    if (lastKnownPlaybackRegion_.end.has_value()) {
      auto playheadSyncEnd = lastKnownPlaybackRegion_.end.value();
      // We have a known playback moment in past
      // Did this region overlap it?
      if (nextRegionStartTime.sampleCounter < playheadSyncEnd.sampleCounter &&
          nextRegionEndTime.sampleCounter > playheadSyncStart.sampleCounter) {
        // Overlaps playback moment
        wasDuringPlayback = true;
        startedDuringPlayback =
            nextRegionStartTime.sampleCounter >=
                playheadSyncStart.sampleCounter &&
            nextRegionStartTime.sampleCounter < playheadSyncEnd.sampleCounter;
        endedDuringPlayback =
            nextRegionEndTime.sampleCounter > playheadSyncStart.sampleCounter &&
            nextRegionEndTime.sampleCounter <= playheadSyncEnd.sampleCounter;
      }
    } else {
      // Currently playing
      // If this region doesn't end before the playback started, it is within
      // playback moment.
      if (nextRegionEndTime.sampleCounter > playheadSyncStart.sampleCounter) {
        wasDuringPlayback = true;
        startedDuringPlayback = nextRegionStartTime.sampleCounter >=
                                playheadSyncStart.sampleCounter;
        endedDuringPlayback = true;
      }
    }
    if (startedDuringPlayback) {
      nextRegionStartTime.playheadTime =
          nextRegionStartTime.sampleCounter - playheadDelta;
    }
    if (endedDuringPlayback) {
      nextRegionEndTime.playheadTime =
          nextRegionEndTime.sampleCounter - playheadDelta;
    }
  }

  // Add region
  bool successReturn{false};
  {
    std::lock_guard mtx(regionsLock_);
    uint16_t nextCount = 0;
    if (regions_.size() > 0) {
      nextCount = regions_.rbegin()->count + 1;
    }
    auto empRes = regions_.emplace(nextRegionStartTime, nextRegionEndTime,
                                   nextCount, wasDuringPlayback);
    if (empRes.second) {
      successReturn = true;
    }
  }

  // If the new regions don't align with regions in playbackResults_, we need to clear that down
  if (nextRegionStartTime.playheadTime.has_value() &&
      nextRegionEndTime.playheadTime.has_value()) {
    playbackResults_.setConfigFromRegion(
        nextRegionStartTime.playheadTime.value(),
        nextRegionEndTime.playheadTime.value(), regionFrequency_);
  }

  return successReturn;
}

bool AnalysisRegions::addNewRegionIfRequired() {
  auto lastAddedRegionSpan = getLastAddedRegionSampleCounters();
  auto alignment = alignment_.load();

  // Check whether we should align the region
  if (alignment != NONE && curTime_.playheadTime.has_value() &&
      lastKnownPlaybackRegion_.start.has_value()) {
    // Currently playing and have data to determine alignment
    auto playbackSyncStart = lastKnownPlaybackRegion_.start.value();
    // Needs aligning if there has either not been a previous region, or
    // the last region was before playback started (so not aligned to playback)
    if (!lastAddedRegionSpan.has_value() ||
        lastAddedRegionSpan.value().first < playbackSyncStart.sampleCounter) {
      // Do alignment
      if (alignment == PLAYBACK_BEGIN) {
        return addNewRegion(playbackSyncStart.sampleCounter);
      } else if (alignment == TIME_ZERO) {
        auto nextTimePoint =
            roundUpToAlignmentBoundary(playbackSyncStart, regionFrequency_);
        return addNewRegion(nextTimePoint.sampleCounter);
      }
      return false;
    }
  }

  // No alignment needed/possible. Free run regions.

  // Default to region right now
  SampleCounter regionStartNow = curTime_.sampleCounter - regionSize_ - 1;
  TimePoint nextRegionStartTime{
      refSampleRate_, regionStartNow > 0 ? regionStartNow : 0, std::nullopt};

  // Correct to align with frequency and any prev block
  if (lastAddedRegionSpan.has_value()) {
    nextRegionStartTime.sampleCounter =
        lastAddedRegionSpan.value().first + regionFrequency_;
  }

  return addNewRegion(nextRegionStartTime.sampleCounter);
}

void AnalysisRegions::timerCallback() {
  // Avoid message tx/rx on audio thread by having updateRegions called from timer
  updateRegions();
}

void AnalysisRegions::generateRegions(bool enable) {
  generateRegions_ = enable;
}

std::optional<std::pair<SampleCounter, SampleCounter>>
AnalysisRegions::getLastAddedRegionSampleCounters() {
  std::lock_guard mtx(regionsLock_);
  if (regions_.empty()) {
    return std::nullopt;
  }
  return std::pair<SampleCounter, SampleCounter>{
      regions_.rbegin()->start.sampleCounter,
          regions_.rbegin()->end.sampleCounter};
}

size_t AnalysisRegions::getNumRegionsInState(Region::State state) {
  std::lock_guard mtx(regionsLock_);
  return std::count_if(
      PREFERRED_EXEC(std::execution::par, regions_.begin(), regions_.end(),
                     [state](const Region& region) {
                       return region.analysisState == state;
                     }));
}

SampleRate AnalysisRegions::getReferenceSampleRate() {
  return refSampleRate_;
}

uint32_t AnalysisRegions::getRegionSizeMs() {
  return samplesToMs(regionSize_, refSampleRate_);
}

uint32_t AnalysisRegions::getRegionSizeSamples() {
  return regionSize_;
}

void AnalysisRegions::setRegionSizeMs(uint32_t ms) {
  regionSize_ = msToSamples(ms, refSampleRate_);
  analysisBlock_.resize(regionSize_, 0.f);
}

uint32_t AnalysisRegions::getRegionFreqMs() {
  return samplesToMs(regionFrequency_, refSampleRate_);
}

uint32_t AnalysisRegions::getRegionFreqSamples() {
  return regionFrequency_;
}

void AnalysisRegions::setRegionFreqMs(uint32_t ms) {
  regionFrequency_ = msToSamples(ms, refSampleRate_);
}

void AnalysisRegions::restartRegions() {
  std::lock_guard mtx(regionsLock_);
  regions_.clear();
}

void AnalysisRegions::updateAsStale() {
  std::lock_guard mtx(regionsLock_);
  std::erase_if(regions_, [](const Region& region) {
    // A stale timedout/failed/pending region might as well not exist
    return region.analysisState == Region::State::PENDING ||
           region.analysisState == Region::State::TIMEOUT ||
           region.analysisState == Region::State::FAILURE;
  });
  for(auto& region : regions_) {
    region.stale = true;
  }
}

AnalysisRegions::Alignment AnalysisRegions::getAlignment() {
  return alignment_;
}

void AnalysisRegions::setAlignment(AnalysisRegions::Alignment alignment){
  alignment_ = alignment;
}

void AnalysisRegions::abortInProgress() {
  std::lock_guard mtx(regionsLock_);
  for (auto& region : regions_) {
    if (region.analysisState == Region::State::IN_PROGRESS) {
      region.analysisState = Region::State::TIMEOUT;
    }
  }
}

void AnalysisRegions::updateRegions() {
  // Called via timer so that message tx/rx doesn't occur on the audio thread.
  // Of course, we still need the lock on regions_, so it will still be 
  // competing with the audio thread a little

  auto comms = comms_.lock();
  auto readBuff = readBuff_.lock();
  assert(comms && readBuff);

  {
    std::lock_guard mtx(regionsLock_);

    // Erase old regions
    auto regionStartCutoff = curTime_.sampleCounter - maxRegionAge_;
    if (regionStartCutoff >= 0) {
      std::erase_if(regions_, [regionStartCutoff](const Region& region) {
        // If region too old, return true
        return region.start.sampleCounter < regionStartCutoff;
      });
    }

    // Send off pending jobs - latest takes priority
    for (auto rit = regions_.rbegin(); rit != regions_.rend(); ++rit) {
      auto const& region = *rit;
      if (region.analysisState == Region::State::PENDING) {
        if (!comms->readyToSend()) {
          break;
        }
        auto res = comms->sendRequest(region.start, regionSize_, readBuff);
        if (res) {
          region.analysisState = Region::State::IN_PROGRESS;
        }
      }
    }

    // Abort pending regions beyond limit
    size_t pendingCount{0};
    for (auto rit = regions_.rbegin(); rit != regions_.rend(); ++rit) {
      auto const& region = *rit;
      if (region.analysisState == Region::State::PENDING) {
        pendingCount++;
        if (pendingCount > maxPendingRegions_) {
          region.analysisState = Region::State::TIMEOUT;
        }
      }
    }
  }

  // Check for new responses
  auto resp = comms->getResponse();
  if (resp.has_value()) {
    std::lock_guard mtx(regionsLock_);
    SampleCounter reqId = resp.value().reqId;
    // Lookup region and update
    for (auto& region : regions_) {
      if (region.start.sampleCounter == reqId) {
        // Update struct
        if (resp.value().success) {
          region.analysisResult = resp.value().result;
          region.analysisState = Region::State::COMPLETE;
        } else {
          region.analysisState = Region::State::FAILURE;
        }
        // If during playback, add to playbackResults_
        if (region.start.playheadTime.has_value() &&
            region.end.playheadTime.has_value()) {
          playbackResults_.addResult(region);
        }
      }
    }
  }

}

PlaybackResults::Results AnalysisRegions::getResults() {
  return playbackResults_.getResults();
}

uint64_t AnalysisRegions::getResultsUpdateCount() {
  return playbackResults_.getUpdateCounter();
}

void AnalysisRegions::resetResults() {
  playbackResults_.clear();
}

std::set<Region> AnalysisRegions::getRegions(
    SampleCounter rangeStart,
    SampleCounter rangeEnd) {
  std::set<Region> ret;
  std::lock_guard mtx(regionsLock_);
  for (auto const& region : regions_) {
    if (region.end.sampleCounter >= rangeStart &&
        region.start.sampleCounter <= rangeEnd) {
      ret.insert(region);
    }
  }
  return ret;
}

void PlaybackResults::addResult(const Region& resultantRegion) {
  auto resultantRegionSize =
      resultantRegion.end.sampleCounter - resultantRegion.start.sampleCounter;
  if (resultantRegionSize == regionSize_) {
    auto resultantRegionStartPlayheadTime = resultantRegion.start.playheadTime.value();
    auto resultantRegionAlignment = calcPlaybackAlignmentOffsetFromZero(
        resultantRegionStartPlayheadTime, regionFrequency_);
    if (alignmentOffset_ == resultantRegionAlignment) {
      auto resultantRegionPlaythroughOffset =
          resultantRegion.start.sampleCounter -
          resultantRegionStartPlayheadTime;
      std::lock_guard mtx(resultsLock_);
      results_.playheadStartTimes.insert(resultantRegionStartPlayheadTime);
      results_.playthroughOffsets.insert(resultantRegionPlaythroughOffset);
      results_.regions[resultantRegionPlaythroughOffset]
                      [resultantRegionStartPlayheadTime] = resultantRegion;
      ++updateCounter_;
    }
  }
}

PlaybackResults::Results PlaybackResults::getResults() {
  std::lock_guard mtx(resultsLock_);
  return results_;
}

uint64_t PlaybackResults::getUpdateCounter() {
  return updateCounter_;
}

void PlaybackResults::setConfigFromRegion(PlayheadTime aligningRegionStart,
                                          PlayheadTime aligningRegionEnd,
                                          SampleCounter regionFrequency) {
  auto regionSize = aligningRegionEnd - aligningRegionStart;
  auto offset = calcPlaybackAlignmentOffsetFromZero(aligningRegionStart, regionFrequency);
  setConfig(offset, regionSize, regionFrequency);
}

void PlaybackResults::setConfig(SampleCounter alignmentOffset,
                                SampleCounter regionSize,
                                SampleCounter regionFrequency) {
  if(alignmentOffset_ == alignmentOffset && regionSize_ == regionSize &&
      regionFrequency_ == regionFrequency) {
    return;
  }
  std::lock_guard mtx(resultsLock_);
  alignmentOffset_ = alignmentOffset;
  regionSize_ = regionSize;
  regionFrequency_ = regionFrequency;
  results_ = Results();
  ++updateCounter_;
}

void PlaybackResults::clear() {
  std::lock_guard mtx(resultsLock_);
  results_ = Results();
  ++updateCounter_;
}

}  // namespace audio_plugin