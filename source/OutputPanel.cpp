#include "OutputPanel.h"

namespace
{
    // Color representativo segun el tipo de canal (para los medidores).
    juce::Colour colourForChannel (ChannelType t)
    {
        switch (t)
        {
            case ChannelType::Red:     return juce::Colour (0xffe23b3b);
            case ChannelType::Green:   return juce::Colour (0xff3bd45f);
            case ChannelType::Blue:    return juce::Colour (0xff4f8cff);
            case ChannelType::White:   return juce::Colour (0xffe8e8e8);
            case ChannelType::Amber:   return juce::Colour (0xffffb020);
            case ChannelType::UV:      return juce::Colour (0xff9b59ff);
            case ChannelType::Dimmer:  return juce::Colour (0xffffd060);
            case ChannelType::Strobe:  return juce::Colour (0xff00d0d0);
            case ChannelType::Pan:
            case ChannelType::PanFine:
            case ChannelType::Tilt:
            case ChannelType::TiltFine: return juce::Colour (0xff7aa0c0);
            default:                    return juce::Colour (0xff4fc3f7);
        }
    }

    juce::String extractComPortName (const juce::String& text)
    {
        const auto upper = text.toUpperCase();
        for (int pos = 0; pos + 3 <= upper.length(); ++pos)
        {
            if (upper[pos] != 'C' || upper[pos + 1] != 'O' || upper[pos + 2] != 'M')
                continue;

            int end = pos + 3;
            while (end < upper.length() && upper[end] >= '0' && upper[end] <= '9')
                ++end;

            if (end > pos + 3)
                return "COM" + upper.substring (pos + 3, end);
        }
        return text.trim();
    }

    int preferredPortScore (const juce::String& label)
    {
        const auto u = label.toUpperCase();
        if (u.contains ("ENTTEC"))     return 5;
        if (u.contains ("DMX"))        return 4;
        if (u.contains ("FTDI"))       return 3;
        if (u.contains ("USB SERIAL")) return 2;
        if (u.contains ("USB"))        return 1;
        return 0;
    }
}

