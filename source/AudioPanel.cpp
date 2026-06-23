#include "AudioPanel.h"

AudioPanel::AudioPanel (DmxVstAudioProcessor& p)
    : processorRef (p)
{
    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { openFile(); };

    addAndMakeVisible (waveform);
    waveform.setResult (processorRef.getLastAnalysis());
    waveform.onSeek = [this] (double seconds) { processorRef.seekToSeconds (seconds); };

    const auto& params = processorRef.getOnsetParams();

    // Sensibilidad: delta invertido para que "mas" = mas triggers (delta 0.30..0.00).
    setupKnob (sensitivity,   "Sensibilidad",  0.0, 100.0, 0.5,   (1.0 - params.delta / 0.30) * 100.0, " %");
    setupKnob (threshold,     "Umbral",        0.0, 0.50,  0.005, params.threshold, "");
    setupKnob (neighbourhood, "Vecindario",    1.0, 30.0,  1.0,   (double) params.neighbourhood, " fr");
    setupKnob (minSpacing,    "Separacion",    0.0, 1.0,   0.01,  params.minSpacingSec, " s");
    setupKnob (lowCut,        "Corte grave",   0.0, 100.0, 1.0,   params.lowCut  * 100.0, " %");
    setupKnob (highCut,       "Corte agudo",   0.0, 100.0, 1.0,   params.highCut * 100.0, " %");
    setupKnob (smoothing,     "Suavizado",     0.0, 12.0,  1.0,   (double) params.smoothing, " fr");
    setupKnob (gain,          "Ganancia",      0.5, 4.0,   0.05,  params.gain, "x");

    startTimerHz (30);   // actualiza el cursor de reproduccion sobre la waveform
}

AudioPanel::~AudioPanel()
{
    stopTimer();

    // Quita el LookAndFeel de cada slider antes de destruirlo.
    for (auto* k : { &sensitivity, &threshold, &neighbourhood, &minSpacing,
                     &lowCut, &highCut, &smoothing, &gain })
        k->slider.setLookAndFeel (nullptr);
}

void AudioPanel::timerCallback()
{
    waveform.setPlayheadSeconds (processorRef.getPlaybackSeconds());
}

void AudioPanel::setupKnob (Knob& k, const juce::String& name,
                            double min, double max, double interval, double value,
                            const juce::String& suffix)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRange (min, max, interval);
    k.slider.setValue (value, juce::dontSendNotification);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    k.slider.setTextValueSuffix (suffix);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffffb020));
    k.slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    k.slider.setLookAndFeel (&knobLnf);
    k.slider.onValueChange = [this] { paramsChanged(); };
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (k.label);
}

void AudioPanel::paramsChanged()
{
    auto& params = processorRef.getOnsetParams();

    // Sensibilidad 0..100% -> delta 0.30..0.00
    params.delta         = (float) (0.30 * (1.0 - sensitivity.slider.getValue() / 100.0));
    params.threshold     = (float) threshold.slider.getValue();
    params.neighbourhood = (int)   neighbourhood.slider.getValue();
    params.minSpacingSec = minSpacing.slider.getValue();
    params.lowCut        = (float) (lowCut.slider.getValue()  / 100.0);
    params.highCut       = (float) (highCut.slider.getValue() / 100.0);
    params.smoothing     = (int)   smoothing.slider.getValue();
    params.gain          = (float) gain.slider.getValue();

    // El corte agudo nunca por debajo del grave.
    if (params.highCut <= params.lowCut)
    {
        params.highCut = juce::jmin (1.0f, params.lowCut + 0.05f);
        highCut.slider.setValue (params.highCut * 100.0, juce::dontSendNotification);
    }

    processorRef.recomputeOnsets();
    waveform.setResult (processorRef.getLastAnalysis());
}

void AudioPanel::refresh()
{
    waveform.setResult (processorRef.getLastAnalysis());
}

void AudioPanel::openFile()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Selecciona un archivo de audio",
        juce::File{},
        "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg");

    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile())
        {
            processorRef.analyzeFile (file);
            processorRef.loadAudioForPlayback (file);   // para reproducirlo con el transporte
            waveform.setResult (processorRef.getLastAnalysis());
        }
    });
}

void AudioPanel::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto buttonRow = area.removeFromTop (36);
    loadButton.setBounds (buttonRow.removeFromLeft (160));

    area.removeFromTop (8);

    // Dos filas de 4 knobs en la parte inferior.
    auto knobArea = area.removeFromBottom (200);

    Knob* row1[] = { &sensitivity, &threshold, &neighbourhood, &minSpacing };
    Knob* row2[] = { &lowCut, &highCut, &smoothing, &gain };

    auto layoutRow = [] (juce::Rectangle<int> r, Knob** knobs, int n)
    {
        const int colW = r.getWidth() / n;
        for (int i = 0; i < n; ++i)
        {
            juce::Rectangle<int> col (r.getX() + i * colW, r.getY(), colW, r.getHeight());
            knobs[i]->label.setBounds (col.removeFromTop (16));
            knobs[i]->slider.setBounds (col.reduced (6));
        }
    };

    layoutRow (knobArea.removeFromTop (knobArea.getHeight() / 2), row1, 4);
    layoutRow (knobArea, row2, 4);

    area.removeFromBottom (8);
    waveform.setBounds (area);
}
