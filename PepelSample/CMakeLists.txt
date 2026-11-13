cmake_minimum_required(VERSION 3.22)
project(PepelSample VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Fonts (OFL) are embedded via BinaryData: VT323Regular_ttf, PressStart2PRegular_ttf
juce_add_binary_data(PepelAssets SOURCES
    Assets/VT323-Regular.ttf
    Assets/PressStart2P-Regular.ttf)
set_target_properties(PepelAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

juce_add_plugin(PepelSample
    COMPANY_NAME "Pepel"
    PLUGIN_MANUFACTURER_CODE Pepl
    PLUGIN_CODE Psmp
    FORMATS VST3
    PRODUCT_NAME "PEPEL SAMPLE"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler)

target_sources(PepelSample PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(PepelSample PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1)

# Cyrillic string literals are written as UTF-8 and wrapped in juce::String::fromUTF8
target_compile_options(PepelSample PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(PepelSample
    PRIVATE
        PepelAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
