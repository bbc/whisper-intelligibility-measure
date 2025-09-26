# CAUTION WITH THE LENGTH OF PLUGIN_PROJECT_NAME! 
# We are right on the limit and CI Windows builds fail with long names - possibly a path length thing, or the juce_vst3_helper having a short string buffer internally or something.
# e.g, "WhisperIntelligibilityPlugin" = OK, "WhisperIntelligibilityMeasurePlugin" = FAIL
set (PLUGIN_PROJECT_NAME "WhisperIntelligibilityPlugin") # CAUTION WITH THE LENGTH OF THIS!
set (PLUGIN_PRODUCT_NAME "Whisper Intelligibility Measure")
set (PLUGIN_VERSION "0.1.0")
set (PLUGIN_BUNDLE_ID "com.bbcrd-uos.WhisperPlugin")
set (PLUGIN_COMPANY_NAME "BBC R&D / University of Salford")

project(${PLUGIN_PROJECT_NAME} VERSION ${PLUGIN_VERSION}) # Version is needed by JUCE
set(CMAKE_CXX_STANDARD 20) # JUCE not working with C++23 on some platforms yet

# Write some temp files to make GitHub Actions / packaging easy
if ((DEFINED ENV{CI}))
    set (env_file "${PROJECT_SOURCE_DIR}/.env")
    message ("Writing ENV file for CI: ${env_file}")
    file(WRITE  "${env_file}" "PROJECT_NAME=${PLUGIN_PROJECT_NAME}\n")
    file(APPEND "${env_file}" "PRODUCT_NAME=${PLUGIN_PRODUCT_NAME}\n")
    file(APPEND "${env_file}" "VERSION=${PLUGIN_VERSION}\n")
    file(APPEND "${env_file}" "BUNDLE_ID=${PLUGIN_BUNDLE_ID}\n")
    file(APPEND "${env_file}" "COMPANY_NAME=${PLUGIN_COMPANY_NAME}\n")
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_XCODE_VERSION VERSION_GREATER_EQUAL 15)
    # Fix JUCE Warning:
    # If you are using Link Time Optimisation (LTO), the new linker introduced in Xcode 15 may produce a broken binary.
    # As a workaround, add either '-Wl,-weak_reference_mismatches,weak' or '-Wl,-ld_classic' to your linker flags.
    # Once you've selected a workaround, you can add JUCE_SILENCE_XCODE_15_LINKER_WARNING to your preprocessor definitions to silence this warning.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-ld_classic")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-ld_classic")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,-ld_classic")
    add_compile_definitions(JUCE_SILENCE_XCODE_15_LINKER_WARNING)
endif()

juce_add_plugin(${PLUGIN_PROJECT_NAME}
    COMPANY_NAME ${PLUGIN_COMPANY_NAME}
    BUNDLE_ID ${PLUGIN_BUNDLE_ID}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE "VAR " #VARious
    PLUGIN_CODE WIMP # Whisper Intelligibility Measure Plugin
    FORMATS VST3 Standalone
    PRODUCT_NAME ${PLUGIN_PRODUCT_NAME}
)

# JUCE cmake doesn't folder the resources lib for some reason
if(TARGET ${PLUGIN_PROJECT_NAME}_rc_lib)
   set_target_properties(${PLUGIN_PROJECT_NAME}_rc_lib PROPERTIES FOLDER ${PLUGIN_PROJECT_NAME})
endif()

set(HEADERS_PLUGIN
    GuiComponents/Graph.h
    GuiComponents/ResultsTable.h
    PluginEditor.h
    PluginProcessor.h
    AnalysisRegions.h
    CircularBuffer.h
    Comms.h
    Types.h
    Utils.h
)

set(SOURCES_PLUGIN
    GuiComponents/Graph.cpp
    GuiComponents/ResultsTable.cpp
    PluginEditor.cpp
    PluginProcessor.cpp
    AnalysisRegions.cpp
    CircularBuffer.cpp
    Comms.cpp
)

target_sources(${PLUGIN_PROJECT_NAME}
    PRIVATE
        ${SOURCES_PLUGIN}
        ${HEADERS_PLUGIN}
)

source_group("Headers" FILES ${HEADERS_PLUGIN})

target_include_directories(${PLUGIN_PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${DEPS_DIR}/cppzmq
)

target_link_libraries(${PLUGIN_PROJECT_NAME}
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        libzmq-static
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(${PLUGIN_PROJECT_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

set_target_properties(
    juce_vst3_helper
    PROPERTIES FOLDER Dependencies)

# Tests
enable_testing()
add_subdirectory(test)
