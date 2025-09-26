#pragma once

#include<juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "Types.h"
#include <mutex>
#include <atomic>

namespace audio_plugin {
namespace ui {

class ResultsTable : public juce::Component,
                     juce::TableListBoxModel,
                     private juce::Timer,
                     juce::Button::Listener {
public:
  ResultsTable(AudioPluginAudioProcessor& processorRef);
  int getNumRows() override;
  void paintRowBackground(juce::Graphics& g,
                          int rowNumber,
                          int width,
                          int height,
                          bool rowIsSelected) override;
  void paintCell(juce::Graphics& g,
                 int rowNumber,
                 int columnId,
                 int width,
                 int height,
                 bool rowIsSelected);
  void resized() override;

private:
  AudioPluginAudioProcessor& processorRef_;
  juce::TableListBox table_{"ResultsTable", this};
  juce::Label heading_;
  juce::Label text_;
  juce::TextButton clearButton_;

  void timerCallback() override;
  void buttonClicked(juce::Button* button) override;

  std::mutex latestResultsMtx_;
  uint64_t latestResultsUpdateCount_{0};
  PlaybackResults::Results latestResults_;
  std::atomic<int> latestResultsMaxRows_{0};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResultsTable)
};

}  // namespace ui
}  // namespace audio_plugin