OutputPanel::OutputPanel (DmxVstAudioProcessor& p)
    : processorRef (p)
{
    playButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a6a2a));
    playButton.onClick = [this] { processorRef.transportPlay(); };
    addAndMakeVisible (playButton);

    stopButton.onClick = [this] { processorRef.transportStop(); };
    addAndMakeVisible (stopButton);

    resetButton.onClick = [this] { processorRef.transportRewind(); };
    addAndMakeVisible (resetButton);

    bpmLabel.setText ("BPM manual", juce::dontSendNotification);
    bpmLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    bpmLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (bpmLabel);

    bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
    bpmSlider.setRange (40.0, 300.0, 1.0);
    bpmSlider.setValue (120.0, juce::dontSendNotification);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 22);
    bpmSlider.onValueChange = [this] { processorRef.setInternalBpm (bpmSlider.getValue()); };
    addAndMakeVisible (bpmSlider);

    posLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffb020));
    posLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    posLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (posLabel);

    // --- Salida Art-Net ---
    artNetToggle.setToggleState (processorRef.isArtNetEnabled(), juce::dontSendNotification);
    artNetToggle.onClick = [this]
    {
        processorRef.setArtNetEnabled (artNetToggle.getToggleState());
        refreshArtNetUi();
    };
    addAndMakeVisible (artNetToggle);

    broadcastToggle.setToggleState (processorRef.isArtNetBroadcast(), juce::dontSendNotification);
    broadcastToggle.onClick = [this]
    {
        processorRef.setArtNetBroadcast (broadcastToggle.getToggleState());
        refreshArtNetUi();
    };
    addAndMakeVisible (broadcastToggle);

    ipLabel.setText ("IP", juce::dontSendNotification);
    ipLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    ipLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (ipLabel);

    ipEditor.setText (processorRef.getArtNetTarget(), juce::dontSendNotification);
    ipEditor.setInputRestrictions (15, "0123456789.");
    ipEditor.onReturnKey = [this] { processorRef.setArtNetTarget (ipEditor.getText()); };
    ipEditor.onFocusLost = [this] { processorRef.setArtNetTarget (ipEditor.getText()); };
    addAndMakeVisible (ipEditor);

    artNetStatus.setColour (juce::Label::textColourId, juce::Colours::grey);
    artNetStatus.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (artNetStatus);
    refreshArtNetUi();

    // --- Salida sACN E1.31 ---
    sacnToggle.setToggleState (processorRef.isSacnEnabled(), juce::dontSendNotification);
    sacnToggle.onClick = [this]
    {
        processorRef.setSacnEnabled (sacnToggle.getToggleState());
        refreshSacnUi();
    };
    addAndMakeVisible (sacnToggle);

    multicastToggle.setToggleState (processorRef.isSacnMulticast(), juce::dontSendNotification);
    multicastToggle.onClick = [this]
    {
        processorRef.setSacnMulticast (multicastToggle.getToggleState());
        refreshSacnUi();
    };
    addAndMakeVisible (multicastToggle);

    sacnIpLabel.setText ("IP", juce::dontSendNotification);
    sacnIpLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    sacnIpLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (sacnIpLabel);

    sacnIpEditor.setText (processorRef.getSacnTarget(), juce::dontSendNotification);
    sacnIpEditor.setInputRestrictions (15, "0123456789.");
    sacnIpEditor.onReturnKey = [this] { processorRef.setSacnTarget (sacnIpEditor.getText()); };
    sacnIpEditor.onFocusLost = [this] { processorRef.setSacnTarget (sacnIpEditor.getText()); };
    addAndMakeVisible (sacnIpEditor);

    sacnStatus.setColour (juce::Label::textColourId, juce::Colours::grey);
    sacnStatus.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (sacnStatus);
    refreshSacnUi();

    // --- Interfaz de red (NIC) compartida por Art-Net y sACN ---
    netIfaceLabel.setText ("Red", juce::dontSendNotification);
    netIfaceLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    netIfaceLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (netIfaceLabel);

    netIfaceCombo.setTooltip ("Interfaz de red (tarjeta) por la que salen Art-Net y sACN. "
                              "Util si tienes varias redes (WiFi + Ethernet de luces).");
    netIfaceCombo.onChange = [this]
    {
        const int idx = netIfaceCombo.getSelectedItemIndex();
        const juce::String ip = (idx >= 0 && idx < netIfaceIps.size()) ? netIfaceIps[idx] : juce::String();
        processorRef.setNetInterface (ip);
    };
    addAndMakeVisible (netIfaceCombo);
    rescanNetInterfaces();

    // --- Salida Enttec USB Pro ---
    enttecToggle.setToggleState (processorRef.isEnttecEnabled(), juce::dontSendNotification);
    enttecToggle.onClick = [this]
    {
        processorRef.setEnttecEnabled (enttecToggle.getToggleState());
        refreshEnttecUi();
    };
    addAndMakeVisible (enttecToggle);

    portCombo.setTextWhenNothingSelected ("(puerto)");
    portCombo.onChange = [this]
    {
        const auto p = extractComPortName (portCombo.getText());
        if (p.isNotEmpty())
            processorRef.setEnttecPort (p);
        refreshEnttecUi();
    };
    addAndMakeVisible (portCombo);

    refreshButton.onClick = [this] { rescanPorts(); };
    addAndMakeVisible (refreshButton);

    uniLabel.setText ("Universo", juce::dontSendNotification);
    uniLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    uniLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (uniLabel);

    uniSlider.setSliderStyle (juce::Slider::IncDecButtons);
    uniSlider.setRange (0.0, 7.0, 1.0);
    uniSlider.setValue (processorRef.getEnttecUniverse(), juce::dontSendNotification);
    uniSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 22);
    uniSlider.onValueChange = [this] { processorRef.setEnttecUniverse ((int) uniSlider.getValue()); };
    addAndMakeVisible (uniSlider);

    enttecStatus.setColour (juce::Label::textColourId, juce::Colours::grey);
    enttecStatus.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (enttecStatus);

    rescanPorts();
    refreshEnttecUi();

    lastTimeMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (44);   // ~44 Hz, refresco tipico de DMX
}

OutputPanel::~OutputPanel()
{
    stopTimer();
}

double OutputPanel::computeBeats()
{
    usingHost = processorRef.hostIsActive();
    return processorRef.getPlayheadBeats();
}

