#include "CircularBuffer.h"
#include <cassert>
#include <algorithm>

namespace audio_plugin {

WriteTracker::WriteTracker(size_t positionLimit)
    : positionLimit_(positionLimit) {}

void WriteTracker::recordWrite(const TimePoint& latestDataStart,
                               int64_t latestDataOffset) {
  if (haveWritten_) {
    // Sanity check sample counter moving sequentially
    assert(latestDataStart.sampleCounter + latestDataOffset ==
           latestDataEnd_.sampleCounter + 1);
  }
  latestDataEnd_ = latestDataStart + latestDataOffset;
  if (haveWritten_) {
    latestPosition_++;
    if (positionLimit_ != 0 && latestPosition_ >= positionLimit_) {
      latestPosition_ = 0;
    }
  }
  haveWritten_ = true;
}

bool WriteTracker::haveWritten() {
  return haveWritten_;
}

size_t WriteTracker::getNextWritePosition() {
  if (!haveWritten_)
    return 0;
  auto nextPosition = latestPosition_ + 1;
  if (positionLimit_ != 0 && nextPosition >= positionLimit_) {
    nextPosition = 0;
  }
  return nextPosition;
}

size_t WriteTracker::getLatestWritePosition() {
  assert(haveWritten_);
  return latestPosition_;
}

TimePoint WriteTracker::getLatestDataTimePoint() {
  assert(haveWritten_);
  return latestDataEnd_;
}

MonoCircularBuffer::MonoCircularBuffer(uint32_t bufferLengthMs,
                                       SampleRate sampleRate)
    : sampleRate_{sampleRate} {
  size_t bufferLength = (bufferLengthMs * sampleRate) / 1000;
  std::lock_guard<std::mutex> bufferLock{bufferMutex_};
  buffer_ = std::vector<float>(bufferLength, 0.f);
  writeTracker_ = WriteTracker(bufferLength);
}

void MonoCircularBuffer::updateFrom(const std::vector<float>& srcBuffer,
                                    const TimePoint& startTime) {

  std::lock_guard<std::mutex> lock{bufferMutex_};
  for (int s = 0; s < srcBuffer.size(); ++s) {
    size_t bufferNextWritePos = writeTracker_.getNextWritePosition();
    buffer_[bufferNextWritePos] = srcBuffer[s];
    writeTracker_.recordWrite(startTime, s);
  }
}

TimePoint MonoCircularBuffer::getLatestSamples(
    std::vector<float>& dstBuffer) {
  std::lock_guard<std::mutex> lock{bufferMutex_};

  if (dstBuffer.size() == 0) {
    return {sampleRate_, 0, std::nullopt};
  }

  if (buffer_.size() == 0 || !writeTracker_.haveWritten()) {
    // Zero-fill
    std::fill(dstBuffer.begin(), dstBuffer.end(), 0.f);
    return {sampleRate_, 0, std::nullopt};
  }

  size_t readPos = writeTracker_.getLatestWritePosition();
  for (int i = dstBuffer.size() - 1; i >= 0; --i) {
    dstBuffer[i] = buffer_[readPos];
    if (readPos == 0) {
      readPos = buffer_.size() - 1;
    } else {
      --readPos;
    }
  }
  return writeTracker_.getLatestDataTimePoint();
}

bool MonoCircularBuffer::getSamples(const TimePoint& startTime,
                                    std::vector<float>& dstBuffer) {
  std::span<float> dstSpan(dstBuffer);
  return getSamples(startTime, dstSpan);
}

bool MonoCircularBuffer::getSamples(const TimePoint& startTime,
                                    std::span<float>& dstBuffer) {
  if (startTime.sampleRate != sampleRate_) {
    return false;
  }
  std::lock_guard<std::mutex> lock{bufferMutex_};
  if (buffer_.size() == 0 || !writeTracker_.haveWritten()) {
    return false;
  }
  auto buffEnd = writeTracker_.getLatestDataTimePoint();
  if (startTime.sampleCounter + dstBuffer.size() > buffEnd.sampleCounter) {
    return false;
  }
  SampleCounter startPosDelta = buffEnd.sampleCounter - startTime.sampleCounter;
  if (startPosDelta > buffer_.size()) {
    return false;
  }
  int64_t buffEndPos = writeTracker_.getLatestWritePosition();
  for (int s = 0; s < dstBuffer.size(); s++) {
    auto buffReadPos = buffEndPos - startPosDelta + s;
    if (buffReadPos < 0) {
      buffReadPos += buffer_.size();
    }
    dstBuffer[s] = buffer_[buffReadPos];
  }
  return true;
}

uint32_t MonoCircularBuffer::getDurationMs() {
  return (buffer_.size() * 1000) / sampleRate_;
}

size_t MonoCircularBuffer::getNumStoredSamples() {
  return buffer_.size();
}

const SampleRate MonoCircularBuffer::getSampleRate() {
  return sampleRate_;
}

Buff::Buff(SampleRate srcSampleRate,
           uint16_t srcBlockSize,
           SampleRate targetSampleRate,
           std::shared_ptr<ServiceCommunicator> comms) {
  srcSampleRate_ = srcSampleRate;
  buffSampleRate_ = targetSampleRate;
  downsampleRatio_ = static_cast<double>(srcSampleRate) /
                     static_cast<double>(targetSampleRate);
  // Unconsumed samples are samples unused by the resampler from the last block.
  // Can't possibly be more than a full block 
  // (usually only up to 3 samples - 48Khz to 16Khz)
  unconsumedSamples_.reserve(srcBlockSize);
  // latestBlockForResampling_ must accomodate a block plus any unconsumedSamples_ 
  latestBlockForResampling_.reserve(srcBlockSize + unconsumedSamples_.capacity());
  // latestResampledBlock_ must accomodate latestBlockForResampling_/downsampleRatio_,
  // ceiled for safety
  size_t maxResampledBlockSize =
      static_cast<size_t>(latestBlockForResampling_.capacity() /
                          downsampleRatio_) + 1;
  latestResampledBlock_.reserve(maxResampledBlockSize);
  // Set up circBuff_ for the monoised 16Khz samples
  circBuff_ = std::make_shared<MonoCircularBuffer>(1200000, targetSampleRate);
  // Set up analysis region handler
  analysisRegions_ = std::make_shared<AnalysisRegions>(circBuff_, comms);
}

void Buff::justStarted() {
  playbackState_ = JUST_STARTED;
}

void Buff::justStopped() {
  playbackState_ = JUST_STOPPED;
}

void Buff::updateFrom(const juce::AudioBuffer<float>& srcBuffer,
                      const TimePoint& startTime) {
  // See if we need to update playhead start/stop points
  {
    std::lock_guard<std::mutex> mtx{playbackRegionMtx_};
    switch (playbackState_) {
      case JUST_STARTED:
        assert(startTime.playheadTime.has_value());
        playbackRegion_.end.reset();
        playbackRegion_.start = PlaybackTimePoint{
            startTime.sampleRate,
            startTime.sampleCounter,
            startTime.playheadTime.value()};
        break;
      case JUST_STOPPED:
        if (playbackRegion_.start.has_value()) {
          assert(startTime.sampleRate == playbackRegion_.start.value().sampleRate);
          auto delta = startTime.sampleCounter -
                       playbackRegion_.start.value().sampleCounter;
          playbackRegion_.end = PlaybackTimePoint{
              startTime.sampleRate,
              startTime.sampleCounter + srcBuffer.getNumSamples(),
              playbackRegion_.start.value().sampleCounter + delta};
        } else {
          playbackRegion_.end.reset(); // Can't have an end without a start
        }
        break;
      case PLAYING:
        playbackRegion_.end.reset(); // Should be anyway, but to be sure
        // Check to see if we are getting sequential blocks
        // If not, we've likely skipped or looped
        if (lastUpdateEndPlayheadTime_.has_value() &&
            startTime.playheadTime.has_value() &&
            lastUpdateEndPlayheadTime_.value() !=
                startTime.playheadTime.value()) {
          playbackRegion_.end.reset();
          playbackRegion_.start =
              PlaybackTimePoint{startTime.sampleRate, startTime.sampleCounter,
                                startTime.playheadTime.value()};
        }
        break;
    }
    lastUpdateEndPlayheadTime_.reset();
    if (startTime.playheadTime.has_value()) {
      lastUpdateEndPlayheadTime_ =
          startTime.playheadTime.value() + srcBuffer.getNumSamples();
    }
  }

  // Put the last unconsumed samples in the vector to pass to the resampler
  latestBlockForResampling_.clear();
  latestBlockForResampling_.insert(latestBlockForResampling_.end(),
                                   unconsumedSamples_.begin(),
                                   unconsumedSamples_.end());

  // Add new mono-ised samples to blockForResampling
  for (int sampleNum = 0; sampleNum < srcBuffer.getNumSamples(); ++sampleNum) {
    float sample = getMonoSample(srcBuffer, sampleNum);
    latestBlockForResampling_.push_back(sample);
  }

  // Pass to resampler
  auto requiredSamples =
      static_cast<int>(latestBlockForResampling_.size() / downsampleRatio_);
  latestResampledBlock_.resize(requiredSamples, 0.f);
  auto samplesConsumed =
      interp_.process(downsampleRatio_, latestBlockForResampling_.data(),
                      latestResampledBlock_.data(), requiredSamples);
  if (!interpPrimingSamples_.has_value()) {
    auto expectedSamplesConsumed =
        static_cast<int>(requiredSamples * downsampleRatio_);
    interpPrimingSamples_ = expectedSamplesConsumed - samplesConsumed;
  }

  // Work out time points
  TimePoint latestBlockForResamplingStartTime =
      startTime - static_cast<SampleCounter>(unconsumedSamples_.size() -
                                             interpPrimingSamples_.value_or(0));
  TimePoint latestResampledBlockStartTime =
      latestBlockForResamplingStartTime / downsampleRatio_;

  // Store unconsumed samples for next iter
  auto unconsumedSampleCount =
      latestBlockForResampling_.size() - samplesConsumed;
  unconsumedSamples_.clear();
  unconsumedSamples_.insert(
      unconsumedSamples_.begin(),
      latestBlockForResampling_.end() - unconsumedSampleCount,
      latestBlockForResampling_.end());

  // At this stage, we have mono 16Khz downsampled data in latestResampledBlock_
  // Put in circular buffer - update latest start timestamp
  circBuff_->updateFrom(latestResampledBlock_, latestResampledBlockStartTime);

  // See if we need to create new analysis regions
  analysisRegions_->updateFrom(
      latestResampledBlockStartTime,
      latestResampledBlockStartTime + latestResampledBlock_.size() - 1,
      getPlaybackRegion()
    );

  // Update state
  switch (playbackState_) { 
    case JUST_STARTED:
      playbackState_ = PLAYING;
      break;
    case JUST_STOPPED:
      playbackState_ = IDLE;
      break;
  }

}

std::shared_ptr<MonoCircularBuffer> Buff::getCircularBuffer() {
  return circBuff_;
}

std::shared_ptr<AnalysisRegions> Buff::getAnalysisRegions() {
  return analysisRegions_;
}

SampleRate Buff::getBufferSampleRate() {
  return buffSampleRate_;
}

PlaybackRegion Buff::getPlaybackRegion() {
  std::lock_guard<std::mutex> mtx{playbackRegionMtx_};
  return playbackRegion_;
}

float Buff::getMonoSample(const juce::AudioBuffer<float>& srcBuffer,
                          int sampleNumber) {
  float outSample{0.f};
  for (int c = 0; c < srcBuffer.getNumChannels(); ++c) {
    // TODO: Proper downmix per channel count.
    auto sample = srcBuffer.getSample(c, sampleNumber);
    if (srcBuffer.getNumChannels() == 1) {
      outSample += sample;
    } else {
      // Probably stereo. For any multi-channel, sum with -3db for now.
      outSample += sample * 0.7079f;
    }
  }
  return outSample;
}

}  // namespace audio_plugin
