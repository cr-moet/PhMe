cmake_minimum_required(VERSION 3.22)
project(PhMe VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)

# MSVC: link the C++ runtime statically, so the VST3 also runs on computers
# without the "Visual C++ Redistributable" installed
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(libs/JUCE)

# JUCE's AudioPluginHost as test host for debugging the VST3 (build target "AudioPluginHost")
option(PHME_BUILD_PLUGIN_HOST "Add JUCE's AudioPluginHost to debug the VST3" ON)
if(PHME_BUILD_PLUGIN_HOST)
    add_subdirectory(libs/JUCE/extras/AudioPluginHost)
endif()

juce_add_plugin(PhMe
    COMPANY_NAME "cr-moet"
    PLUGIN_MANUFACTURER_CODE Crmt
    PLUGIN_CODE Phme
    FORMATS VST3
    PRODUCT_NAME "PhMe"
    DESCRIPTION "Phase Meter"
    VST3_CATEGORIES Fx Analyzer
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(PhMe PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp)

target_compile_definitions(PhMe PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Source files are UTF-8
target_compile_options(PhMe PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(PhMe
    PRIVATE juce::juce_audio_utils
            juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
