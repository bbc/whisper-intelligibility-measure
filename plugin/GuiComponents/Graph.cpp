#include "Graph.h"
#include <algorithm>
#include <cassert>
#include "Utils.h"

using namespace audio_plugin::ui;


Graph::Graph(AudioPluginAudioProcessor& processorRef)
    : processorRef_(processorRef) {
  startTimerHz(30);
  auto circBuff = processorRef_.getCircularBuffer();
  assert(circBuff);
  if (circBuff) {
    samples_ = std::vector<float>(circBuff->getNumStoredSamples(), 0.f);
  }
  setOpaque(true);
}

void Graph::resized() {
  int width = getWidth();
  samples_ = std::vector<float>(width > 0 ? width * samplesPerLine_ : 0, 0.f);
  waveformColumns_ = std::vector<float>(width > 0 ? width : 0, 0.f);
}

void Graph::paint(juce::Graphics& g) {
  g.fillAll(juce::Colours::black);
  auto normalFont = g.getCurrentFont(); // Just use default
  auto boldFont = normalFont.boldened();

  // This is quite inefficient - we redraw the whole waveform every
  // time. This also causes the jitter in the waveform as the boundaries of
  // each `samplesPerLine` block will move on each redraw. We should just
  // cache the waveform image each time, request the region since the last draw
  // and push the existing image along. However, note this will blank the
  // waveform if the zoom level was changed. We need to repopulate all in that
  // case.
  // Also note that any regions graphics will need rendering fully on each
  // frame. Their state can change at any time and trying to cache with
  // appropriate invalidation would likely introduce so much complexity that it
  // wasn't worth optimising in the first place.

  auto buffMan = processorRef_.getBufferManager();
  auto circBuff = processorRef_.getCircularBuffer();
  auto regionAnalyser = processorRef_.getAnalysisRegions();

  assert(buffMan && circBuff && regionAnalyser);
  if (!buffMan || !circBuff || !regionAnalyser) {
    g.setFont(15.0f);
    g.setColour(juce::Colours::red);
    g.drawFittedText("An error occurred", getLocalBounds(),
                     juce::Justification::centred, 1);
    return;
  }

  // Proportions
  const float overlaps{(float)regionAnalyser->getRegionSizeSamples() /
                       (float)regionAnalyser->getRegionFreqSamples()};
  const int levels{
      (int)(overlaps + 1.5f)};  // +0.5 does round up,
                                // +1.0 adds an extra level to avoid butting up

  int pendingRegionAreaHeight =
      pendingRegionsMinHeightProportion_ * getHeight();
  int pendingRegionBarHeight =
      std::max(pendingRegionAreaHeight / levels, pendingRegionMinHeight_);
  pendingRegionAreaHeight = pendingRegionBarHeight * levels;
  int mainAreaHeight = getHeight() - pendingRegionAreaHeight;

  // Waveform
  auto dataTime = circBuff->getLatestSamples(
      samples_);  // Use dataTime to align whisper results with waveform

  // Precompute the absolute values of samples
  std::for_each(
      PREFERRED_EXEC(std::execution::par, samples_.begin(), samples_.end(),
                     [](float& sample) { sample = std::abs(sample); }));

  // Parallel loop to find the max sample for each line
  std::for_each(PREFERRED_EXEC(
      std::execution::par, waveformColumns_.begin(), waveformColumns_.end(), [&](float& line) {
        size_t i = &line - &waveformColumns_[0];  // Calculate the index based on the
                                       // pointer difference
        size_t start = i * samplesPerLine_;
        size_t end = start + samplesPerLine_;
        line =
            *std::max_element(samples_.begin() + start, samples_.begin() + end);
      }));

  g.setColour(colWaveform_);
  for (int x = 0; x < waveformColumns_.size(); ++x) {
    auto lineEndY = mainAreaHeight - (waveformColumns_[x] * mainAreaHeight);
    g.drawVerticalLine(x, lineEndY, mainAreaHeight);
  }

  // Draw Regions
  auto graphLeftSampleCounter =
      dataTime.sampleCounter - static_cast<SampleCounter>(getWidth() * samplesPerLine_);
  auto graphRightSampleCounter = dataTime.sampleCounter;
  auto regions = regionAnalyser->getRegions(graphLeftSampleCounter, graphRightSampleCounter);

  for (auto const& region : regions) {

    // Rules for Regions:
    /// If complete - show on graph, sized bar with background.
    /// If pending, in progress, or timed out - show on bars under graph
    /// In all cases: If not stale, in greys, otherwise in colours

    if (region.analysisState == Region::State::COMPLETE) {
      // On graph view
      // Init with stale colours
      juce::Colour regionBackground = colAnalysisNull_;
      juce::Colour regionOutline = colAnalysisResultStaleOutline_;
      juce::Colour regionFill = colAnalysisResultStaleFill_;
      if (!region.stale) {
        regionOutline = colAnalysisResultOutline_;
        regionFill = colAnalysisResultFill_;
        regionBackground = colAnalysisResultBackground_;
      }

      // Draw
      int ySplit = (1.f - region.analysisResult) * mainAreaHeight;
      auto left = getGraphX(region.start.sampleCounter, dataTime.sampleCounter);
      auto right = getGraphX(region.end.sampleCounter, dataTime.sampleCounter);
      ///Background
      g.setColour(regionBackground);
      g.fillRect(left, 0, right - left, ySplit);
      ///Bar
      juce::Rectangle area(left, ySplit, right - left, mainAreaHeight - ySplit);
      g.setColour(regionFill);
      g.fillRect(area);
      g.setColour(regionOutline);
      g.drawRect(area);
      auto [resultArea, timeRangeArea] = calcCompletedRegionTextArea(area);
      if (region.wasDuringPlayback) {
        g.setFont(normalFont);
        drawTimeRangeText(g, timeRangeArea, region.start, region.end);
      }
      g.setColour(juce::Colours::white);
      g.setFont(boldFont);
      g.drawText(juce::String(region.analysisResult, 3, false), resultArea,
                  juce::Justification::centred);

    } else if (region.analysisState == Region::State::PENDING ||
               region.analysisState == Region::State::IN_PROGRESS ||
               region.analysisState == Region::State::FAILURE ||
               region.analysisState == Region::State::TIMEOUT) {
      // On bar view
      // Init with 'Invalid' colours
      juce::Colour regionFill = colAnalysisRegionInvalidFill_;
      switch (region.analysisState) {
        case Region::State::PENDING:
          regionFill = colAnalysisRegionPendingFill_;
          break;
        case Region::State::IN_PROGRESS:
          regionFill = region.stale ? colAnalysisRegionStaleInProgressFill_
                                    : colAnalysisRegionInProgressFill_;
          break;
      }

      auto level = region.count % levels;
      int y = level * pendingRegionBarHeight;
      g.setColour(regionFill);
      auto left =
          getGraphX(region.start.sampleCounter, dataTime.sampleCounter);
      auto right =
          getGraphX(region.end.sampleCounter, dataTime.sampleCounter);
      juce::Rectangle area(left, mainAreaHeight + y, right - left,
                            pendingRegionBarHeight);
      g.fillRect(area);
      if (region.wasDuringPlayback) {
        g.setFont(normalFont);
        drawTimeRangeText(g, area, region.start, region.end);
      }
    }
  }

  // Figure out what playback lines to draw
  auto playbackRegion = buffMan->getPlaybackRegion();

  if (playbackRegion.start.has_value()) {
    auto playbackRegionTimePoint = toTimePoint(playbackRegion.start.value())
                                       .asSampleRate(dataTime.sampleRate);
    auto x = getGraphX(playbackRegionTimePoint.sampleCounter,
                       dataTime.sampleCounter);
    if (x >= 0 && x < getWidth()) {
      g.setColour(juce::Colours::limegreen);
      g.drawVerticalLine(x, mainAreaHeight, getHeight());
    }
  }

  if (playbackRegion.end.has_value()) {
    auto playbackRegionTimePoint = toTimePoint(playbackRegion.end.value())
                                       .asSampleRate(dataTime.sampleRate);
    auto x = getGraphX(playbackRegionTimePoint.sampleCounter,
                       dataTime.sampleCounter);
    if (x >= 0 && x < getWidth()) {
      g.setColour(juce::Colours::red);
      g.drawVerticalLine(x, mainAreaHeight, getHeight());
    }
  }

}