void OutputPanel::timerCallback()
{
    displayedBeats = computeBeats();
    // El render y la salida DMX los hace ahora el processor (su propio timer),
    // para que sigan funcionando aunque esta ventana este cerrada. Aqui solo
    // leemos el buffer ya calculado para mostrarlo.

    const int bpb = 4;
    const int bar  = (int) (displayedBeats / bpb) + 1;
    const double beatInBar = displayedBeats - (bar - 1) * bpb;

    const bool playing = processorRef.isPlayingNow();
    juce::String s;
    s << (usingHost ? "HOST" : (playing ? "PLAY" : "PARADO"))
      << "   compas " << bar << " : " << juce::String (beatInBar + 1.0, 2);
    posLabel.setText (s, juce::dontSendNotification);

    // El estado de conexion del Enttec puede cambiar (conectar/desconectar).
    if (processorRef.isEnttecEnabled())
        refreshEnttecUi();

    repaint();
}

void OutputPanel::refreshArtNetUi()
{
    const bool on = processorRef.isArtNetEnabled();
    const bool bc = processorRef.isArtNetBroadcast();
    ipLabel.setEnabled (on && ! bc);
    ipEditor.setEnabled (on && ! bc);
    broadcastToggle.setEnabled (on);

    juce::String s;
    if (! on)        s = "desactivado";
    else if (bc)     s = "broadcast -> puerto " + juce::String (processorRef.getArtNetPort());
    else             s = processorRef.getArtNetTarget() + " : " + juce::String (processorRef.getArtNetPort());
    artNetStatus.setText (s, juce::dontSendNotification);
    artNetStatus.setColour (juce::Label::textColourId,
                            on ? juce::Colour (0xff3bd45f) : juce::Colours::grey);
}

void OutputPanel::refreshSacnUi()
{
    const bool on = processorRef.isSacnEnabled();
    const bool mc = processorRef.isSacnMulticast();
    sacnIpLabel.setEnabled (on && ! mc);
    sacnIpEditor.setEnabled (on && ! mc);
    multicastToggle.setEnabled (on);

    juce::String s;
    if (! on)        s = "desactivado";
    else if (mc)     s = "multicast 239.255.x.x : 5568  (prio " + juce::String (processorRef.getSacnPriority()) + ")";
    else             s = processorRef.getSacnTarget() + " : 5568  (prio " + juce::String (processorRef.getSacnPriority()) + ")";
    sacnStatus.setText (s, juce::dontSendNotification);
    sacnStatus.setColour (juce::Label::textColourId,
                          on ? juce::Colour (0xff3bd45f) : juce::Colours::grey);
}

void OutputPanel::rescanNetInterfaces()
{
    netIfaceCombo.clear (juce::dontSendNotification);
    netIfaceIps.clear();

    // Item 1 = Auto (deja que el sistema elija la ruta).
    netIfaceCombo.addItem ("Auto (sistema)", 1);
    netIfaceIps.add (juce::String());

    int id = 2;
    for (const auto& addr : juce::IPAddress::getAllAddresses (false))   // solo IPv4
    {
        if (addr == juce::IPAddress::any())
            continue;
        netIfaceCombo.addItem (addr.toString(), id++);
        netIfaceIps.add (addr.toString());
    }

    const auto saved = processorRef.getNetInterface();
    int sel = 1;   // Auto por defecto
    if (saved.isNotEmpty())
    {
        const int idx = netIfaceIps.indexOf (saved);
        if (idx >= 0)
            sel = idx + 1;
        else
        {
            // Guardada pero ahora ausente: la mostramos para no perderla.
            netIfaceCombo.addItem (saved + " (ausente)", id);
            netIfaceIps.add (saved);
            sel = id;
        }
    }
    netIfaceCombo.setSelectedId (sel, juce::dontSendNotification);
}

