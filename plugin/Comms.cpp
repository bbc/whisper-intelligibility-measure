#include "Comms.h"
#include "Utils.h"
#include <cstring>  // For memcpy
#include <chrono>
#include <vector>
#include <span>
#include <juce_data_structures/juce_data_structures.h> 

namespace audio_plugin {

ServiceCommunicator::ServiceCommunicator()
    : context_{1}, requester_{context_, ZMQ_DEALER} {
  identity_ = generateUniqueID();
  requester_.setsockopt(ZMQ_IDENTITY, identity_.c_str(), identity_.length());
  // Allow only 1 message to be queued
  // This can be used for connection verification
  // and to prevent spamming the server on reconnect
  int snd_hwm = 1;
  requester_.setsockopt(ZMQ_SNDHWM, &snd_hwm, sizeof(snd_hwm));
}

ServiceCommunicator::~ServiceCommunicator() {
  std::lock_guard mtx(mtx_);
  // ZMQ Cleanup steps
  try {
    // Close the socket
    requester_.close();
    // Terminate the context
    context_.shutdown();
    context_.close();
  } catch (const zmq::error_t& e) {
    std::cerr << "Error during cleanup: " << e.what() << std::endl;
  }
}

bool ServiceCommunicator::setServiceAddress(const std::string& address) {
  std::lock_guard mtx(mtx_);
  reconnectionErrors_.clear();
  if (!address_.empty()) {
    try {
      requester_.disconnect(address_);
    } catch (const zmq::error_t& e) {
      std::cerr << "Error during disconnect: " << e.what() << std::endl;
      reconnectionErrors_.add(juce::String("Disconnect: ") + e.what());
      // Although we might have a disconnection error, 
      // we're not going to return false unless connect fails
      // since that's all we're really interested in.
    }
  }
  if (address.empty()) {
    return false;
  }
  try {
    requester_.connect(std::string("tcp://") + address);
    address_ = address;
  } catch (const zmq::error_t& e) {
    std::cerr << "Error during connect: " << e.what() << std::endl;
    address_.clear();
    reconnectionErrors_.add(juce::String("Connect: ") + e.what());
    return false;
  }
  return true;
}

std::string ServiceCommunicator::getServiceAddress() {
  std::lock_guard mtx(mtx_);
  return address_;
}

juce::StringArray ServiceCommunicator::getConnectionErrors() {
  return reconnectionErrors_;
}

bool ServiceCommunicator::readyToSend() {
  std::lock_guard mtx(mtx_);
  if (address_.empty()) {
    return false;
  }

  // Check for socket events to detect disconnection
  int events = 0;
  size_t events_size = sizeof(events);
  requester_.getsockopt(ZMQ_EVENTS, &events, &events_size);

  if (!(events & ZMQ_POLLOUT)) {
    // Socket is not ready for sending
    return false;
  }
  return true;
}

bool ServiceCommunicator::sendRequest(
    const TimePoint& start,
    const SampleCounter length,
    std::shared_ptr<MonoCircularBuffer> readBuff) {
  std::lock_guard mtx(mtx_);

  const int64_t reqId{start.sampleCounter};
  const size_t reqIdElmCount = sizeof(int64_t) / sizeof(float);
  const size_t floatBufferSize = reqIdElmCount + length;
  std::vector<float> buff(floatBufferSize);
  // Copy the 64 bits of id directly into the first two float elements
  std::memcpy(buff.data(), &reqId, sizeof(reqId));
  // Fill the remaining elements with buffer samples
  std::span<float> samplesArea(buff.data() + reqIdElmCount,
                               buff.size() - reqIdElmCount);
  readBuff->getSamples(start, samplesArea);
  zmq::const_buffer zmqBuff(buff.data(), floatBufferSize * sizeof(float));
  zmq::send_result_t res;
  try {
    res = requester_.send(zmqBuff, zmq::send_flags::dontwait);
  } catch (const zmq::error_t& e) {
    std::cout << zmq_errno() << std::endl;
    return false;
  }
  if (!res.has_value()) {
      return false;
  }
  outstandingReplies_++;
  return true;
}

std::optional<ServiceCommunicator::Response>
ServiceCommunicator::getResponse() {
  std::lock_guard mtx(mtx_);
  if (outstandingReplies_ > 0) {
    // Polling for replies with a 0 ms timeout (non-blocking, immediate check)
    zmq::pollitem_t items[] = {{requester_, 0, ZMQ_POLLIN, 0}};
    zmq::poll(items, 1, std::chrono::milliseconds(0));

    if (items[0].revents & ZMQ_POLLIN) {
      zmq::message_t msg;

      // Receive the reply
      auto res = requester_.recv(msg, zmq::recv_flags::none);
      if (res.has_value()) {
        std::string jsonString(static_cast<const char*>(msg.data()),
                               msg.size());
        juce::var json = juce::JSON::parse(jsonString);
        if (json.isObject()) {
          Response response;
          if (json.hasProperty("request_id") && json["request_id"].isInt()) {
            response.reqId = static_cast<juce::int64>(json["request_id"]);
            response.success = false; // Default - we'll correct this unless "error" in response or result field is missing/invalid
            if (json.hasProperty("result") &&
                json["result"].isArray()) {
              auto resultsArray = json["result"].getArray();
              if (resultsArray->size() > 0) {
                auto resultElement = resultsArray->begin();
                if (resultElement->isDouble()) {
                  response.success = true;
                  response.result = *resultElement;
                }
              }
            }
            if (json.hasProperty("error")) {
              // We don't need to read this. The very presence of the field means something went wrong
              response.success = false;
            }
            return response;
          }
        }
      }
    }
  }

  return std::optional<Response>();
}

}  // namespace audio_plugin