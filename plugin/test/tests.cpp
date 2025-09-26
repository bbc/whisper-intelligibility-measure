#include <PluginProcessor.h>
#include <gtest/gtest.h>

namespace audio_plugin_test {
TEST(AudioProcessor, Test1) {
  audio_plugin::AudioPluginAudioProcessor processor{};
}
//TEST(AudioProcessor, Test2) {
//  audio_plugin::AudioPluginAudioProcessor processor{};
//  throw std::exception();
//}
} // namespace audio_plugin_test
