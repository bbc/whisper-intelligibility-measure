#pragma once

#include "CircularBuffer.h"
#include "Comms.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include <mutex>
#include <memory>

namespace audio_plugin {

class AudioPluginAudioProcessorEditor; //forward decl

class AudioPluginAudioProcessor : public juce::AudioProcessor {
public:
  AudioPluginAudioProcessor();
  ~AudioPluginAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;

  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
  using AudioProcessor::processBlock;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String &newName) override;

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  bool isStandalone();

  SampleCounter getSampleCounter();
  std::optional<SampleCounter> getPlayheadPosition();

  const SampleRate processingSampleRate{16000};

  std::shared_ptr<Buff> getBufferManager();
  std::shared_ptr<MonoCircularBuffer> getCircularBuffer();
  std::shared_ptr<AnalysisRegions> getAnalysisRegions();
  std::shared_ptr<ServiceCommunicator> getCommunicator();

private:
  juce::PluginHostType pluginHostType_;

  std::shared_ptr<Buff> buffMan_;
  std::shared_ptr<ServiceCommunicator> comms_;

  double lastKnownSampleRate_{0.0};

  struct PlayState {
    SampleCounter sampleCounter{0}; // enough for ~1.5m years at 192khz 
    juce::Optional<SampleCounter> lastRecordedPlayheadTime{0}; // Avoids conv between juce/std types
    bool isPlaying{false};
  } playState_;
  std::mutex playStateMtx_;

  AudioPluginAudioProcessorEditor* getCastEditor();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};

} // namespace audio_plugin
