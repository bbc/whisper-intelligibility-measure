#include "ResultsTable.h"
#include "Utils.h"
#include <cassert>
#include <algorithm>
#include <iterator>
#include <set>
#include <optional>

namespace {

template <typename OuterKey, typename InnerKey, typename T>
std::optional<T> getNestedValue(
    const std::map<OuterKey, std::map<InnerKey, T>>& nestedMap,
    const OuterKey& outerKey,
    const InnerKey& innerKey) {
  auto outerIt = nestedMap.find(outerKey);
  if (outerIt != nestedMap.end()) {
    auto innerIt = outerIt->second.find(innerKey);
    if (innerIt != outerIt->second.end()) {
      return innerIt->second;
    }
  }
  return std::nullopt;
}

template <typename T>
std::optional<T> getElementAtIndex(const std::set<T>& container, size_t index) {
  if (index >= container.size()) {
    return std::nullopt;  // Return empty optional if index is out of bounds
  }

  auto it = container.begin();
  std::advance(it, index);  // Move iterator to the desired index
  return *it;
}

}

using namespace audio_plugin::ui;

ResultsTable::ResultsTable(AudioPluginAudioProcessor& processorRef)
    : processorRef_(processorRef) {

  table_.setColour(juce::ListBox::outlineColourId, juce::Colours::grey);
  table_.setOutlineThickness(1);
  table_.setHeaderHeight(30);
  addAndMakeVisible(table_);

  heading_.setEditable(false);
  heading_.setText("Playback Results",
                   juce::NotificationType::dontSendNotification);
  heading_.setFont(heading_.getFont().boldened().withHeight(20));
  heading_.setJustificationType(juce::Justification::bottomLeft);
  addAndMakeVisible(heading_);

  text_.setEditable(false);
  text_.setText("Results will auto-reset if settings/alignment changes.",
                   juce::NotificationType::dontSendNotification);
  text_.setJustificationType(juce::Justification::bottomLeft);
  addAndMakeVisible(text_);

  clearButton_.setButtonText("Clear");
  clearButton_.setToggleable(false);
  clearButton_.addListener(this);
  addAndMakeVisible(clearButton_);

  startTimer(100);
}

int ResultsTable::getNumRows() {
  // Row 0 to 2 will be for most current result,
  // row 3 will be for "History" text
  return latestResultsMaxRows_ + 4;
}

void ResultsTable::paintRowBackground(juce::Graphics& g,
                                        int rowNumber,
                                        int width,
                                        int height,
                                        bool rowIsSelected) {}