void OutputPanel::rescanPorts()
{
    const auto ports = processorRef.getSerialPorts();
    const auto current = extractComPortName (processorRef.getEnttecPort());

    portCombo.clear (juce::dontSendNotification);
    int idToSelect = 0;
    int autoId = 0;
    int bestScore = -1;
    for (int i = 0; i < ports.size(); ++i)
    {
        portCombo.addItem (ports[i], i + 1);
        if (extractComPortName (ports[i]) == current)
            idToSelect = i + 1;

        const int score = preferredPortScore (ports[i]);
        if (score > bestScore)
        {
            bestScore = score;
            autoId = i + 1;
        }
    }

    if (idToSelect > 0)
        portCombo.setSelectedId (idToSelect, juce::dontSendNotification);
    else if (current.isEmpty() && autoId > 0 && bestScore > 0)
    {
        portCombo.setSelectedId (autoId, juce::dontSendNotification);
        processorRef.setEnttecPort (extractComPortName (portCombo.getText()));
    }
    else if (current.isNotEmpty())
    {
        // El puerto guardado ya no esta presente; lo mostramos igualmente.
        portCombo.addItem (current + " (ausente)", ports.size() + 1);
        portCombo.setSelectedId (ports.size() + 1, juce::dontSendNotification);
    }
}

void OutputPanel::refreshEnttecUi()
{
    const bool on = processorRef.isEnttecEnabled();
    portCombo.setEnabled (on);
    uniLabel.setEnabled (on);
    uniSlider.setEnabled (on);

    juce::String s;
    juce::Colour c = juce::Colours::grey;
    if (! on)
        s = "desactivado";
    else if (processorRef.getEnttecPort().isEmpty())
        s = "elige un puerto";
    else if (processorRef.isEnttecConnected())
    {
        s = "conectado: " + processorRef.getEnttecPort()
          + "  (U" + juce::String (processorRef.getEnttecUniverse()) + ")";
        c = juce::Colour (0xff3bd45f);
    }
    else
    {
        s = "sin conexion (" + processorRef.getEnttecPort() + ")";
        c = juce::Colour (0xffe23b3b);
    }
    enttecStatus.setText (s, juce::dontSendNotification);
    enttecStatus.setColour (juce::Label::textColourId, c);
}

void OutputPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0f1115));

    const auto& fixtures = processorRef.getFixtures();

    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (130);  // cuatro franjas de controles (transporte + Art-Net + sACN + Enttec)

    if (fixtures.empty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Anade equipos y dibuja keyframes en \"Timeline\" para ver la salida DMX aqui.",
                    area, juce::Justification::centred, true);
        return;
    }

    const int meterW   = 26;
    const int meterGap = 6;
    const int meterH   = 120;
    const int blockGap = 16;
    const int headerH  = 20;

    int y = area.getY();

    for (int fi = 0; fi < (int) fixtures.size(); ++fi)
    {
        const auto& f = fixtures[(size_t) fi];
        const int numCh = (int) f.channels.size();

        // Cabecera del equipo
        g.setColour (juce::Colour (0xffffb020));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        juce::String header = f.name + "   (U" + juce::String (f.universe)
                            + "  DMX " + juce::String (f.startAddress) + ")";
        g.drawText (header, area.getX(), y, area.getWidth(), headerH,
                    juce::Justification::centredLeft, true);
        y += headerH + 2;

        // Medidores de canal
        int x = area.getX();
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto& chan = f.channels[(size_t) ch];
            const int address = f.startAddress + ch;
            const int value = processorRef.getDmxValue (f.universe, address);
            const float frac = juce::jlimit (0.0f, 1.0f, value / 255.0f);

            juce::Rectangle<int> meter (x, y, meterW, meterH);

            // Fondo
            g.setColour (juce::Colour (0xff1a1d23));
            g.fillRoundedRectangle (meter.toFloat(), 3.0f);

            // Relleno proporcional al valor
            const int fillH = (int) (frac * meter.getHeight());
            auto fill = meter.withTop (meter.getBottom() - fillH);
            g.setColour (colourForChannel (chan.type).withAlpha (0.35f + 0.65f * frac));
            g.fillRoundedRectangle (fill.toFloat(), 3.0f);

            g.setColour (juce::Colour (0xff333842));
            g.drawRoundedRectangle (meter.toFloat(), 3.0f, 1.0f);

            // Valor numerico
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String (value), meter.withHeight (16).translated (0, 2),
                        juce::Justification::centred, false);

            // Etiqueta (tipo) y direccion bajo el medidor
            g.setColour (juce::Colours::lightgrey);
            g.setFont (juce::FontOptions (9.0f));
            const auto name = chan.label.isNotEmpty() ? chan.label : channelTypeToString (chan.type);
            g.drawText (name, x - 4, y + meterH + 1, meterW + 8, 12,
                        juce::Justification::centred, false);
            g.setColour (juce::Colours::grey);
            g.drawText ("#" + juce::String (address), x - 4, y + meterH + 12, meterW + 8, 12,
                        juce::Justification::centred, false);

            x += meterW + meterGap;
            if (x + meterW > area.getRight())   // salto de linea si no cabe
            {
                x = area.getX();
                y += meterH + 28;
            }
        }

        y += meterH + 28 + blockGap;
        if (y > area.getBottom())
            break;
    }
}

