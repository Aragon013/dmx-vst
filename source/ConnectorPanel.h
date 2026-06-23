#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "LuxLookAndFeel.h"

/**
    Panel del modo CONNECTOR: identifica la pista (nombre de bus), permite afinar
    la deteccion con perillas y muestra los medidores en vivo. Las senales se
    publican en el hub para que la instancia Main las use.
*/
class ConnectorPanel : public juce::Component,
                       private juce::Timer
{
public:
    explicit ConnectorPanel (DmxVstAudioProcessor&);
    ~ConnectorPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupKnob (juce::Slider&, juce::Label&, const juce::String& name,
                    double min, double max, double value, double interval);

    DmxVstAudioProcessor& processorRef;
    LuxLookAndFeel knobLnf;

    juce::Label      busLabel;
    juce::TextEditor busEditor;

    juce::Slider gainSlider, ratioSlider, floorSlider;
    juce::Label  gainLabel,  ratioLabel,  floorLabel;

    float mLevel = 0.0f, mBass = 0.0f, mTransient = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConnectorPanel)
};
