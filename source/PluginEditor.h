#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "LuxLookAndFeel.h"
#include "AudioPanel.h"
#include "FixturesPanel.h"
#include "TimelinePanel.h"
#include "OutputPanel.h"
#include "ReactivePanel.h"
#include "ConnectorPanel.h"

class DmxVstAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit DmxVstAudioProcessorEditor (DmxVstAudioProcessor&);
    ~DmxVstAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    DmxVstAudioProcessor& processorRef;
    LuxLookAndFeel lux;

    juce::Label    roleLabel;
    juce::ComboBox roleCombo;
    void updateRoleView();

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    AudioPanel    audioPanel    { processorRef };
    FixturesPanel fixturesPanel { processorRef };
    TimelinePanel timelinePanel { processorRef };
    OutputPanel   outputPanel   { processorRef };
    ReactivePanel reactivePanel { processorRef };
    ConnectorPanel connectorPanel { processorRef };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DmxVstAudioProcessorEditor)
};
