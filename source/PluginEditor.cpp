#include "PluginEditor.h"

DmxVstAudioProcessorEditor::DmxVstAudioProcessorEditor (DmxVstAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lux);

    const juce::Colour tabBg { LuxLookAndFeel::Palette::bg1 };
    tabs.addTab ("Analisis", tabBg, &audioPanel,    false);
    tabs.addTab ("Equipos",  tabBg, &fixturesPanel, false);
    tabs.addTab ("Timeline", tabBg, &timelinePanel, false);
    tabs.addTab ("Salida DMX", tabBg, &outputPanel, false);
    tabs.addTab ("Reactivo", tabBg, &reactivePanel, false);
    tabs.setTabBarDepth (38);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);

    addChildComponent (connectorPanel);

    roleLabel.setText ("ROL", juce::dontSendNotification);
    roleLabel.setColour (juce::Label::textColourId, juce::Colour (LuxLookAndFeel::Palette::textDim));
    roleLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    roleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (roleLabel);

    roleCombo.addItem ("Main (controla DMX)", 1);
    roleCombo.addItem ("Connector (analiza pista)", 2);
    roleCombo.setSelectedId (processorRef.getRole() == DmxVstAudioProcessor::Role::Connector ? 2 : 1,
                             juce::dontSendNotification);
    roleCombo.onChange = [this]
    {
        processorRef.setRole (roleCombo.getSelectedId() == 2
                              ? DmxVstAudioProcessor::Role::Connector
                              : DmxVstAudioProcessor::Role::Main);
        updateRoleView();
    };
    addAndMakeVisible (roleCombo);

    fixturesPanel.refresh();
    updateRoleView();

    setResizable (true, true);
    setResizeLimits (820, 560, 2600, 1700);
    setSize (980, 700);
    startTimerHz (30);   // refresca la franja de transporte
}

DmxVstAudioProcessorEditor::~DmxVstAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DmxVstAudioProcessorEditor::timerCallback()
{
    repaint (getLocalBounds().removeFromTop (76));   // solo la franja superior
}

void DmxVstAudioProcessorEditor::updateRoleView()
{
    const bool connector = (processorRef.getRole() == DmxVstAudioProcessor::Role::Connector);
    tabs.setVisible (! connector);
    connectorPanel.setVisible (connector);
    resized();
}

void DmxVstAudioProcessorEditor::paint (juce::Graphics& g)
{
    using P = LuxLookAndFeel::Palette;
    const juce::Colour amber { P::accent };

    // Fondo con gradiente vertical sutil (estudio a oscuras)
    juce::ColourGradient bgGrad (juce::Colour (P::bg1), 0.0f, 0.0f,
                                 juce::Colour (P::bg0), 0.0f, (float) getHeight(), false);
    g.setGradientFill (bgGrad);
    g.fillAll();

    auto area = getLocalBounds();

    // ---- Barra superior (header) ----
    auto header = area.removeFromTop (54);
    g.setColour (juce::Colour (P::bg0).withAlpha (0.6f));
    g.fillRect (header);

    auto headerInner = header.reduced (18, 0);

    // Marca: punto luminoso + wordmark
    auto brandDot = headerInner.removeFromLeft (16).withSizeKeepingCentre (10, 10).toFloat();
    g.setColour (amber.withAlpha (0.30f));
    g.fillEllipse (brandDot.expanded (3.0f));
    g.setColour (amber);
    g.fillEllipse (brandDot);

    headerInner.removeFromLeft (10);
    auto titleArea = headerInner.removeFromLeft (260);
    g.setColour (juce::Colour (P::textHi));
    g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    g.drawText ("LuxSync", titleArea.removeFromLeft (92), juce::Justification::centredLeft, false);
    g.setColour (amber);
    g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    g.drawText ("DMX", titleArea, juce::Justification::centredLeft, false);

    // ---- Info de transporte (a la derecha del header) ----
    const auto& t = processorRef.getTransportState();
    const bool  connected = t.hasPlayHead.load();
    const bool  playing   = t.isPlaying.load();

    auto transport = headerInner.removeFromRight (360);
    auto dot = transport.removeFromLeft (22).withSizeKeepingCentre (10, 10).toFloat();
    const auto dotCol = connected ? (playing ? juce::Colour (0xff39d98a) : amber)
                                  : juce::Colour (P::textDim);
    g.setColour (dotCol.withAlpha (0.30f));
    g.fillEllipse (dot.expanded (3.0f));
    g.setColour (dotCol);
    g.fillEllipse (dot);

    const double bpm  = t.bpm.load();
    const double secs = t.timeSeconds.load();
    const int    num  = t.timeSigNum.load();
    const int    den  = t.timeSigDen.load();

    juce::String info;
    if (connected)
    {
        info << (playing ? "PLAYING" : "STOPPED")
             << "   \xc2\xb7   " << (bpm > 0.0 ? juce::String (bpm, 1) + " BPM" : "-- BPM")
             << "   \xc2\xb7   " << ((num > 0 && den > 0) ? juce::String (num) + "/" + juce::String (den) : "--")
             << "   \xc2\xb7   " << juce::String (secs, 2) + " s";
    }
    else
    {
        info = "Sin host (Standalone / DAW parado)";
    }

    g.setColour (juce::Colour (P::textMid));
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (info, transport, juce::Justification::centredLeft, false);

    // Separador fino bajo el header
    g.setColour (juce::Colour (P::line));
    g.fillRect (0, header.getBottom() - 1, getWidth(), 1);
}

void DmxVstAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (54);   // header

    // Selector de rol (franja propia)
    auto roleStrip = area.removeFromTop (36).reduced (18, 5);
    roleLabel.setBounds (roleStrip.removeFromLeft (34));
    roleStrip.removeFromLeft (4);
    roleCombo.setBounds (roleStrip.removeFromLeft (240).withSizeKeepingCentre (240, 26));

    area.removeFromTop (4);
    auto content = area.reduced (10, 0);
    content.removeFromBottom (10);
    tabs.setBounds (content);
    connectorPanel.setBounds (content);
}