void OutputPanel::resized()
{
    auto top = getLocalBounds().reduced (10);
    auto controls = top.removeFromTop (40);

    playButton.setBounds  (controls.removeFromLeft (70).withSizeKeepingCentre (70, 26));
    controls.removeFromLeft (6);
    stopButton.setBounds  (controls.removeFromLeft (60).withSizeKeepingCentre (60, 26));
    controls.removeFromLeft (6);
    resetButton.setBounds (controls.removeFromLeft (60).withSizeKeepingCentre (60, 26));
    controls.removeFromLeft (16);

    bpmLabel.setBounds (controls.removeFromLeft (72).withSizeKeepingCentre (72, 22));
    bpmSlider.setBounds (controls.removeFromLeft (96).withSizeKeepingCentre (96, 26));

    posLabel.setBounds (controls.removeFromRight (240).withSizeKeepingCentre (240, 24));

    // Segunda fila: salida Art-Net.
    auto row2 = top.removeFromTop (28);
    artNetToggle.setBounds    (row2.removeFromLeft (90).withSizeKeepingCentre (90, 24));
    row2.removeFromLeft (8);
    broadcastToggle.setBounds (row2.removeFromLeft (104).withSizeKeepingCentre (104, 24));
    row2.removeFromLeft (12);
    ipLabel.setBounds   (row2.removeFromLeft (24).withSizeKeepingCentre (24, 24));
    ipEditor.setBounds  (row2.removeFromLeft (140).withSizeKeepingCentre (140, 24));
    row2.removeFromLeft (12);
    artNetStatus.setBounds (row2.removeFromLeft (260).withSizeKeepingCentre (260, 24));

    // Tercera fila: salida sACN.
    auto row3 = top.removeFromTop (28);
    sacnToggle.setBounds      (row3.removeFromLeft (90).withSizeKeepingCentre (90, 24));
    row3.removeFromLeft (8);
    multicastToggle.setBounds (row3.removeFromLeft (104).withSizeKeepingCentre (104, 24));
    row3.removeFromLeft (12);
    sacnIpLabel.setBounds  (row3.removeFromLeft (24).withSizeKeepingCentre (24, 24));
    sacnIpEditor.setBounds (row3.removeFromLeft (140).withSizeKeepingCentre (140, 24));
    row3.removeFromLeft (12);
    sacnStatus.setBounds (row3.removeFromLeft (320).withSizeKeepingCentre (320, 24));

    // Fila: interfaz de red (NIC) compartida por Art-Net/sACN.
    auto rowIf = top.removeFromTop (28);
    netIfaceLabel.setBounds (rowIf.removeFromLeft (40).withSizeKeepingCentre (40, 24));
    rowIf.removeFromLeft (8);
    netIfaceCombo.setBounds (rowIf.removeFromLeft (220).withSizeKeepingCentre (220, 24));

    // Cuarta fila: salida Enttec USB Pro.
    auto row4 = top.removeFromTop (28);
    enttecToggle.setBounds (row4.removeFromLeft (90).withSizeKeepingCentre (90, 24));
    row4.removeFromLeft (8);
    portCombo.setBounds (row4.removeFromLeft (160).withSizeKeepingCentre (160, 24));
    row4.removeFromLeft (8);
    refreshButton.setBounds (row4.removeFromLeft (80).withSizeKeepingCentre (80, 24));
    row4.removeFromLeft (12);
    uniLabel.setBounds (row4.removeFromLeft (56).withSizeKeepingCentre (56, 24));
    uniSlider.setBounds (row4.removeFromLeft (84).withSizeKeepingCentre (84, 24));
    row4.removeFromLeft (12);
    enttecStatus.setBounds (row4.removeFromLeft (300).withSizeKeepingCentre (300, 24));
}
