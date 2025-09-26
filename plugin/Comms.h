#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <mutex>
#include <optional>
#include <memory>
#include "CircularBuffer.h"
#include "Types.h"
#include <zmq.hpp>

namespace audio_plugin {

class MonoCircularBuffer;

class ServiceCommunicator {
public:
  ServiceCommunicator();
  ~ServiceCommunicator();

  bool setServiceAddress(const std::string& address);
  std::string getServiceAddress();
  juce::StringArray getConnectionErrors();

  struct Response {
    int64_t reqId;
    float result{0.f};
    bool success{true};
  };

  bool readyToSend();
  bool sendRequest(const TimePoint& start,
                   const SampleCounter length,
                   std::shared_ptr<MonoCircularBuffer> readBuff);
  std::optional<Response> getResponse();

private:
  std::mutex mtx_;
  std::string identity_;
  zmq::context_t context_;
  zmq::socket_t requester_;
  std::string address_;
  uint32_t outstandingReplies_{0};
  juce::StringArray reconnectionErrors_;
};

}  // namespace audio_plugin