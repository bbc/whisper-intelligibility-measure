#pragma once

#include "PluginProcessor.h"
#include "GuiComponents/Graph.h"
#include "GuiComponents/ResultsTable.h"
#include "Types.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace audio_plugin {

class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer,
                                        juce::Button::Listener,
                                        juce::Slider::Listener,
                                        juce::TextEditor::Listener,
                                        juce::ComboBox::Listener

{
public:
  explicit AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &);
  ~AudioPluginAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  void updateSampleRate(SampleRate sampleRate);

private:
  // This reference is provided as a quick way for your editor to
  // access the processor object that created it.
  AudioPluginAudioProcessor &processorRef_;

  void timerCallback() override;
  void buttonClicked(juce::Button* button) override;
  void sliderValueChanged(juce::Slider* slider) override;
  void textEditorTextChanged(juce::TextEditor& textEditor) override;
  void textEditorReturnKeyPressed(juce::TextEditor& textEditor) override;
  void textEditorEscapeKeyPressed(juce::TextEditor& textEditor) override;
  void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;

  void serviceAddressSetAction();
  void serviceAddressCancelAction();

  void showConnectionErrors(const juce::StringArray& errors);

  void updateAccordingToUiToggle();

  ui::GraphPane graph_;
  ui::ResultsTable table_;
  juce::TextButton uiToggle_;
  juce::Label sampleRateHeading_;
  juce::Label sampleRate_;
  juce::Label downsampleRateHeading_;
  juce::Label downsampleRate_;
  juce::Label sampleCounterHeading_;
  juce::Label sampleCounter_;
  juce::Label playheadPositionHeading_;
  juce::Label playheadPosition_;
  juce::Label regionsQueuedHeading_;
  juce::Label regionsQueued_;
  juce::Label regionSizeHeading_;
  juce::Slider regionSize_;
  juce::Label regionFreqHeading_;
  juce::Slider regionFreq_;
  juce::Label serviceAddressHeading_;
  juce::TextEditor serviceAddress_;
  juce::TextButton serviceAddressSet_;
  juce::TextButton serviceAddressCancel_;
  juce::Label alignmentHeading_;
  juce::ComboBox alignment_;

  juce::ScopedMessageBox messageBox_;

  void updatePendingRegionsText();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
} // namespace audio_plugin
