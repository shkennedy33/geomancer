cmake_minimum_required(VERSION 3.22)
project(Geomancer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(external/JUCE)

juce_add_plugin(Geomancer
    COMPANY_NAME "Sequoyah"
    PLUGIN_MANUFACTURER_CODE Sqyh
    PLUGIN_CODE Gmcr
    PRODUCT_NAME "Geomancer"
    DESCRIPTION "Drum sampler + step sequencer"
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Instrument Drum Sampler)

target_sources(Geomancer PRIVATE
    Source/PluginProcessor.cpp
    Source/Params.cpp
    Source/Pattern.cpp
    Source/Sample.cpp
    Source/DrumSynth.cpp
    Source/Voice.cpp
    Source/Track.cpp
    Source/Sequencer.cpp
    Source/Maths.cpp
    Source/PluginEditor.cpp
    Source/gui/LookAndFeel.cpp
    Source/gui/Header.cpp
    Source/gui/StepGrid.cpp
    Source/gui/LaneEditor.cpp
    Source/gui/SoundPanel.cpp
    Source/gui/MathsPanel.cpp
    Source/gui/Waveform.cpp
    Source/gui/Browser.cpp)

target_include_directories(Geomancer PRIVATE Source)

target_compile_definitions(Geomancer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0)

target_link_libraries(Geomancer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

target_compile_options(Geomancer PRIVATE $<$<CONFIG:Release>:-O3>)

# ---- test harness: engine checks in-process, GUI snapshots, and a smoke test of the built VST3 ----
get_target_property(GM_SOURCES Geomancer SOURCES)
list(FILTER GM_SOURCES INCLUDE REGEX "^Source/")
juce_add_console_app(gm_test PRODUCT_NAME "gm_test")
target_sources(gm_test PRIVATE tools/gm_test.cpp ${GM_SOURCES})
target_include_directories(gm_test PRIVATE Source)
target_compile_definitions(gm_test PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(gm_test PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
target_compile_options(gm_test PRIVATE -O2)
