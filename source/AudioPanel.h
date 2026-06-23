#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "WaveformView.h"
#include "LuxLookAndFeel.h"

/** Pestana de analisis de audio: cargar archivo + forma de onda con onsets. */
class AudioPanel : public juce::Component,
                   private juce::Timer
{
public:
    explicit AudioPanel (DmxVstAudioProcessor&);
    ~AudioPanel() override;

    void resized() override;
    void refresh();

private:
    void openFile();
    void paramsChanged();   // re-detecta onsets al mover los knobs
    void timerCallback() override;

    DmxVstAudioProcessor& processorRef;
    LuxLookAndFeel        knobLnf;

    juce::TextButton loadButton { "Cargar audio..." };
    WaveformView     waveform;
    std::unique_ptr<juce::FileChooser> chooser;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
    };

    Knob sensitivity, threshold, neighbourhood, minSpacing;
    Knob lowCut, highCut, smoothing, gain;

    void setupKnob (Knob&, const juce::String& name,
                    double min, double max, double interval, double value,
                    const juce::String& suffix);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPanel)
};
