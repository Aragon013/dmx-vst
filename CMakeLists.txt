cmake_minimum_required(VERSION 3.22)

project(DmxVst VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Evita recompilar JUCE cada vez que limpias: se descarga una sola vez.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# AU solo existe en macOS; en Windows generamos VST3 + Standalone (para test sin DAW).
if(APPLE)
    set(PLUGIN_FORMATS AU VST3 Standalone)
else()
    set(PLUGIN_FORMATS VST3 Standalone)
endif()

juce_add_plugin(DmxVst
    COMPANY_NAME              "Aragon"
    PLUGIN_MANUFACTURER_CODE  Argn
    PLUGIN_CODE               Dmx1
    FORMATS                   ${PLUGIN_FORMATS}
    PRODUCT_NAME              "LuxSync DMX"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
)

target_sources(DmxVst PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/AudioAnalyzer.cpp
    source/WaveformView.cpp
    source/AudioPanel.cpp
    source/FixtureModel.cpp
    source/FixturesPanel.cpp
    source/ChannelRow.cpp
    source/FixtureEditor.cpp
    source/TimelinePanel.cpp
    source/OutputPanel.cpp
    source/ReactivePanel.cpp
    source/ConnectorPanel.cpp
    source/ArtNetSender.cpp
    source/SacnSender.cpp
    source/SerialPort.cpp
    source/EnttecSender.cpp
)

target_compile_definitions(DmxVst PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DmxVst
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# AI Automator Edition (app Standalone para DJs/eventos).
# Reutiliza piezas de source/ (LuxLookAndFeel, y mas en fases futuras).
#==============================================================================
juce_add_gui_app(LuxAutomator
    COMPANY_NAME  "Aragon"
    PRODUCT_NAME  "LuxSync AI Automator"
)

target_sources(LuxAutomator PRIVATE
    apps/automator/Main.cpp
    apps/automator/AutomatorComponent.cpp
    source/AudioAnalyzer.cpp
    source/ArtNetSender.cpp
    source/SacnSender.cpp
    source/SerialPort.cpp
    source/EnttecSender.cpp
    source/FixtureModel.cpp
)

target_compile_definitions(LuxAutomator PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LuxAutomator,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LuxAutomator,JUCE_VERSION>"
)

target_link_libraries(LuxAutomator PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