void ResultsTable::paintCell(juce::Graphics& g,
                               int rowNumber,
                               int columnId,
                               int width,
                               int height,
                               bool rowIsSelected) {
  if (rowNumber == 3) {
    g.setColour(juce::Colours::white);
    g.drawText("History", 0, 0, width, height,
               juce::Justification::centred, false);
  } else {
    std::optional<Region> region;
    {
      std::lock_guard mtx(latestResultsMtx_);

      if (rowNumber < 4) {
        // Get latest COMPLETE result
        for (auto playthroughOffsetIt =
                 latestResults_.playthroughOffsets.rbegin();
             playthroughOffsetIt != latestResults_.playthroughOffsets.rend();
             ++playthroughOffsetIt) {
          auto optRes =
              getNestedValue(latestResults_.regions,
                             static_cast<SampleCounter>(*playthroughOffsetIt),
                             static_cast<PlayheadTime>(columnId));
          if (optRes.has_value() &&
              optRes.value().analysisState == Region::State::COMPLETE) {
            region = optRes.value();
            break;
          }
        }
      } else {
        auto resultNumber = rowNumber - 4;
        auto rowSetIndex =
            static_cast<int>(latestResults_.playthroughOffsets.size()) - resultNumber - 1;
        auto rowValueOpt =
            getElementAtIndex(latestResults_.playthroughOffsets, rowSetIndex);

        if (rowValueOpt.has_value()) {
          auto rowValue = rowValueOpt.value();
          region = getNestedValue(latestResults_.regions,
                                  static_cast<SampleCounter>(rowValue),
                                  static_cast<PlayheadTime>(columnId));
        }
      }
    }

    if (region) {
      if (region->analysisState == Region::State::COMPLETE) {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::teal);
        if (rowNumber < 4) {
          float boxRes{0.f};
          if (rowNumber == 0) {
            // Result from 0.67 to 1.0
            boxRes = (region->analysisResult - 0.67f) / 0.33f;
          } else if (rowNumber == 1) {
            // Result from 0.33 to 0.67
            boxRes = (region->analysisResult - 0.33f) / 0.34f;
          } else if (rowNumber == 2) {
            // Result from 0.0 to 0.33
            boxRes = region->analysisResult / 0.33f;
          }
          if (boxRes > 0.f) {
            if (boxRes > 1.f)
              boxRes = 1.f;
            g.fillRect(0.f, static_cast<float>(height) * (1.f - boxRes),
                       static_cast<float>(width), static_cast<float>(height));
          }
        } else {
          g.fillRect(0.f, 0.f,
                     static_cast<float>(width) * region->analysisResult,
                     static_cast<float>(height));
        }
        if (rowNumber == 1 || rowNumber >= 4) {
          g.setColour(juce::Colours::white);
          g.drawText(juce::String(region->analysisResult, 3), 0, 0, width,
                     height, juce::Justification::centred, false);
        }
      } else {
        g.fillAll(juce::Colours::darkred);
      }
    }
  }
}

void ResultsTable::resized() {
  auto area = getLocalBounds();
  auto header = area.removeFromTop(30);
  heading_.setBounds(header.removeFromLeft(200));
  clearButton_.setBounds(
      header.removeFromRight(75).withSizeKeepingCentre(75, 20));
  text_.setBounds(header);
  area.removeFromTop(5);
  table_.setBounds(area);
}

void ResultsTable::timerCallback() {
  stopTimer();

  bool doRepaint{false};
  bool doColumnReset{false};
  std::set<PlayheadTime> columns;
  auto regionAnalyser = processorRef_.getAnalysisRegions();
  assert(regionAnalyser);

  {
    std::lock_guard mtx(latestResultsMtx_);
    auto currentUpdateCounter = regionAnalyser->getResultsUpdateCount();
    if (currentUpdateCounter != latestResultsUpdateCount_) {
      auto oldCols = latestResults_.playheadStartTimes;
      latestResults_ = regionAnalyser->getResults();
      latestResultsMaxRows_ = latestResults_.playthroughOffsets.size();
      latestResultsUpdateCount_ = currentUpdateCounter;
      if (latestResults_.playheadStartTimes != oldCols) {
        doColumnReset = true;
        columns = latestResults_.playheadStartTimes;
      }
      doRepaint = true;
    }
  }

  if (doColumnReset) {
    auto regionSize = regionAnalyser->getRegionSizeSamples();
    auto sampleRate = regionAnalyser->getReferenceSampleRate();
    table_.getHeader().removeAllColumns();
    for (auto const& colStartTime : columns) {
      auto startStr = formatTime(colStartTime, sampleRate);
      auto endStr = formatTime(colStartTime + regionSize, sampleRate);
      table_.getHeader().addColumn(startStr + "\n- " + endStr, colStartTime,
                                   100, 100, 100,
                                   juce::TableHeaderComponent::ColumnPropertyFlags::visible);
    }
  }

  if (doRepaint) {
    juce::MessageManager::callAsync([&]() { 
      table_.updateContent();
      repaint();
    });
  }

  startTimer(100);
}

void ResultsTable::buttonClicked(juce::Button* button) {
  if (button == &clearButton_) {
    auto regionAnalyser = processorRef_.getAnalysisRegions();
    if (regionAnalyser) {
      regionAnalyser->resetResults();
    }
  }
}