int Graph::getGraphDurationMs() {
  return calcGraphDurationMs(samplesPerLine_);
}

void Graph::zoomIn() {
  auto newSPL = samplesPerLine_ / 2;
  auto newDur = calcGraphDurationMs(newSPL);
  if (newDur >= 5000) {
    samplesPerLine_ = newSPL;
    resized();
  }
}

void Graph::zoomOut() {
  auto circBuff = processorRef_.getCircularBuffer();
  assert(circBuff);
  if (circBuff) {
    auto newSPL = samplesPerLine_ * 2;
    auto newDur = calcGraphDurationMs(newSPL);
    auto maxDur = circBuff->getDurationMs();
    if (newDur <= maxDur) {
      samplesPerLine_ = newSPL;
      resized();
    }
  }
}

void Graph::drawTimeRangeText(juce::Graphics& g,
                              const juce::Rectangle<int>& area,
                              const TimePoint& start,
                              const TimePoint& end) {
  juce::String startStr{"..."};
  if (start.playheadTime.has_value()) {
    startStr = formatTime(start.playheadTime.value(), start.sampleRate);
  }
  juce::String endStr{"..."};
  if (end.playheadTime.has_value()) {
    endStr = formatTime(end.playheadTime.value(), end.sampleRate);
  }
  juce::String timeString = startStr + " - " + endStr;
  g.setColour(juce::Colours::white);
  g.drawText(timeString, area, juce::Justification::centred);
}

