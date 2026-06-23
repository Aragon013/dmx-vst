#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <memory>
#include <vector>

/**
    Panel "Reactivo": automatizacion inteligente a partir del audio en vivo.

    Arriba: medidores en tiempo real de las senales del analisis (Volumen, Graves,
    Transitorio). Debajo: editor de reglas que conectan esas senales con las luces
    (p.ej. Volumen -> Dimmer, Transitorio -> cambio de color de un par RGB).
*/
class ReactivePanel : public juce::Component,
                      private juce::Timer
{
public:
    explicit ReactivePanel (DmxVstAudioProcessor&);
    ~ReactivePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class RuleRow;   // fila editable de una regla

    void timerCallback() override;
    void rebuildRows();
    void addRule();

    DmxVstAudioProcessor& processorRef;

    juce::Label      title;
    juce::TextButton addButton { "+ Regla" };
    juce::Viewport   viewport;
    std::unique_ptr<juce::Component>     rowsHolder;
    std::vector<std::unique_ptr<RuleRow>> rows;

    int  lastRuleCount    = -1;
    int  lastFixtureCount = -1;

    // medidores en vivo (valores cacheados para pintar)
    float mLevel = 0.0f, mBass = 0.0f, mTransient = 0.0f;

    static constexpr int kMetersHeight = 92;
    static constexpr int kRowHeight    = 34;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReactivePanel)
};
