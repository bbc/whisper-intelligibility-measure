#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cassert>
#include <algorithm>
#include <random>

namespace audio_plugin {
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor(
          BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
              )
{
  comms_ = std::make_shared<ServiceCommunicator>();
  buffMan_ = std::make_shared<Buff>(static_cast<SampleRate>(48000),
                                    static_cast<uint16_t>(1024),
                                    processingSampleRate, comms_);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor() {
  buffMan_.reset();
}

const juce::String AudioPluginAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool AudioPluginAudioProcessor::acceptsMidi() const {
#if JucePlugin_WantsMidiInput
  return true;
#else
  return false;
#endif
}

bool AudioPluginAudioProcessor::producesMidi() const {
#if JucePlugin_ProducesMidiOutput
  return true;
#else
  return false;
#endif
}

bool AudioPluginAudioProcessor::isMidiEffect() const {
#if JucePlugin_IsMidiEffect
  return true;
#else
  return false;
#endif
}

double AudioPluginAudioProcessor::getTailLengthSeconds() const {
  return 0.0; // Ideally we would return the region size, but this has to be const!
}

int AudioPluginAudioProcessor::getNumPrograms() {
  return 1;  // NB: some hosts don't cope very well if you tell them there are 0
             // programs, so this should be at least 1, even if you're not
             // really implementing programs.
}

int AudioPluginAudioProcessor::getCurrentProgram() {
  return 0;
}

void AudioPluginAudioProcessor::setCurrentProgram(int index) {
  juce::ignoreUnused(index);
}

const juce::String AudioPluginAudioProcessor::getProgramName(int index) {
  juce::ignoreUnused(index);
  return {};
}

void AudioPluginAudioProcessor::changeProgramName(int index,
                                                  const juce::String& newName) {
  juce::ignoreUnused(index, newName);
}

void AudioPluginAudioProcessor::prepareToPlay(double sampleRate,
                                              int samplesPerBlock) {
  // Use this method as the place to do any pre-playback
  // initialisation that you need..
  if (sampleRate != lastKnownSampleRate_) {
    lastKnownSampleRate_ = sampleRate;
    SampleRate castSampleRate = static_cast<SampleRate>(sampleRate);
    buffMan_ = std::make_shared<Buff>(castSampleRate,
                                      static_cast<uint16_t>(samplesPerBlock),
                                      processingSampleRate, comms_);
    auto regions = getAnalysisRegions();
    assert(regions);
    if (regions) {
      regions->restartRegions();
    }
    if (auto e = getCastEditor()) {
      e->updateSampleRate(castSampleRate);
    }
  }
  {
    std::lock_guard<std::mutex> lock(playStateMtx_);
    playState_.isPlaying = false;
    playState_.lastRecordedPlayheadTime.reset();
  }
}

void AudioPluginAudioProcessor::releaseResources() {
  // When playback stops, you can use this as an opportunity to free up any
  // spare memory, etc.
  std::lock_guard<std::mutex> lock(playStateMtx_);
  playState_.isPlaying = false;
  playState_.lastRecordedPlayheadTime.reset();
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const {
#if JucePlugin_IsMidiEffect
  juce::ignoreUnused(layouts);
  return true;
#else
  // This is the place where you check if the layout is supported.
  // In this template code we only support mono or stereo.
  // Some plugin hosts, such as certain GarageBand versions, will only
  // load plugins that support stereo bus layouts.
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
      layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

    // This checks if the input layout matches the output layout
#if !JucePlugin_IsSynth
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    return false;
#endif

  return true;
#endif
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages) {
  juce::ignoreUnused(midiMessages);
  assert(buffMan_);

  juce::ScopedNoDenormals noDenormals;
  auto totalNumInputChannels = getTotalNumInputChannels();
  auto totalNumOutputChannels = getTotalNumOutputChannels();

  // In case we have more outputs than inputs, this code clears any output
  // channels that didn't contain input data, (because these aren't
  // guaranteed to be empty - they may contain garbage).
  for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    buffer.clear(i, 0, buffer.getNumSamples());

  // Determine playback state (Real playback or just set to "run when stopped"?)
  juce::Optional<int64_t> phTime;
  auto phPos = getPlayHead()->getPosition();
  bool isNowPlaying{false}; 
  if (phPos.hasValue()) {
    isNowPlaying = phPos->getIsPlaying();
    phTime = phPos->getTimeInSamples();
  }

  TimePoint blockStartTime;
  {
    std::lock_guard<std::mutex> lock(playStateMtx_);
    if (isNowPlaying != playState_.isPlaying) {
      playState_.isPlaying = isNowPlaying;
      if (isNowPlaying) {
        // Just started playing. Mark old completed regions stale.
        auto regions = getAnalysisRegions();
        assert(regions);
        if (regions) {
          regions->updateAsStale();
        }
        buffMan_->justStarted();
      } else {
        buffMan_->justStopped();
      }
    }
    playState_.lastRecordedPlayheadTime = phTime;
       
    blockStartTime = TimePoint{
        static_cast<SampleRate>(getSampleRate()),
        static_cast<SampleCounter>(playState_.sampleCounter), std::nullopt};
    playState_.sampleCounter += buffer.getNumSamples();

    if (phTime.hasValue() && isNowPlaying) {
      blockStartTime.playheadTime = *phTime;
    }
  }
  buffMan_->updateFrom(buffer, blockStartTime);
}

bool AudioPluginAudioProcessor::hasEditor() const {
  return true;  // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor() {
  return new AudioPluginAudioProcessorEditor(*this);
}

void AudioPluginAudioProcessor::getStateInformation(
    juce::MemoryBlock& destData) {
  // You should use this method to store your parameters in the memory block.
  // You could do that either as raw data, or use the XML or ValueTree classes
  // as intermediaries to make it easy to save and load complex data.
  std::unique_ptr<juce::XmlElement> xml(new juce::XmlElement("PluginSettings"));
  xml->setAttribute("serviceAddress", comms_->getServiceAddress());
  copyXmlToBinary(*xml, destData);
}

void AudioPluginAudioProcessor::setStateInformation(const void* data,
                                                    int sizeInBytes) {
  // You should use this method to restore your parameters from this memory
  // block, whose contents will have been created by the getStateInformation()
  // call.
  std::unique_ptr<juce::XmlElement> xmlState(
      getXmlFromBinary(data, sizeInBytes));
  if (xmlState && xmlState->hasTagName("PluginSettings")) {
    if (xmlState->hasAttribute("serviceAddress")) {
      auto serviceAddress =
          xmlState->getStringAttribute("serviceAddress", "").toStdString();
      comms_->setServiceAddress(serviceAddress);
    }
  }
}

bool AudioPluginAudioProcessor::isStandalone() {
  return pluginHostType_.getPluginLoadedAs() ==
         AudioProcessor::wrapperType_Standalone;
}

SampleCounter AudioPluginAudioProcessor::getSampleCounter() {
  std::lock_guard<std::mutex> lock(playStateMtx_);
  return playState_.sampleCounter;
}

std::optional<SampleCounter> AudioPluginAudioProcessor::getPlayheadPosition() {
  std::lock_guard<std::mutex> lock(playStateMtx_);
  if (playState_.lastRecordedPlayheadTime.hasValue())
    return *playState_.lastRecordedPlayheadTime;
  return std::nullopt;
}

std::shared_ptr<Buff> AudioPluginAudioProcessor::getBufferManager() {
  return buffMan_;
}

std::shared_ptr<MonoCircularBuffer>
AudioPluginAudioProcessor::getCircularBuffer() {
  auto buffMan = buffMan_;
  if (!buffMan)
    return nullptr;
  return buffMan->getCircularBuffer();
}

std::shared_ptr<AnalysisRegions>
AudioPluginAudioProcessor::getAnalysisRegions() {
  auto buffMan = buffMan_;
  if (!buffMan)
    return nullptr;
  return buffMan->getAnalysisRegions();
}

std::shared_ptr<ServiceCommunicator>
AudioPluginAudioProcessor::getCommunicator() {
  return comms_;
}

AudioPluginAudioProcessorEditor* AudioPluginAudioProcessor::getCastEditor() {
  if (auto e = getActiveEditor()) {
    return dynamic_cast<AudioPluginAudioProcessorEditor*>(e);
  }
  return nullptr;
}

}  // namespace audio_plugin

// This creates new instances of the plugin.
// This function definition must be in the global namespace.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new audio_plugin::AudioPluginAudioProcessor();
}
