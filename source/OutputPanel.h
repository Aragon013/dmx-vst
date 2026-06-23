#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/**
    Panel de "Salida DMX": reproduce la secuencia (keyframes) y muestra en vivo el
    valor 0..255 de cada canal de cada equipo.

    Fuente de tiempo:
      - Si el host (DAW) esta reproduciendo, usa su posicion (ppq) en beats.
      - Si no hay host (Standalone) o esta parado, usa un reloj manual interno
        (Play/Stop/Reset) a un BPM ajustable, para poder probar sin DAW.

    Los valores resultantes se escriben en el buffer DMX del procesador, que mas
    adelante alimentara la salida fisica (Enttec USB Pro).
*/
class OutputPanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit OutputPanel (DmxVstAudioProcessor&);
    ~OutputPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    double computeBeats();

    DmxVstAudioProcessor& processorRef;

    juce::TextButton playButton  { "Play" };
    juce::TextButton stopButton  { "Stop" };
    juce::TextButton resetButton { "Reset" };

    juce::Label  bpmLabel;
    juce::Slider bpmSlider;
    juce::Label  posLabel;     // muestra beats/compas actuales y la fuente

    // --- Salida Art-Net (DMX por red) ---
    juce::ToggleButton artNetToggle    { "Art-Net" };
    juce::ToggleButton broadcastToggle { "Broadcast" };
    juce::Label        ipLabel;
    juce::TextEditor   ipEditor;
    juce::Label        artNetStatus;
    void refreshArtNetUi();

    // --- Salida sACN E1.31 (DMX por red) ---
    juce::ToggleButton sacnToggle      { "sACN" };
    juce::ToggleButton multicastToggle { "Multicast" };
    juce::Label        sacnIpLabel;
    juce::TextEditor   sacnIpEditor;
    juce::Label        sacnStatus;
    void refreshSacnUi();

    // --- Interfaz de red (NIC) de salida para Art-Net/sACN ---
    juce::Label        netIfaceLabel;
    juce::ComboBox     netIfaceCombo;
    juce::StringArray  netIfaceIps;       // IPs locales por indice (idx0 = Auto, vacia)
    void rescanNetInterfaces();

    // --- Salida Enttec USB Pro (DMX por hardware/serie) ---
    juce::ToggleButton enttecToggle  { "Enttec" };
    juce::ComboBox     portCombo;
    juce::TextButton   refreshButton  { "Refrescar" };
    juce::Label        uniLabel;
    juce::Slider       uniSlider;
    juce::Label        enttecStatus;
    void refreshEnttecUi();
    void rescanPorts();

    // Reloj manual interno
    bool   manualPlaying = false;
    double manualBeats   = 0.0;
    double lastTimeMs    = 0.0;

    double displayedBeats = 0.0;
    bool   usingHost      = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputPanel)
};
