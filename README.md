# Whisper Intelligibility Measure Plugin & Service

Implementation of Whisper for speech intelligibility measurement in a VST3 plugin using a backend service.

The intelligibility model is implemented as a separate service to the VST plugin. This provides two key advantages; it allows the model to run remotely on hardware optimised for ML, and it allows other applications of the model to be built quickly and easily by interfacing with the same service. Both the plugin and the service are included in this repository.

The service running the model and providing the API is written in Python. The VST plugin is written in C++ using the JUCE framework. They communicate with each other using ZMQ and the ROUTER-DEALER pattern.

## Plugin

Build using CMake from the `CMakeLists.txt` script in the root directory of the repository.  Tested working on Windows 11 x86-64, VS 2022, although other platforms and dev environments should be supported. 

The CMake script will download the JUCE, googletest and ZMQ dependencies automatically using CPM for the plugin. For ZMQ, we are using libzmq with the cppzmq wrapper.

JUCE is used as the audio plugin framework. This defines several `WhisperIntelligibilityPlugin*` targets. A VST3 plugin is produced from these targets as well as a standalone application. 

googletest is used as a testing framework. Tests are defined in the `plugin-tests` target.

## Service

The Python backend service is located in `whisper/python-service/si_service.py`.

pip is supported for collecting the necessary dependencies - run `pip install -r requirements.txt` from within the `whisper/python-service` directory. The only remaining dependency is the model itself which should be pointed to by the `defaults.yaml` file.

`defaults.yaml` can also specify the port that the service will listen for connections on and the pool size for the model (i.e, the number of concurrent analyses that can be performed.)

### Service Testing

The Python backend service can be fed an audio file to test functionality by using the `whisper/python-service/audio_broadcaster.py` script.

This script will read the `defaults.yaml` to learn which port to send data to. An optional `ip_address` field can also be specified if the backend service is not running on the same machine.

### Service Simulation

To test the functionality of the plugin without launching the real backend service, there is a service simulator script in `whisper/python-service-simulator/si_service.py`. pip is supported for collecting the necessary dependencies - run `pip install -r requirements.txt` from within the `whisper/python-service-simulator` directory.

This script simply responds with random scores at random delays, and with the occasional rejection.

This can also be configured with a `defaults.yaml` file.

## Messaging System

Messaging between the plugin (or audio_broadcaster.py script) and the service is over TCP using ZMQ with the ROUTER-DEALER pattern.

### Requests

The message format for requests is an 8-byte "Request ID" (64-bit unsigned little-endian integer), followed by audio data. This allows for very efficient handling of audio data by potentially avoiding data copying involved in placing the data in a containerised structure.

- The Request ID is completely arbitrary and only used to allow the requester to determine which request a response belongs to, since responses may arrive out of sequence due to parrellelisation of analysis. Although arbitrary, the Request ID is considered to be a 64-bit unsigned little-endian integer when it is returned in the response JSON.

- The audio data should be sequential samples of audio. These should be mono (i.e, not interleaved with other channels) 32-bit float samples at 16kHz sampling frequency. A chunk size of 80,000 samples (i.e, 5 seconds of audio) is recommended per request.

### Responses

The message structure of a response is stringified JSON. A successful response will look as follows;
```
{
    request_id: 12345,
    result: [ 0.6789 ]
}
```
Should a response need to convey an error state, it will contain an `error` field. This should contain a string description of the error, but the very presence of an `error` field regardless of value is an indication of error state. `request_id` and `result` may or may not be present in this structure depending on the circumstances of the error.

### Attribution
This project uses a [model architecture](https://claritychallenge.org/clarity2023-workshop/papers/Clarity_2023_CPC2_paper_mogridge.pdf) developed as part of the [2nd Clarity Prediction Challenge](https://claritychallenge.org/docs/cpc2/cpc2_intro) and was presented at the [2023 Clarity Challenge Workshop](https://claritychallenge.org/clarity2023-workshop/). The project page can be found [here](https://github.com/RhiM1/CPC2_challenge).

### License

Please see the separate LICENSE.md for license details, in particular please note the requirements of the JUCE and Steinberg licensing agreements before building or distributing this project.
