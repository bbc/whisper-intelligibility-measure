#pragma once

#include<juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include <optional>

namespace audio_plugin {
namespace ui {

class Graph : public juce::Component, private juce::Timer {
public:
  Graph(AudioPluginAudioProcessor& processorRef);

  void resized() override;
  void paint(juce::Graphics& g) override;

  int getGraphDurationMs();
  void zoomIn();
  void zoomOut();

private:
  AudioPluginAudioProcessor& processorRef_;
  std::vector<float> samples_;
  std::vector<float> waveformColumns_;

  void drawTimeRangeText(juce::Graphics& g,
                         const juce::Rectangle<int>& area,
                         const TimePoint& start,
                         const TimePoint& end);
  int calcGraphDurationMs(size_t forSamplesPerLineValue);
  std::pair<juce::Rectangle<int>, juce::Rectangle<int>>
  calcCompletedRegionTextArea(const juce::Rectangle<int>& inputArea);
  int getGraphX(SampleCounter forSc, SampleCounter knownScAtGraphRightEdge);
  size_t samplesPerLine_{256};

  void timerCallback() override;

  const float pendingRegionsMinHeightProportion_{0.1f};
  const int pendingRegionMinHeight_{1};

  const int regionTextLineHeight_{12};

  const juce::Colour colWaveform_{juce::Colours::teal};

  const juce::Colour colAnalysisNull_{juce::Colours::transparentBlack};

  const juce::Colour colAnalysisRegionInvalidFill_{
      juce::Colours::darkred};

  const juce::Colour colAnalysisRegionPendingFill_{juce::Colours::grey};

  const juce::Colour colAnalysisRegionInProgressFill_{
      juce::Colours::darkolivegreen};
  const juce::Colour colAnalysisRegionStaleInProgressFill_{
      juce::Colours::darkolivegreen.withAlpha(0.5f)};

  const juce::Colour colAnalysisResultOutline_{juce::Colours::blue} ;
  const juce::Colour colAnalysisResultFill_{juce::Colours::blue.withAlpha(0.3f)};
  const juce::Colour colAnalysisResultBackground_{
      juce::Colours::white.withAlpha(0.1f)};
  const juce::Colour colAnalysisResultStaleOutline_{juce::Colours::grey};
  const juce::Colour colAnalysisResultStaleFill_{
      juce::Colours::grey.withAlpha(0.5f)};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Graph)
};

class GraphPane : public juce::Component, private juce::Button::Listener {
public:
  GraphPane(AudioPluginAudioProcessor& processorRef);

  void resized() override;
  void paint(juce::Graphics& g) override;

private:
  Graph graph_;
  juce::Label lowTime_;
  juce::Label highTime_;
  juce::TextButton zoomIn_;
  juce::TextButton zoomOut_;

  void buttonClicked(juce::Button* button) override;

  int getGraphLeft();
  int getGraphRight();
  int getGraphTop();
  void updateLowTime();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphPane)
};

}  // namespace ui
}  // namespace audio_plugin