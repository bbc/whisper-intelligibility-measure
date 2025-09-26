#pragma once

#include <iostream>
#include <iomanip>
#include <string>
#include <random>
#include <chrono>
#include <sstream>
#include <functional>
#include "Types.h"

// Detect if <execution> and its policies are available
#if defined(__has_include)
  #if __has_include(<execution>)
    #include <execution>
    #define HAS_EXECUTION_POLICY 1
  #else
    #define HAS_EXECUTION_POLICY 0
  #endif
#else
  #define HAS_EXECUTION_POLICY 0
#endif

// Check if compiling with Apple Clang
#if defined(__apple_build_version__)
  #define USE_PARALLEL_POLICY 0
#else
  #define USE_PARALLEL_POLICY 1
#endif

// Define the PREFERRED_EXEC macro based on availability and compiler
#if HAS_EXECUTION_POLICY && USE_PARALLEL_POLICY
  #define PREFERRED_EXEC(policy, ...) policy, __VA_ARGS__
#else
  #define PREFERRED_EXEC(policy, ...) __VA_ARGS__
#endif

inline std::string formatTime(SampleCounter sampleCounter, SampleRate samplesPerSecond) {
  // Total time in seconds (floating point for fractional part)
  double totalSeconds = static_cast<double>(sampleCounter) / samplesPerSecond;

  // Extract minutes
  int minutes = static_cast<int>(totalSeconds / 60);

  // Extract full seconds (after subtracting minutes)
  int seconds = static_cast<int>(totalSeconds) % 60;

  // Extract tenths of a second (fractional part)
  int tenths =
      static_cast<int>((totalSeconds - static_cast<int>(totalSeconds)) * 10);

  // Format the result as MM:SS.s
  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(2) << minutes << ":"
      << std::setfill('0') << std::setw(2) << seconds << "." << tenths;

  return oss.str();
}

inline SampleCounter toSecBoundary(SampleCounter fromSampleCounter,
                               SampleRate samplesPerSecond,
                               int64_t minStepAndDir) {
  fromSampleCounter += minStepAndDir;
  if (minStepAndDir > 0) 
    fromSampleCounter += samplesPerSecond - 1;
  return fromSampleCounter / samplesPerSecond;

}

inline SampleCounter msToSamples(uint32_t ms, SampleRate refSampleRate) {
  return (ms * refSampleRate) / 1000;
}

inline uint32_t samplesToMs(SampleCounter samples, SampleRate refSampleRate) {
  return (samples * 1000) / refSampleRate;
}

inline PlaybackTimePoint toPlaybackTimePoint(
    const TimePoint& src, const SampleCounter defaultPlayheadTime = 0) {
  if (src.playheadTime.has_value()) {
    return PlaybackTimePoint{src.sampleRate, src.sampleCounter,
                             src.playheadTime.value()};
  }
  return PlaybackTimePoint{src.sampleRate, src.sampleCounter,
                           defaultPlayheadTime};
}

inline TimePoint toTimePoint(const PlaybackTimePoint& src) {
  return TimePoint{src.sampleRate, src.sampleCounter, src.playheadTime};
}

inline std::string generateUniqueID() {
  // Get current time in milliseconds since epoch
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();

  // Generate a random number
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(100000, 999999);  // 6-digit random number

  // Get hash of the memory address of the generator (just as a source of
  // entropy)
  std::hash<std::mt19937*> hasher;
  std::size_t hashValue = hasher(&gen);

  // Combine the elements into a string
  std::ostringstream oss;
  oss << timestamp << "-" << dis(gen) << "-" << hashValue;

  return oss.str();
}