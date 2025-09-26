#pragma once
#include <cstdint>
#include <optional>
#include <cassert>
#include <type_traits>

typedef uint32_t SampleRate;
typedef int64_t SampleCounter;  // need to support neg for priming
typedef int64_t PlayheadTime;  // same underlying type as juce uses for playhead

// This struct describes a point in the audio in time
//
// sampleCounter is an ever-increasing count of the number
//  of samples received in to the plugin so it can uniquely
//  point to any point in audio-time in this plugin
//  instances existence (by "audio-time" we're referring
//  only to points in time whilst the plugin was receiving
//  audio, so when no audio is being fed it, this counter
//  would pause).
//
// playheadTime is the time in samples of the playhead
//  position when this TimePoint was taken. This time
//  isn't necessarily ever-increasing and not necessarily
//  sequential since the user can move the playhead
//  position within the DAW project. Also for live signals
//  there may not be a playhead position, so it is an
//  optional

struct TimePoint {
  SampleRate sampleRate;
  SampleCounter sampleCounter{0};
  std::optional<PlayheadTime> playheadTime;

  // Overload operators

  TimePoint& operator+=(const TimePoint& other) {
    auto ratio = other.sampleRate / sampleRate;
    auto otherConv = other / ratio;
    assert(otherConv.sampleRate == sampleRate);
    sampleCounter += otherConv.sampleCounter;
    if (playheadTime.has_value() && otherConv.playheadTime.has_value()) {
      *playheadTime += *otherConv.playheadTime;
    } else {
      playheadTime.reset();
    }
    return *this;
  }

  TimePoint operator+(const TimePoint& other) const {
    TimePoint newTimePoint = *this;
    newTimePoint += other;
    return newTimePoint;
  }

  template <typename T>
  TimePoint& operator+=(T delta) {
    static_assert(std::is_integral<T>::value,
                  "Operand must be an integral type");
    sampleCounter += delta;
    if (playheadTime.has_value()) {
      *playheadTime += delta;
    }
    return *this;
  }

  template <typename T>
  TimePoint operator+(T delta) const {
    TimePoint newTimePoint = *this;
    newTimePoint += delta;
    return newTimePoint;
  }

  TimePoint& operator-=(const TimePoint& other) {
    auto ratio = other.sampleRate / sampleRate;
    auto otherConv = other / ratio;
    assert(otherConv.sampleRate == sampleRate);
    sampleCounter -= otherConv.sampleCounter;
    if (playheadTime.has_value() && otherConv.playheadTime.has_value()) {
      *playheadTime -= *otherConv.playheadTime;
    } else {
      playheadTime.reset();
    }
    return *this;
  }

  TimePoint operator-(const TimePoint& other) const {
    TimePoint newTimePoint = *this;
    newTimePoint -= other;
    return newTimePoint;
  }

  template <typename T>
  TimePoint& operator-=(T delta) {
    static_assert(std::is_integral<T>::value,
                  "Operand must be an integral type");
    sampleCounter -= delta;
    if (playheadTime.has_value()) {
      *playheadTime -= delta;
    }
    return *this;
  }

  template <typename T>
  TimePoint operator-(T delta) const {
    TimePoint newTimePoint = *this;
    newTimePoint -= delta;
    return newTimePoint;
  }

  TimePoint& operator/=(double downsampleRatio) {
    if (downsampleRatio == 1.0)
      return *this;
    double sampleRateDownsampled =
        static_cast<double>(sampleRate) / downsampleRatio;
    this->sampleRate = static_cast<SampleRate>(sampleRateDownsampled);
    double sampleCounterDownsampled =
        static_cast<double>(sampleCounter) / downsampleRatio;
    this->sampleCounter = static_cast<SampleCounter>(sampleCounterDownsampled);
    if (playheadTime.has_value()) {
      double playheadTimeDownsampled =
          static_cast<double>(playheadTime.value()) / downsampleRatio;
      playheadTime = static_cast<PlayheadTime>(playheadTimeDownsampled);
    }
    return *this;
  }

  TimePoint operator/(double downsampleRatio) const {
    TimePoint newTimePoint = *this;
    newTimePoint /= downsampleRatio;
    return newTimePoint;
  }

  TimePoint asSampleRate(const SampleRate newSampleRate) const {
    if (newSampleRate == sampleRate) {
      return *this;
    }
    auto downsampleRatio =
        static_cast<double>(sampleRate) / static_cast<double>(newSampleRate);
    return *this / downsampleRatio;
  }

};

struct PlaybackTimePoint {
  SampleRate sampleRate;
  SampleCounter sampleCounter;
  PlayheadTime playheadTime;

  PlaybackTimePoint asSampleRate(const SampleRate newSampleRate) const {
    if (newSampleRate == sampleRate) {
      return *this;
    }
    auto downsampleRatio =
        static_cast<double>(sampleRate) / static_cast<double>(newSampleRate);
    double sampleRateDownsampled =
        static_cast<double>(sampleRate) / downsampleRatio;
    double sampleCounterDownsampled =
        static_cast<double>(sampleCounter) / downsampleRatio;
    double playheadTimeDownsampled =
        static_cast<double>(playheadTime) / downsampleRatio;

    return PlaybackTimePoint{
        static_cast<SampleRate>(sampleRateDownsampled),
        static_cast<SampleCounter>(sampleCounterDownsampled),
        static_cast<PlayheadTime>(playheadTimeDownsampled)};
  }

};

struct PlaybackRegion {
  std::optional<PlaybackTimePoint> start;
  std::optional<PlaybackTimePoint> end;

  PlaybackRegion asSampleRate(const SampleRate newSampleRate) const {
    std::optional<PlaybackTimePoint> newStart;
    std::optional<PlaybackTimePoint> newEnd;
    if (start.has_value()) {
      newStart = start.value().asSampleRate(newSampleRate);
    }
    if (end.has_value()) {
      newEnd = end.value().asSampleRate(newSampleRate);
    }
    return PlaybackRegion{newStart, newEnd};
  }

};