int Graph::calcGraphDurationMs(size_t forSamplesPerLineValue) {
  auto buffMan = processorRef_.getBufferManager();
  assert(buffMan);
  if (buffMan) {
    auto numSamplesInView =
        forSamplesPerLineValue * static_cast<uint32_t>(getWidth());
    auto circBuffSampleRate = buffMan->getBufferSampleRate();
    return (numSamplesInView * 1000) / circBuffSampleRate;
  }
  return 0;
}

std::pair<juce::Rectangle<int>, juce::Rectangle<int>>
Graph::calcCompletedRegionTextArea(const juce::Rectangle<int>& inputArea) {
  // We should try to centre 2 lines according to regionTextLineHeight_, but
  // avoid bottoming out below inputArea
  auto totalReqHeight = 2 * regionTextLineHeight_;
  if (totalReqHeight > inputArea.getHeight()) {
    juce::Rectangle<int> line2{inputArea.getX(),
                               inputArea.getBottom() - regionTextLineHeight_,
                               inputArea.getWidth(), regionTextLineHeight_};
    juce::Rectangle<int> line1{inputArea.getX(),
                               line2.getY() - regionTextLineHeight_,
                               inputArea.getWidth(), regionTextLineHeight_};
    return {line1, line2};
  }
  auto midArea =
      inputArea.withSizeKeepingCentre(inputArea.getWidth(), totalReqHeight);
  juce::Rectangle<int> line1 = midArea.removeFromTop(regionTextLineHeight_);
  return {line1, midArea};
}

int Graph::getGraphX(SampleCounter forSc,
                     SampleCounter knownScAtGraphRightEdge) {
  int64_t scDiff = forSc - knownScAtGraphRightEdge;
  int64_t completeLines = scDiff / static_cast<int64_t>(samplesPerLine_);
  int64_t x = getWidth() + completeLines;
  int64_t rem = scDiff % static_cast<int64_t>(samplesPerLine_);
  if (rem < 0)
    x--;
  return x;
}

void Graph::timerCallback() {
  repaint();
}

GraphPane::GraphPane(AudioPluginAudioProcessor& processorRef)
    : graph_{processorRef} {

  zoomOut_.setButtonText("-");
  zoomOut_.setToggleable(false);
  zoomOut_.addListener(this);
  addAndMakeVisible(zoomOut_);

  zoomIn_.setButtonText("+");
  zoomIn_.setToggleable(false);
  zoomIn_.addListener(this);
  addAndMakeVisible(zoomIn_);

  lowTime_.setEditable(false);
  lowTime_.setJustificationType(juce::Justification::centredLeft);
  updateLowTime();
  addAndMakeVisible(lowTime_);

  highTime_.setEditable(false);
  highTime_.setText("T-0ms", juce::NotificationType::dontSendNotification);
  highTime_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(highTime_);

  addAndMakeVisible(graph_);
}

void GraphPane::resized() {
  auto area = getLocalBounds();
  auto topArea = area.removeFromTop(50);
  auto topLeft = topArea.removeFromLeft(topArea.getWidth() / 2);
  auto topRight = topArea;

  zoomOut_.setBounds(topLeft.removeFromRight(40).reduced(5, 10));
  zoomIn_.setBounds(topRight.removeFromLeft(40).reduced(5, 10));
  lowTime_.setBounds(topLeft);
  highTime_.setBounds(topRight);

  graph_.setBounds(area);

  updateLowTime();
}

void GraphPane::paint(juce::Graphics& g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

  g.setColour(juce::Colours::white);
  auto yStart = getGraphTop() - 40.f;
  g.drawVerticalLine(getGraphLeft(), yStart, getGraphTop());
  g.drawVerticalLine(getGraphRight(), yStart, getGraphTop());
}

void GraphPane::buttonClicked(juce::Button* button) {
  if (button == &zoomIn_) {
    graph_.zoomIn();
    updateLowTime();
  } else if (button == &zoomOut_) {
    graph_.zoomOut();
    updateLowTime();
  }
}

int GraphPane::getGraphLeft() {
  return graph_.getBoundsInParent().getX();
}

int GraphPane::getGraphRight() {
  return graph_.getBoundsInParent().getRight() - 1;
}

int GraphPane::getGraphTop() {
  return graph_.getBoundsInParent().getY();
}

void GraphPane::updateLowTime() {
  lowTime_.setText("T-" + juce::String(graph_.getGraphDurationMs()) + "ms",
                   juce::NotificationType::dontSendNotification);
}
