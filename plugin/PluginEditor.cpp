#include "PluginEditor.h"
#include "PluginProcessor.h"

const int comboIdOffset = 1; // Used to ensure no entry has ID 0

namespace audio_plugin {
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(
    AudioPluginAudioProcessor &p)
    : AudioProcessorEditor(&p), processorRef_(p), graph_(p), table_(p) {
  // Make sure that before the constructor has finished, you've set the
  // editor's size to whatever you need it to be.
  setResizable(true, true); 
  setSize(1200, 800);

  auto regions = processorRef_.getAnalysisRegions();

  addChildComponent(graph_);
  addChildComponent(table_);

  uiToggle_.setToggleable(true);
  uiToggle_.setClickingTogglesState(true);
  uiToggle_.setToggleState(false, juce::NotificationType::dontSendNotification);
  uiToggle_.addListener(this);
  addAndMakeVisible(uiToggle_);
  updateAccordingToUiToggle();

  sampleRateHeading_.setEditable(false);
  sampleRateHeading_.setText("Input Sample Rate:",
                             juce::NotificationType::dontSendNotification);
  addAndMakeVisible(sampleRateHeading_);

  sampleRate_.setEditable(false);
  sampleRate_.setText(juce::String(static_cast<int>(p.getSampleRate())) + " Hz",
                      juce::NotificationType::dontSendNotification);
  addAndMakeVisible(sampleRate_);

  downsampleRateHeading_.setEditable(false);
  downsampleRateHeading_.setText("Processing Sample Rate:",
                                 juce::NotificationType::dontSendNotification);
  addAndMakeVisible(downsampleRateHeading_);

  downsampleRate_.setEditable(false);
  downsampleRate_.setText(juce::String(p.processingSampleRate) + " Hz",
                          juce::NotificationType::dontSendNotification);
  addAndMakeVisible(downsampleRate_);

  sampleCounterHeading_.setEditable(false);
  sampleCounterHeading_.setText("Sample Counter:",
                                juce::NotificationType::dontSendNotification);
  addAndMakeVisible(sampleCounterHeading_);

  sampleCounter_.setEditable(false);
  sampleCounter_.setText("---", juce::NotificationType::dontSendNotification);
  addAndMakeVisible(sampleCounter_);

  playheadPositionHeading_.setEditable(false);
  playheadPositionHeading_.setText(
      "Playhead Position:", juce::NotificationType::dontSendNotification);
  addAndMakeVisible(playheadPositionHeading_);

  playheadPosition_.setEditable(false);
  playheadPosition_.setText("---",
                            juce::NotificationType::dontSendNotification);
  addAndMakeVisible(playheadPosition_);

  regionsQueuedHeading_.setEditable(false);
  regionsQueuedHeading_.setText("Regions Queued:",
                                 juce::NotificationType::dontSendNotification);
  addAndMakeVisible(regionsQueuedHeading_);

  regionsQueued_.setEditable(false);
  updatePendingRegionsText();
  addAndMakeVisible(regionsQueued_);

  regionSizeHeading_.setEditable(false);
  regionSizeHeading_.setText("Region Size:",
                             juce::NotificationType::dontSendNotification);
  addChildComponent(regionSizeHeading_);
  if (regions) {
    //regionSizeHeading_.setVisible(true); // Hiding - service doesn't support anything other than 5000ms so don't allow user to change
  }

  regionSize_.setRange(3000, 15000, 100);
  regionSize_.setTextValueSuffix(" ms");
  regionSize_.addListener(this);
  addChildComponent(regionSize_);
  if (regions) {
    regionSize_.setValue(regions->getRegionSizeMs(),
                         juce::NotificationType::dontSendNotification);
    // regionSize_.setVisible(true); // Hiding - service doesn't support anything other than 5000ms so don't allow user to change
  }

  regionFreqHeading_.setEditable(false);
  regionFreqHeading_.setText("Region Frequency:",
                             juce::NotificationType::dontSendNotification);
  addChildComponent(regionFreqHeading_);
  if (regions) {
    regionFreqHeading_.setVisible(true);
  }

  regionFreq_.setRange(1000, 15000, 100);
  regionFreq_.setTextValueSuffix(" ms");
  regionFreq_.addListener(this);
  addChildComponent(regionFreq_);
  if (regions) {
    regionFreq_.setValue(regions->getRegionFreqMs(),
                         juce::NotificationType::dontSendNotification);
    regionFreq_.setVisible(true);
  }

  alignmentHeading_.setEditable(false);
  alignmentHeading_.setText("Region Alignment:",
                             juce::NotificationType::dontSendNotification);
  addChildComponent(alignmentHeading_);
  if (regions) {
    alignmentHeading_.setVisible(true);
  }

  alignment_.addItem("None", AnalysisRegions::Alignment::NONE + comboIdOffset);
  alignment_.addItem("Playback time zero", AnalysisRegions::Alignment::TIME_ZERO + comboIdOffset);
  alignment_.addItem("Playback begin", AnalysisRegions::Alignment::PLAYBACK_BEGIN + comboIdOffset);
  alignment_.addListener(this);
  addChildComponent(alignment_);
  if (regions) {
    alignment_.setSelectedId(regions->getAlignment() + comboIdOffset);
    alignment_.setVisible(true);
  }

  serviceAddressHeading_.setEditable(false);
  serviceAddressHeading_.setText("Service Address/Port:",
                             juce::NotificationType::dontSendNotification);
  addAndMakeVisible(serviceAddressHeading_);

  serviceAddress_.setTextToShowWhenEmpty("e.g, 127.0.0.1:12345",
                                         juce::Colours::grey);
  serviceAddress_.setText(p.getCommunicator()->getServiceAddress(), false);
  serviceAddress_.addListener(this);
  addAndMakeVisible(serviceAddress_);

  serviceAddressSet_.setButtonText("Set");
  serviceAddressSet_.setToggleable(false);
  serviceAddressSet_.addListener(this);
  addChildComponent(serviceAddressSet_);

  serviceAddressCancel_.setButtonText("Cancel");
  serviceAddressCancel_.setToggleable(false);
  serviceAddressCancel_.addListener(this);
  addChildComponent(serviceAddressCancel_);

  auto errorStrings = processorRef_.getCommunicator()->getConnectionErrors();
  if (!errorStrings.isEmpty()) {
    showConnectionErrors(errorStrings);
  }

  startTimerHz(20);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() {}

void AudioPluginAudioProcessorEditor::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void AudioPluginAudioProcessorEditor::resized() {
  // This is generally where you'll want to lay out the positions of any
  // subcomponents in your editor..
  auto area = getLocalBounds();

  auto header = area.removeFromTop(45).reduced(50, 10);
  uiToggle_.setBounds(header.removeFromRight(100));

  serviceAddressHeading_.setBounds(header.removeFromLeft(150));
  header.removeFromLeft(10);
  serviceAddress_.setBounds(header.removeFromLeft(250));
  header.removeFromLeft(10);
  serviceAddressSet_.setBounds(header.removeFromLeft(75));
  header.removeFromLeft(10);
  serviceAddressCancel_.setBounds(header.removeFromLeft(75));

  auto btmArea = area.removeFromBottom(100).reduced(50, 10);
  auto btmLeft = btmArea.removeFromLeft(400);
  auto btmRight = btmArea;

  const int rowHeight{20};
  const int sliderRowHeight{30};
  const int headingWidth{180};

  auto sampleRateArea = btmLeft.removeFromTop(rowHeight);
  sampleRateHeading_.setBounds(sampleRateArea.removeFromLeft(headingWidth));
  sampleRate_.setBounds(sampleRateArea);

  auto downsampleRateArea = btmLeft.removeFromTop(rowHeight);
  downsampleRateHeading_.setBounds(
      downsampleRateArea.removeFromLeft(headingWidth));
  downsampleRate_.setBounds(downsampleRateArea);

  auto sampleCounterArea = btmLeft.removeFromTop(rowHeight);
  sampleCounterHeading_.setBounds(
      sampleCounterArea.removeFromLeft(headingWidth));
  sampleCounter_.setBounds(sampleCounterArea);

  auto playheadPositionArea = btmLeft.removeFromTop(rowHeight);
  playheadPositionHeading_.setBounds(
      playheadPositionArea.removeFromLeft(headingWidth));
  playheadPosition_.setBounds(playheadPositionArea);

  auto pendingRegionsArea = btmRight.removeFromTop(rowHeight);
  regionsQueuedHeading_.setBounds(
      pendingRegionsArea.removeFromLeft(headingWidth));
  regionsQueued_.setBounds(pendingRegionsArea);

  /* Hiding - service doesn't support anything other than 5000ms so don't allow user to change
  auto regionSizeArea = btmRight.removeFromTop(sliderRowHeight);
  regionSizeHeading_.setBounds(regionSizeArea.removeFromLeft(headingWidth));
  regionSize_.setBounds(regionSizeArea);
  */

  auto regionFreqArea = btmRight.removeFromTop(sliderRowHeight);
  regionFreqHeading_.setBounds(regionFreqArea.removeFromLeft(headingWidth));
  regionFreq_.setBounds(regionFreqArea);

  auto alignmentArea = btmRight.removeFromTop(sliderRowHeight);
  alignmentHeading_.setBounds(alignmentArea.removeFromLeft(headingWidth));
  alignment_.setBounds(alignmentArea);

  auto mainArea = area.reduced(20, 5);
  table_.setBounds(mainArea);
  graph_.setBounds(mainArea);
}

void AudioPluginAudioProcessorEditor::updateSampleRate(SampleRate sampleRate) {
  sampleRate_.setText(juce::String(sampleRate) + " Hz",
                      juce::NotificationType::dontSendNotification);
}

void AudioPluginAudioProcessorEditor::timerCallback() {
  auto phPos = processorRef_.getPlayheadPosition();
  if (phPos.has_value()) {
    playheadPosition_.setText(juce::String(*phPos),
                              juce::NotificationType::dontSendNotification);
  } else {
    playheadPosition_.setText("---",
                              juce::NotificationType::dontSendNotification);
  }
  sampleCounter_.setText(juce::String(processorRef_.getSampleCounter()),
                         juce::NotificationType::dontSendNotification);
  auto regions = processorRef_.getAnalysisRegions();
  updatePendingRegionsText();
}

void AudioPluginAudioProcessorEditor::buttonClicked(juce::Button* button) {
  if (button == &uiToggle_) {
    updateAccordingToUiToggle();
  } else if (button == &serviceAddressSet_) {
    serviceAddressSetAction();
  } else if (button == &serviceAddressCancel_) {
    serviceAddressCancelAction();
  }
}

void AudioPluginAudioProcessorEditor::sliderValueChanged(juce::Slider* slider) {
  auto regions = processorRef_.getAnalysisRegions();
  assert(regions);
  if (regions) {
    regions->generateRegions(false);
    if (slider == &regionFreq_) {
      regions->setRegionFreqMs(static_cast<uint32_t>(regionFreq_.getValue()));
    } else if (slider == &regionSize_) {
      regions->setRegionSizeMs(static_cast<uint32_t>(regionSize_.getValue()));
    }
    regions->abortInProgress();
    regions->generateRegions(true);
  }
}

void AudioPluginAudioProcessorEditor::textEditorTextChanged(
    juce::TextEditor& textEditor) {
  serviceAddressSet_.setVisible(true);
  serviceAddressCancel_.setVisible(true);
}

void AudioPluginAudioProcessorEditor::textEditorReturnKeyPressed(
    juce::TextEditor& textEditor) {
  serviceAddressSetAction();
}

void AudioPluginAudioProcessorEditor::textEditorEscapeKeyPressed(
    juce::TextEditor& textEditor) {
  serviceAddressCancelAction();
}

void AudioPluginAudioProcessorEditor::comboBoxChanged(
    juce::ComboBox* comboBoxThatHasChanged) {
  if (comboBoxThatHasChanged == &alignment_) {
    auto regions = processorRef_.getAnalysisRegions();
    assert(regions);
    if (regions) {
      regions->generateRegions(false);
      regions->setAlignment(static_cast<AnalysisRegions::Alignment>(
          alignment_.getSelectedId() - comboIdOffset));
      regions->abortInProgress();
      regions->generateRegions(true);
    }
  }
}

void AudioPluginAudioProcessorEditor::serviceAddressSetAction() {
  serviceAddressSet_.setVisible(false);
  serviceAddressCancel_.setVisible(false);
  serviceAddress_.unfocusAllComponents();
  auto res = processorRef_.getCommunicator()->setServiceAddress(
      serviceAddress_.getText().toStdString());
  auto regions = processorRef_.getAnalysisRegions();
  assert(regions);
  if (regions) {
    regions->generateRegions(false);
    regions->abortInProgress();
    regions->generateRegions(true);
  }
  if (!res) {
    auto errorStrings = processorRef_.getCommunicator()->getConnectionErrors();
    showConnectionErrors(errorStrings);
  }
}

void AudioPluginAudioProcessorEditor::serviceAddressCancelAction() {
  serviceAddressSet_.setVisible(false);
  serviceAddressCancel_.setVisible(false);
  serviceAddress_.unfocusAllComponents();
  serviceAddress_.setText(processorRef_.getCommunicator()->getServiceAddress(), false);
}

void AudioPluginAudioProcessorEditor::showConnectionErrors(
    const juce::StringArray& errors) {
  const auto options = juce::MessageBoxOptions::makeOptionsOk(
      juce::MessageBoxIconType::WarningIcon, "Reconnection Error",
      errors.joinIntoString("\n"));
  messageBox_ = juce::AlertWindow::showScopedAsync(options, nullptr);
}

void AudioPluginAudioProcessorEditor::updateAccordingToUiToggle() {
  auto state = uiToggle_.getToggleState();
  uiToggle_.setButtonText(state ? "<< Graph View" : "Table View >>");
  graph_.setVisible(!state);
  table_.setVisible(state);
}

void AudioPluginAudioProcessorEditor::updatePendingRegionsText() {
  auto regions = processorRef_.getAnalysisRegions();
  if (regions) {
    auto numPending =
        regions->getNumRegionsInState(Region::State::PENDING);
    auto numInProgress = regions->getNumRegionsInState(
        Region::State::IN_PROGRESS);
    regionsQueued_.setText(juce::String(numPending + numInProgress),
                            juce::NotificationType::dontSendNotification);
  } else {
    regionsQueued_.setText("---",
                            juce::NotificationType::dontSendNotification);
  }
}

} // namespace audio_plugin
