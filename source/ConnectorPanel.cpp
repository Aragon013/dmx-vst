#include "ConnectorPanel.h"

ConnectorPanel::ConnectorPanel (DmxVstAudioProcessor& p)
    : processorRef (p)
{
    busLabel.setText ("Nombre de la pista (bus)", juce::dontSendNotification);
    busLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    busLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (busLabel);

    busEditor.setText (processorRef.getBusName(), juce::dontSendNotification);
    busEditor.setTextToShowWhenEmpty ("p.ej. Bajo, Bateria, Guitarra, Synth", juce::Colours::grey);
    busEditor.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    busEditor.onTextChange = [this] { processorRef.setBusName (busEditor.getText()); };
    busEditor.onReturnKey  = [this] { processorRef.setBusName (busEditor.getText()); };
    addAndMakeVisible (busEditor);

    auto& la = processorRef.getLiveAnalyzer();
    setupKnob (gainSlider,  gainLabel,  "Realce",       1.0,  8.0,  la.makeupGain,     0.1);
    setupKnob (ratioSlider, ratioLabel, "Sensib. golpe",1.05, 3.0,  la.transientRatio, 0.05);
    setupKnob (floorSlider, floorLabel, "Umbral",       0.0,  0.10, la.transientFloor, 0.005);

    startTimerHz (30);
}

ConnectorPanel::~ConnectorPanel()
{
    stopTimer();
    gainSlider.setLookAndFeel (nullptr);
    ratioSlider.setLookAndFeel (nullptr);
    floorSlider.setLookAndFeel (nullptr);
}

void ConnectorPanel::setupKnob (juce::Slider& s, juce::Label& lbl, const juce::String& name,
                                double min, double max, double value, double interval)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setRange (min, max, interval);
    s.setValue (value, juce::dontSendNotification);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    s.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffffb020));
    s.setLookAndFeel (&knobLnf);
    s.onValueChange = [this]
    {
        auto& la = processorRef.getLiveAnalyzer();
        la.makeupGain     = (float) gainSlider.getValue();
        la.transientRatio = (float) ratioSlider.getValue();
        la.transientFloor = (float) floorSlider.getValue();
    };
    addAndMakeVisible (s);

    lbl.setText (name, juce::dontSendNotification);
    lbl.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    lbl.setFont (juce::FontOptions (12.0f));
    lbl.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (lbl);
}

void ConnectorPanel::timerCallback()
{
    mLevel     = processorRef.getLiveLevel();
    mBass      = processorRef.getLiveBass();
    mTransient = processorRef.getLiveTransient();
    repaint();
}

void ConnectorPanel::paint (juce::Graphics& g)
{
    using P = LuxLookAndFeel::Palette;
    g.fillAll (juce::Colour (P::bg1));

    auto area = getLocalBounds().reduced (16);

    // Cabecera
    g.setColour (juce::Colour (P::textHi));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("MODO CONNECTOR", area.removeFromTop (28), juce::Justification::centredLeft, false);

    g.setColour (juce::Colour (P::textDim));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Analiza esta pista y la envia al Main. Pon esta instancia en la pista del stem.",
                area.removeFromTop (18), juce::Justification::centredLeft, false);

    area.removeFromTop (8 + 24 + 30 + 16);   // hueco de bus editor + label (colocados en resized)
    area.removeFromTop (130);                // hueco de knobs

    // Medidores en vivo
    auto meters = area.removeFromTop (120);
    struct M { const char* name; float value; juce::Colour col; };
    const M ms[] =
    {
        { "VOLUMEN",     mLevel,     juce::Colour (0xffffd060) },
        { "GRAVES",      mBass,      juce::Colour (0xffff5fae) },
        { "TRANSITORIO", mTransient, juce::Colour (0xff4fc3f7) },
    };
    const int gap = 12;
    const int w = (meters.getWidth() - 2 * gap) / 3;
    int x = meters.getX();
    for (const auto& m : ms)
    {
        juce::Rectangle<int> box (x, meters.getY(), w, meters.getHeight() - 20);
        const float frac = juce::jlimit (0.0f, 1.0f, m.value);

        g.setColour (juce::Colour (LuxLookAndFeel::Palette::surface));
        g.fillRoundedRectangle (box.toFloat(), 6.0f);
        g.setColour (juce::Colour (LuxLookAndFeel::Palette::line));
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 6.0f, 1.0f);

        auto inner = box.reduced (4);
        auto fill = inner.withTop (inner.getBottom() - (int) (frac * inner.getHeight()));
        if (frac > 0.001f)
        {
            g.setColour (m.col.withAlpha (0.18f + 0.20f * frac));
            g.fillRoundedRectangle (fill.toFloat().expanded (2.0f), 5.0f);
            juce::ColourGradient grad (m.col.withAlpha (0.95f), 0.0f, (float) fill.getY(),
                                       m.col.withAlpha (0.55f), 0.0f, (float) fill.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fill.toFloat(), 4.0f);
        }

        g.setColour (juce::Colour (LuxLookAndFeel::Palette::textHi));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String ((int) (frac * 100.0f)) + "%",
                    box.withHeight (18).translated (0, 4), juce::Justification::centred, false);

        g.setColour (juce::Colour (LuxLookAndFeel::Palette::textDim));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (m.name, x, box.getBottom() + 4, w, 14, juce::Justification::centred, false);

        x += w + gap;
    }
}

void ConnectorPanel::resized()
{
    auto area = getLocalBounds().reduced (16);

    area.removeFromTop (28 + 18 + 8);   // cabecera + subtitulo

    busLabel.setBounds (area.removeFromTop (16));
    busEditor.setBounds (area.removeFromTop (30).removeFromLeft (320));
    area.removeFromTop (16);

    // Knobs en fila
    auto knobs = area.removeFromTop (130);
    const int kw = 110;
    auto place = [&knobs, kw] (juce::Slider& s, juce::Label& lbl)
    {
        auto col = knobs.removeFromLeft (kw);
        lbl.setBounds (col.removeFromTop (16));
        s.setBounds (col);
        knobs.removeFromLeft (10);
    };
    place (gainSlider,  gainLabel);
    place (ratioSlider, ratioLabel);
    place (floorSlider, floorLabel);
}
