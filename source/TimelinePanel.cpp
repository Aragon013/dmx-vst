#include "TimelinePanel.h"
#include "LuxLookAndFeel.h"

namespace
{
    constexpr float kKeyHitRadius = 8.0f;
}

TimelinePanel::TimelinePanel (DmxVstAudioProcessor& p)
    : processorRef (p)
{
    fixtureLabel.setText ("Equipo", juce::dontSendNotification);
    fixtureLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    fixtureLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (fixtureLabel);

    fixtureCombo.setTextWhenNothingSelected ("(sin equipos)");
    fixtureCombo.onChange = [this] { repaint(); };
    addAndMakeVisible (fixtureCombo);

    auto setupSlider = [this] (juce::Label& lbl, juce::Slider& s, const juce::String& text,
                               double min, double max, double value, double interval)
    {
        lbl.setText (text, juce::dontSendNotification);
        lbl.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        lbl.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (lbl);

        s.setSliderStyle (juce::Slider::IncDecButtons);
        s.setRange (min, max, interval);
        s.setValue (value, juce::dontSendNotification);
        s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 52, 22);
        s.onValueChange = [this] { clampScroll(); updateScrollBar(); syncInternalTransport(); repaint(); };
        addAndMakeVisible (s);
    };

    setupSlider (bpmLabel,   bpmSlider,   "BPM",      40.0, 300.0, 120.0, 1.0);
    setupSlider (barsLabel,  barsSlider,  "Compases", 1.0,  64.0,  4.0,   1.0);
    setupSlider (beatsLabel, beatsSlider, "x compas", 1.0,  12.0,  4.0,   1.0);
    snapButton.setToggleState (true, juce::dontSendNotification);
    snapButton.setColour (juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (snapButton);

    clipModeButton.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffffb020));
    clipModeButton.setTooltip ("Modo efectos: arrastra en una pista para crear un clip LFO; "
                               "arrastra los bordes para redimensionar, doble clic para editar, "
                               "clic derecho para borrar.");
    clipModeButton.onClick = [this] { repaint(); };
    addAndMakeVisible (clipModeButton);

    // --- Transporte interno (para Standalone / previsualizar sin DAW) ---
    playButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a6a2a));
    playButton.setWantsKeyboardFocus (false);
    playButton.onClick = [this] { processorRef.transportPlay();  grabKeyboardFocus(); };
    addAndMakeVisible (playButton);

    stopButton.setWantsKeyboardFocus (false);
    stopButton.onClick = [this] { processorRef.transportStop();  grabKeyboardFocus(); };
    addAndMakeVisible (stopButton);

    rewindButton.setWantsKeyboardFocus (false);
    rewindButton.onClick = [this] { processorRef.transportRewind(); repaint(); grabKeyboardFocus(); };
    addAndMakeVisible (rewindButton);

    loopButton.setWantsKeyboardFocus (false);
    loopButton.setColour (juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    loopButton.setToggleState (processorRef.getInternalLoop(), juce::dontSendNotification);
    loopButton.onClick = [this] { processorRef.setInternalLoop (loopButton.getToggleState()); grabKeyboardFocus(); };
    addAndMakeVisible (loopButton);

    // --- Coreografia por IA: capturar la pista del DAW y generar el show ---
    captureButton.setWantsKeyboardFocus (false);
    captureButton.setTooltip ("Captura el audio de la pista mientras la reproduces UNA vez "
                              "(hasta 10 min). Pulsa de nuevo para detener; luego usa 'Generar IA'.");
    captureButton.onClick = [this]
    {
        if (processorRef.isCapturing())
            processorRef.stopCapture();
        else
            processorRef.startCapture();
        updateAiUi();
        grabKeyboardFocus();
    };
    addAndMakeVisible (captureButton);

    generateButton.setWantsKeyboardFocus (false);
    generateButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff6a4a8a));
    generateButton.setTooltip ("Analiza el audio capturado y genera automaticamente los "
                               "keyframes del show. Despues puedes editarlos a mano.");
    generateButton.onClick = [this]
    {
        if (processorRef.getFixtures().empty())
        {
            aiStatusLabel.setText ("No hay equipos: anadelos en la pestana \"Equipos\" primero.",
                                   juce::dontSendNotification);
            repaint();
            grabKeyboardFocus();
            return;
        }

        const bool ok = processorRef.generateAiShow();
        aiStatusLabel.setText (ok ? "Show generado: editalo abajo a mano."
                                  : "Captura una pista primero (reproduce y pulsa Capturar).",
                               juce::dontSendNotification);
        refreshFixtures();
        syncInternalTransport();
        updateAiUi();
        repaint();
        grabKeyboardFocus();
    };
    addAndMakeVisible (generateButton);

    aiStatusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaab2c2));
    aiStatusLabel.setFont (juce::FontOptions (12.0f));
    aiStatusLabel.setText ("IA: captura la pista del DAW y genera un lightshow editable.",
                           juce::dontSendNotification);
    addAndMakeVisible (aiStatusLabel);

    setWantsKeyboardFocus (true);

    hScroll.setAutoHide (false);
    hScroll.addListener (this);
    addAndMakeVisible (hScroll);

    refreshFixtures();
    syncInternalTransport();
    updateAiUi();
    startTimerHz (30);
}

TimelinePanel::~TimelinePanel()
{
    stopTimer();
}

void TimelinePanel::timerCallback()
{
    const int count = (int) processorRef.getFixtures().size();
    if (count != lastFixtureCount)
        refreshFixtures();

    if (processorRef.isCapturing())
        updateAiUi();   // refresca el tiempo capturado

    if (processorRef.isCapturing())
        repaint (getAiBarBounds());   // avanza la barra de progreso

    // Refrescamos para mover el playhead si algo esta reproduciendo (host o interno).
    if (processorRef.isPlayingNow())
        repaint (getTracksBounds().expanded (0, kRulerHeight));
}

void TimelinePanel::updateAiUi()
{
    const bool capturing = processorRef.isCapturing();
    const bool hasCap    = processorRef.hasCapture();

    captureButton.setButtonText (capturing ? "Detener captura" : "Capturar pista");
    captureButton.setColour (juce::TextButton::buttonColourId,
                             capturing ? juce::Colour (0xffb03030) : juce::Colour (0xff2a2f3a));
    generateButton.setEnabled (hasCap && ! capturing);

    if (capturing)
    {
        aiStatusLabel.setText ("Capturando... " + juce::String (processorRef.getCaptureSeconds(), 1)
                                 + " s  (reproduce la pista entera, luego Detener)",
                               juce::dontSendNotification);
    }
    else if (hasCap)
    {
        aiStatusLabel.setText (juce::String (processorRef.getCaptureSeconds(), 1)
                                 + " s capturados. Pulsa 'Generar IA'.",
                               juce::dontSendNotification);
    }
}

void TimelinePanel::syncInternalTransport()
{
    processorRef.setInternalBpm (currentBpm());
    processorRef.setInternalLength (totalBeats());
}

bool TimelinePanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        processorRef.transportToggle();
        repaint();
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        processorRef.transportStop();
        processorRef.transportRewind();
        repaint();
        return true;
    }

    const auto mods = key.getModifiers();
    if (mods.isCommandDown())
    {
        const int code = key.getKeyCode();

        if (code == 'Z')
        {
            if (mods.isShiftDown()) doRedo(); else doUndo();
            return true;
        }
        if (code == 'Y') { doRedo(); return true; }
        if (code == 'C') { copyChannel (targetChannel()); return true; }
        if (code == 'V')
        {
            double at = processorRef.getPlayheadBeats();
            if (snapButton.getToggleState())
                at = std::round (at);
            pasteToChannel (targetChannel(), at);
            return true;
        }
    }

    return false;
}

//==============================================================================
int TimelinePanel::targetChannel() const
{
    const int fi = currentFixtureIndex();
    if (fi < 0)
        return -1;

    const int numCh = (int) processorRef.getFixtures()[(size_t) fi].channels.size();
    if (numCh == 0)
        return -1;

    if (hoverRow >= 0 && hoverRow < numCh) return hoverRow;
    if (dragChannel >= 0 && dragChannel < numCh) return dragChannel;
    return 0;
}

void TimelinePanel::pushUndoSnapshot()
{
    undoStack.push_back (processorRef.getFixtures());
    if ((int) undoStack.size() > kMaxUndo)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void TimelinePanel::doUndo()
{
    if (undoStack.empty())
        return;

    redoStack.push_back (processorRef.getFixtures());
    processorRef.getFixtures() = undoStack.back();
    undoStack.pop_back();

    dragging = false; draggingClip = false;
    processorRef.markFixturesDirty();
    refreshFixtures();
    repaint();
}

void TimelinePanel::doRedo()
{
    if (redoStack.empty())
        return;

    undoStack.push_back (processorRef.getFixtures());
    processorRef.getFixtures() = redoStack.back();
    redoStack.pop_back();

    dragging = false; draggingClip = false;
    processorRef.markFixturesDirty();
    refreshFixtures();
    repaint();
}

void TimelinePanel::copyChannel (int ch)
{
    const int fi = currentFixtureIndex();
    if (fi < 0)
        return;

    auto& f = processorRef.getFixtures()[(size_t) fi];
    if (ch < 0 || ch >= (int) f.channels.size())
        return;

    clipboard = f.channels[(size_t) ch].keyframes;
    clipboardBase = 1.0e9;
    for (const auto& k : clipboard)
        clipboardBase = juce::jmin (clipboardBase, k.timeBeats);
    if (clipboard.empty())
        clipboardBase = 0.0;
}

void TimelinePanel::pasteToChannel (int ch, double atBeat)
{
    if (clipboard.empty())
        return;

    const int fi = currentFixtureIndex();
    if (fi < 0)
        return;

    auto& f = processorRef.getFixtures()[(size_t) fi];
    if (ch < 0 || ch >= (int) f.channels.size())
        return;

    pushUndoSnapshot();

    const double delta = atBeat - clipboardBase;
    auto& kfs = f.channels[(size_t) ch].keyframes;
    for (auto k : clipboard)
    {
        k.timeBeats = juce::jlimit (0.0, totalBeats(), k.timeBeats + delta);
        kfs.push_back (k);
    }
    sortKeyframes (kfs);

    processorRef.markFixturesDirty();
    repaint();
}

void TimelinePanel::refreshFixtures()
{
    const auto& fixtures = processorRef.getFixtures();
    lastFixtureCount = (int) fixtures.size();

    const int previous = fixtureCombo.getSelectedId();
    fixtureCombo.clear (juce::dontSendNotification);

    for (int i = 0; i < (int) fixtures.size(); ++i)
        fixtureCombo.addItem (fixtures[(size_t) i].name, i + 1);

    if (previous > 0 && previous <= (int) fixtures.size())
        fixtureCombo.setSelectedId (previous, juce::dontSendNotification);
    else if (! fixtures.empty())
        fixtureCombo.setSelectedId (1, juce::dontSendNotification);

    repaint();
}

//==============================================================================
int TimelinePanel::currentFixtureIndex() const
{
    const int id = fixtureCombo.getSelectedId();
    if (id <= 0 || id > (int) processorRef.getFixtures().size())
        return -1;
    return id - 1;
}

double TimelinePanel::currentBpm() const
{
    return bpmSlider.getValue();
}

int TimelinePanel::beatsPerBar() const
{
    return juce::jmax (1, (int) beatsSlider.getValue());
}

double TimelinePanel::totalBeats() const
{
    return juce::jmax (1.0, barsSlider.getValue() * beatsPerBar());
}

//==============================================================================
juce::Rectangle<int> TimelinePanel::getRulerBounds() const
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (44);   // franja de controles
    area.removeFromTop (kAiBarHeight);   // franja de IA
    auto r = area.removeFromTop (kRulerHeight);
    return r.withTrimmedLeft (kLabelWidth);
}

juce::Rectangle<int> TimelinePanel::getTracksBounds() const
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (44);
    area.removeFromTop (kAiBarHeight);
    area.removeFromTop (kRulerHeight);
    area.removeFromBottom (kScrollBarHeight + 2);   // hueco para la barra de scroll
    return area.withTrimmedLeft (kLabelWidth);
}

juce::Rectangle<int> TimelinePanel::getAiBarBounds() const
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (44);
    return area.removeFromTop (kAiBarHeight);
}

juce::Rectangle<int> TimelinePanel::getRowBounds (int channelIndex, int numChannels) const
{
    auto tracks = getTracksBounds();
    if (numChannels <= 0)
        return tracks;

    const float rowH = (float) tracks.getHeight() / (float) numChannels;
    return juce::Rectangle<int> (tracks.getX(),
                                 tracks.getY() + (int) (channelIndex * rowH),
                                 tracks.getWidth(),
                                 (int) rowH);
}

juce::Rectangle<int> TimelinePanel::getSwatchBounds (int channelIndex, int numChannels) const
{
    const auto row = getRowBounds (channelIndex, numChannels);
    const int sz = juce::jlimit (8, 16, row.getHeight() - 8);
    return juce::Rectangle<int> (12, row.getCentreY() - sz / 2, sz, sz);
}

void TimelinePanel::openColourPicker (int channelIndex)
{
    const int fi = currentFixtureIndex();
    if (fi < 0)
        return;

    auto& f = processorRef.getFixtures()[(size_t) fi];
    if (channelIndex < 0 || channelIndex >= (int) f.channels.size())
        return;

    colourEditChannel = channelIndex;

    auto selector = std::make_unique<juce::ColourSelector> (
        juce::ColourSelector::showColourAtTop
        | juce::ColourSelector::showColourspace
        | juce::ColourSelector::editableColour);
    selector->setName ("Color del canal");
    selector->setCurrentColour (f.channels[(size_t) channelIndex].colour, juce::dontSendNotification);
    selector->setSize (240, 280);
    selector->addChangeListener (this);

    const int numCh = (int) f.channels.size();
    const auto screenArea = localAreaToGlobal (getSwatchBounds (channelIndex, numCh));
    juce::CallOutBox::launchAsynchronously (std::move (selector), screenArea, nullptr);
}

void TimelinePanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    auto* cs = dynamic_cast<juce::ColourSelector*> (source);
    if (cs == nullptr)
        return;

    const int fi = currentFixtureIndex();
    if (fi < 0 || colourEditChannel < 0)
        return;

    auto& f = processorRef.getFixtures()[(size_t) fi];
    if (colourEditChannel >= (int) f.channels.size())
        return;

    f.channels[(size_t) colourEditChannel].colour = cs->getCurrentColour();
    processorRef.markFixturesDirty();
    repaint();
}

float TimelinePanel::beatToX (double beats) const
{
    auto tracks = getTracksBounds();
    return (float) tracks.getX() + (float) ((beats - scrollBeats) * pixelsPerBeat());
}

double TimelinePanel::xToBeat (float x) const
{
    auto tracks = getTracksBounds();
    const double b = scrollBeats + (x - tracks.getX()) / juce::jmax (1.0, pixelsPerBeat());
    return juce::jlimit (0.0, totalBeats(), b);
}

double TimelinePanel::pixelsPerBeat() const
{
    auto tracks = getTracksBounds();
    return (tracks.getWidth() / juce::jmax (1.0, totalBeats())) * zoom;
}

double TimelinePanel::visibleBeats() const
{
    return totalBeats() / juce::jmax (1.0, zoom);
}

void TimelinePanel::clampScroll()
{
    const double maxScroll = juce::jmax (0.0, totalBeats() - visibleBeats());
    scrollBeats = juce::jlimit (0.0, maxScroll, scrollBeats);
}

void TimelinePanel::updateScrollBar()
{
    hScroll.setRangeLimits (0.0, totalBeats(), juce::dontSendNotification);
    hScroll.setCurrentRange (scrollBeats, visibleBeats(), juce::dontSendNotification);
}

void TimelinePanel::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    scrollBeats = newRangeStart;
    repaint();
}

void TimelinePanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        // Zoom centrado en el cursor
        const double beatUnderCursor = xToBeat (e.position.x);
        const double factor = (w.deltaY > 0.0f) ? 1.18 : (1.0 / 1.18);
        zoom = juce::jlimit (1.0, 80.0, zoom * factor);

        const double ppb = pixelsPerBeat();
        if (ppb > 0.0)
        {
            const auto tracks = getTracksBounds();
            scrollBeats = beatUnderCursor - (e.position.x - tracks.getX()) / ppb;
        }
    }
    else
    {
        // Desplazamiento horizontal
        scrollBeats -= (double) w.deltaY * visibleBeats() * 0.25;
    }

    clampScroll();
    updateScrollBar();
    repaint();
}

float TimelinePanel::valueToY (float value, juce::Rectangle<int> row) const
{
    auto r = row.reduced (0, 6);
    const float frac = juce::jlimit (0.0f, 1.0f, value / 255.0f);
    return (float) r.getBottom() - frac * (float) r.getHeight();
}

float TimelinePanel::yToValue (float y, juce::Rectangle<int> row) const
{
    auto r = row.reduced (0, 6);
    const float frac = ((float) r.getBottom() - y) / (float) juce::jmax (1, r.getHeight());
    return juce::jlimit (0.0f, 255.0f, frac * 255.0f);
}

bool TimelinePanel::hitTestKeyframe (juce::Point<int> p, int& channelOut, int& kfOut) const
{
    const int fi = currentFixtureIndex();
    if (fi < 0)
        return false;

    const auto& f = processorRef.getFixtures()[(size_t) fi];
    const int numCh = (int) f.channels.size();

    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto row = getRowBounds (ch, numCh);
        const auto& kfs = f.channels[(size_t) ch].keyframes;

        for (int i = 0; i < (int) kfs.size(); ++i)
        {
            const float kx = beatToX (kfs[(size_t) i].timeBeats);
            const float ky = valueToY (kfs[(size_t) i].value, row);

            if (p.toFloat().getDistanceFrom ({ kx, ky }) <= kKeyHitRadius)
            {
                channelOut = ch;
                kfOut = i;
                return true;
            }
        }
    }

    return false;
}

//==============================================================================
juce::Rectangle<float> TimelinePanel::getClipBounds (int channelIndex, int clipIdx, int numChannels) const
{
    const int fi = currentFixtureIndex();
    if (fi < 0)
        return {};

    const auto& f = processorRef.getFixtures()[(size_t) fi];
    if (channelIndex < 0 || channelIndex >= (int) f.channels.size())
        return {};

    const auto& clips = f.channels[(size_t) channelIndex].clips;
    if (clipIdx < 0 || clipIdx >= (int) clips.size())
        return {};

    const auto& cl = clips[(size_t) clipIdx];
    const auto row = getRowBounds (channelIndex, numChannels).reduced (0, 4);
    const float x0 = beatToX (cl.startBeats);
    const float x1 = beatToX (cl.startBeats + cl.lengthBeats);
    return { x0, (float) row.getY(), juce::jmax (2.0f, x1 - x0), (float) row.getHeight() };
}

bool TimelinePanel::hitTestClip (juce::Point<int> p, int& channelOut, int& clipOut, int& edgeOut) const
{
    const int fi = currentFixtureIndex();
    if (fi < 0)
        return false;

    const auto& f = processorRef.getFixtures()[(size_t) fi];
    const int numCh = (int) f.channels.size();

    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto& clips = f.channels[(size_t) ch].clips;
        for (int i = (int) clips.size() - 1; i >= 0; --i)   // el de encima primero
        {
            const auto b = getClipBounds (ch, i, numCh);
            if (b.contains (p.toFloat()))
            {
                edgeOut = 0;
                if      (p.x - b.getX()    <= 6.0f) edgeOut = 1;
                else if (b.getRight() - p.x <= 6.0f) edgeOut = 2;
                channelOut = ch;
                clipOut    = i;
                return true;
            }
        }
    }

    return false;
}

void TimelinePanel::openClipEditor (int channelIndex, int clipIdx)
{
    const int fi = currentFixtureIndex();
    if (fi < 0)
        return;

    auto& f = processorRef.getFixtures()[(size_t) fi];
    if (channelIndex < 0 || channelIndex >= (int) f.channels.size())
        return;

    auto& clips = f.channels[(size_t) channelIndex].clips;
    if (clipIdx < 0 || clipIdx >= (int) clips.size())
        return;

    const auto& cl = clips[(size_t) clipIdx];

    auto* aw = new juce::AlertWindow ("Editar efecto",
                                      "Oscilador (LFO) sobre el canal.",
                                      juce::MessageBoxIconType::NoIcon);

    const auto typeNames = allEffectTypeNames();
    aw->addComboBox ("type", typeNames, "Forma de onda");
    aw->getComboBoxComponent ("type")->setSelectedItemIndex (
        typeNames.indexOf (effectTypeToString (cl.type)), juce::dontSendNotification);

    aw->addTextEditor ("period", juce::String (cl.periodBeats, 3), "Periodo (beats por ciclo)");
    aw->addTextEditor ("low",    juce::String ((int) cl.low),       "Minimo (0-255)");
    aw->addTextEditor ("high",   juce::String ((int) cl.high),      "Maximo (0-255)");
    aw->addTextEditor ("phase",  juce::String (cl.phase, 3),        "Fase (0-1)");

    aw->addButton ("Guardar",  1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, fi, channelIndex, clipIdx, aw] (int result)
        {
            if (result == 1)
            {
                auto& fx = processorRef.getFixtures()[(size_t) fi];
                if (channelIndex < (int) fx.channels.size())
                {
                    auto& cls = fx.channels[(size_t) channelIndex].clips;
                    if (clipIdx < (int) cls.size())
                    {
                        auto& c = cls[(size_t) clipIdx];
                        c.type        = effectTypeFromString (
                            allEffectTypeNames()[aw->getComboBoxComponent ("type")->getSelectedItemIndex()]);
                        c.periodBeats = juce::jmax (0.01, aw->getTextEditorContents ("period").getDoubleValue());
                        c.low         = (float) juce::jlimit (0, 255, aw->getTextEditorContents ("low").getIntValue());
                        c.high        = (float) juce::jlimit (0, 255, aw->getTextEditorContents ("high").getIntValue());
                        c.phase       = aw->getTextEditorContents ("phase").getDoubleValue();
                        processorRef.markFixturesDirty();
                    }
                }
            }
            repaint();
        }), true);
}

//==============================================================================
void TimelinePanel::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();   // para que la barra espaciadora controle el transporte

    const int fi = currentFixtureIndex();
    if (fi < 0)
        return;

    auto& f = processorRef.getFixtures()[(size_t) fi];
    const int numCh = (int) f.channels.size();
    if (numCh == 0)
        return;

    // Click/arrastre en la regla de compases (franja superior): mover el playhead.
    if (! e.mods.isPopupMenu() && getRulerBounds().expanded (0, 2).contains (e.getPosition()))
    {
        scrubbing = true;
        double beats = xToBeat (e.position.x);
        if (snapButton.getToggleState())
            beats = std::round (beats);
        processorRef.seekToBeats (beats);
        repaint();
        return;
    }

    // --- Modo efectos: crear / mover / redimensionar / borrar clips LFO ---
    if (clipModeButton.getToggleState())
    {
        int hc = -1, hi = -1, edge = 0;
        if (hitTestClip (e.getPosition(), hc, hi, edge))
        {
            // Clic derecho sobre un clip: borrarlo.
            if (e.mods.isPopupMenu())
            {
                pushUndoSnapshot();
                auto& clips = f.channels[(size_t) hc].clips;
                clips.erase (clips.begin() + hi);
                processorRef.markFixturesDirty();
                repaint();
                return;
            }

            draggingClip = true;
            clipChannel  = hc;
            clipIndex    = hi;
            clipDragMode = edge;
            clipGrabOffset = xToBeat (e.position.x) - f.channels[(size_t) hc].clips[(size_t) hi].startBeats;
            pushUndoSnapshot();
            return;
        }

        if (e.mods.isPopupMenu())
            return;

        // Zona vacia: crear un clip nuevo y empezar a redimensionar su borde derecho.
        const auto tracks = getTracksBounds();
        if (! tracks.contains (e.getPosition()))
            return;

        const float rowH = (float) tracks.getHeight() / (float) numCh;
        int ch = juce::jlimit (0, numCh - 1, (int) ((e.position.y - tracks.getY()) / rowH));

        double start = xToBeat (e.position.x);
        if (snapButton.getToggleState())
            start = juce::jlimit (0.0, totalBeats(), std::round (start));

        pushUndoSnapshot();
        EffectClip clip;
        clip.startBeats  = start;
        clip.lengthBeats = juce::jmax (0.25, juce::jmin (1.0, totalBeats() - start));
        clip.type        = EffectType::Sine;
        clip.periodBeats = 1.0;
        clip.low         = 0.0f;
        clip.high        = 255.0f;

        auto& clips = f.channels[(size_t) ch].clips;
        clips.push_back (clip);

        draggingClip = true;
        clipChannel  = ch;
        clipIndex    = (int) clips.size() - 1;
        clipDragMode = 2;   // redimensionar borde derecho mientras se arrastra
        processorRef.markFixturesDirty();
        repaint();
        return;
    }

    // Click en la muestra de color de una etiqueta: abrir selector de color.
    if (! e.mods.isPopupMenu())
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            if (getSwatchBounds (ch, numCh).contains (e.getPosition()))
            {
                openColourPicker (ch);
                return;
            }
        }
    }

    int hitCh = -1, hitKf = -1;
    const bool onKeyframe = hitTestKeyframe (e.getPosition(), hitCh, hitKf);

    // Click derecho sobre keyframe: alterna rampa/step.
    if (e.mods.isPopupMenu())
    {
        if (onKeyframe)
        {
            pushUndoSnapshot();
            auto& k = f.channels[(size_t) hitCh].keyframes[(size_t) hitKf];
            k.stepped = ! k.stepped;
            processorRef.markFixturesDirty();
            repaint();
        }
        return;
    }

    if (onKeyframe)
    {
        pushUndoSnapshot();
        dragging     = true;
        dragChannel  = hitCh;
        dragKeyframe = hitKf;
        return;
    }

    // Click en zona vacia: determinar canal por Y y crear keyframe.
    const auto tracks = getTracksBounds();
    if (! tracks.contains (e.getPosition()))
        return;

    const float rowH = (float) tracks.getHeight() / (float) numCh;
    int ch = (int) ((e.position.y - tracks.getY()) / rowH);
    ch = juce::jlimit (0, numCh - 1, ch);

    const auto row = getRowBounds (ch, numCh);

    pushUndoSnapshot();
    Keyframe k;
    k.timeBeats = xToBeat (e.position.x);
    if (snapButton.getToggleState())
        k.timeBeats = juce::jlimit (0.0, totalBeats(), std::round (k.timeBeats));
    k.value = std::round (yToValue (e.position.y, row));

    auto& kfs = f.channels[(size_t) ch].keyframes;
    kfs.push_back (k);
    sortKeyframes (kfs);

    // Localizar el indice del recien insertado para arrastrarlo.
    for (int i = 0; i < (int) kfs.size(); ++i)
    {
        if (kfs[(size_t) i].timeBeats == k.timeBeats && kfs[(size_t) i].value == k.value)
        {
            dragKeyframe = i;
            break;
        }
    }

    dragging    = true;
    dragChannel = ch;
    processorRef.markFixturesDirty();
    repaint();
}

void TimelinePanel::mouseDrag (const juce::MouseEvent& e)
{
    // Arrastre de un clip de efecto (mover o redimensionar).
    if (draggingClip)
    {
        const int fi = currentFixtureIndex();
        if (fi < 0)
            return;

        auto& f = processorRef.getFixtures()[(size_t) fi];
        if (clipChannel < 0 || clipChannel >= (int) f.channels.size())
            return;

        auto& clips = f.channels[(size_t) clipChannel].clips;
        if (clipIndex < 0 || clipIndex >= (int) clips.size())
            return;

        auto& cl = clips[(size_t) clipIndex];
        double b = xToBeat (e.position.x);
        if (snapButton.getToggleState())
            b = std::round (b);

        if (clipDragMode == 0)   // mover
        {
            cl.startBeats = juce::jlimit (0.0, juce::jmax (0.0, totalBeats() - cl.lengthBeats),
                                          b - clipGrabOffset);
        }
        else if (clipDragMode == 2)   // redimensionar borde derecho
        {
            cl.lengthBeats = juce::jlimit (0.25, totalBeats() - cl.startBeats,
                                           b - cl.startBeats);
        }
        else   // redimensionar borde izquierdo
        {
            const double end = cl.startBeats + cl.lengthBeats;
            const double ns  = juce::jlimit (0.0, end - 0.25, b);
            cl.startBeats  = ns;
            cl.lengthBeats = end - ns;
        }

        processorRef.markFixturesDirty();
        repaint();
        return;
    }

    // Arrastre del playhead desde la regla.
    if (scrubbing)
    {
        double beats = xToBeat (e.position.x);
        if (snapButton.getToggleState())
            beats = std::round (beats);
        processorRef.seekToBeats (beats);
        repaint();
        return;
    }

    if (! dragging || dragChannel < 0 || dragKeyframe < 0)
        return;

    const int fi = currentFixtureIndex();
    if (fi < 0)
        return;

    auto& f = processorRef.getFixtures()[(size_t) fi];
    if (dragChannel >= (int) f.channels.size())
        return;

    auto& kfs = f.channels[(size_t) dragChannel].keyframes;
    if (dragKeyframe >= (int) kfs.size())
        return;

    const int numCh = (int) f.channels.size();
    const auto row = getRowBounds (dragChannel, numCh);

    double beats = xToBeat (e.position.x);
    if (snapButton.getToggleState())
        beats = juce::jlimit (0.0, totalBeats(), std::round (beats));

    kfs[(size_t) dragKeyframe].timeBeats = beats;
    kfs[(size_t) dragKeyframe].value     = std::round (yToValue (e.position.y, row));

    // Mantener orden por tiempo, siguiendo el keyframe arrastrado.
    while (dragKeyframe > 0
           && kfs[(size_t) dragKeyframe].timeBeats < kfs[(size_t) dragKeyframe - 1].timeBeats)
    {
        std::swap (kfs[(size_t) dragKeyframe], kfs[(size_t) dragKeyframe - 1]);
        --dragKeyframe;
    }
    while (dragKeyframe < (int) kfs.size() - 1
           && kfs[(size_t) dragKeyframe].timeBeats > kfs[(size_t) dragKeyframe + 1].timeBeats)
    {
        std::swap (kfs[(size_t) dragKeyframe], kfs[(size_t) dragKeyframe + 1]);
        ++dragKeyframe;
    }

    processorRef.markFixturesDirty();
    repaint();
}

void TimelinePanel::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        processorRef.markFixturesDirty();

    dragging     = false;
    dragChannel  = -1;
    dragKeyframe = -1;
    scrubbing    = false;

    if (draggingClip)
        processorRef.markFixturesDirty();
    draggingClip = false;
    clipChannel  = -1;
    clipIndex    = -1;
    clipDragMode = 0;
}

void TimelinePanel::mouseMove (const juce::MouseEvent& e)
{
    hoverPos = e.getPosition();

    // Cursor "mano" al pasar por la regla de compases (zona de scrub).
    const bool overRuler = getRulerBounds().expanded (0, 2).contains (e.getPosition());
    if (overRuler)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    else if (clipModeButton.getToggleState())
    {
        // En modo efectos: cursor de redimension en los bordes, mano en el cuerpo.
        int hc = -1, hi = -1, edge = 0;
        if (hitTestClip (e.getPosition(), hc, hi, edge))
            setMouseCursor (edge == 0 ? juce::MouseCursor::DraggingHandCursor
                                      : juce::MouseCursor::LeftRightResizeCursor);
        else
            setMouseCursor (juce::MouseCursor::NormalCursor);
    }
    else
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    const int fi = currentFixtureIndex();
    const int numCh = (fi >= 0) ? (int) processorRef.getFixtures()[(size_t) fi].channels.size() : 0;

    const auto tracks = getTracksBounds();
    int newHover = -1;
    if (numCh > 0 && tracks.contains (e.getPosition()))
    {
        const float rowH = (float) tracks.getHeight() / (float) numCh;
        newHover = juce::jlimit (0, numCh - 1, (int) ((e.position.y - tracks.getY()) / rowH));
    }

    if (newHover != hoverRow)
    {
        hoverRow = newHover;
        repaint();
    }
    else
    {
        // refrescar solo la regla (indicador de posicion)
        repaint (getRulerBounds().withTrimmedTop (-kRulerHeight));
    }
}

void TimelinePanel::mouseExit (const juce::MouseEvent&)
{
    hoverPos = { -1, -1 };
    hoverRow = -1;
    repaint();
}


void TimelinePanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int fi = currentFixtureIndex();
    if (fi < 0)
        return;

    // En modo efectos, doble clic sobre un clip abre su editor de parametros.
    if (clipModeButton.getToggleState())
    {
        int hc = -1, hi = -1, edge = 0;
        if (hitTestClip (e.getPosition(), hc, hi, edge))
            openClipEditor (hc, hi);
        return;
    }

    int hitCh = -1, hitKf = -1;
    if (hitTestKeyframe (e.getPosition(), hitCh, hitKf))
    {
        pushUndoSnapshot();
        auto& f = processorRef.getFixtures()[(size_t) fi];
        auto& kfs = f.channels[(size_t) hitCh].keyframes;
        kfs.erase (kfs.begin() + hitKf);
        processorRef.markFixturesDirty();
        repaint();
    }
}

//==============================================================================
void TimelinePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0f1115));

    // --- Barra de progreso de captura IA (detras de la etiqueta de estado) ---
    {
        auto aiBar = getAiBarBounds();
        // La zona util empieza tras los dos botones (Capturar 130 + 8 + Generar 104 + 12).
        auto barArea = aiBar.withTrimmedLeft (130 + 8 + 104 + 12).reduced (0, 5);
        if (barArea.getWidth() > 40)
        {
            const float corner = 4.0f;
            g.setColour (juce::Colour (0xff1c2230));
            g.fillRoundedRectangle (barArea.toFloat(), corner);

            const double frac = processorRef.getCaptureFillFraction();
            if (frac > 0.0)
            {
                auto fill = barArea.toFloat().withWidth ((float) (barArea.getWidth() * frac));
                const bool cap = processorRef.isCapturing();
                g.setColour (cap ? juce::Colour (0xffb03030) : juce::Colour (0xff6a4a8a));
                g.fillRoundedRectangle (fill, corner);
            }

            g.setColour (juce::Colour (0xff2a3140));
            g.drawRoundedRectangle (barArea.toFloat(), corner, 1.0f);
        }
    }

    const int fi = currentFixtureIndex();
    if (fi < 0)
    {
        g.setColour (juce::Colour (0xffffb020));
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        auto msgArea = getLocalBounds().withTrimmedTop (44 + kAiBarHeight);
        g.drawText ("No hay equipos: anade equipos en la pestana \"Equipos\" "
                    "antes de generar o automatizar.",
                    msgArea, juce::Justification::centred, true);
        return;
    }

    const auto& f = processorRef.getFixtures()[(size_t) fi];
    const int numCh = (int) f.channels.size();
    if (numCh == 0)
        return;

    const auto tracks = getTracksBounds();
    const auto ruler  = getRulerBounds();
    const int  bpb    = beatsPerBar();
    const int  bars   = (int) barsSlider.getValue();
    const double totalB = totalBeats();

    using P = LuxLookAndFeel::Palette;
    const juce::Colour accent { P::accent };

    // --- Fondo de la regla de compases ---
    g.setColour (juce::Colour (P::bg0));
    g.fillRect (ruler);

    // A partir de aqui, recortamos al area derecha (regla + pistas) para que las
    // lineas no invadan la columna de etiquetas al hacer scroll.
    g.saveState();
    g.reduceClipRegion (ruler.getUnion (tracks));

    // --- Zebra de compases: sombrea los compases impares en TODA la altura ---
    for (int bar = 0; bar < bars; ++bar)
    {
        if (bar % 2 == 1)
        {
            const float x0 = beatToX (bar * bpb);
            const float x1 = beatToX ((bar + 1) * bpb);
            g.setColour (juce::Colours::white.withAlpha (0.022f));
            g.fillRect (x0, (float) tracks.getY(), x1 - x0, (float) tracks.getHeight());
        }
    }

    // --- Pass A: fondos de fila, hover, guias horizontales de valor, separadores ---
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto row = getRowBounds (ch, numCh);
        const auto chanCol = f.channels[(size_t) ch].colour;

        g.setColour ((ch % 2 == 0) ? juce::Colour (0xff14171c) : juce::Colour (0xff111419));
        g.fillRect (row);

        // Tinte sutil de la fila con el color del canal
        g.setColour (chanCol.withAlpha (0.05f));
        g.fillRect (row);

        // Resaltado de la fila bajo el cursor (sutil)
        if (ch == hoverRow && ! dragging)
        {
            g.setColour (accent.withAlpha (0.05f));
            g.fillRect (row);
        }

        // Guias horizontales de valor (25/50/75%)
        auto valueArea = row.reduced (0, 6);
        for (int q = 1; q <= 3; ++q)
        {
            const float y = (float) valueArea.getY() + (float) valueArea.getHeight() * (q / 4.0f);
            g.setColour (juce::Colours::white.withAlpha (q == 2 ? 0.06f : 0.03f));
            g.drawHorizontalLine ((int) y, (float) tracks.getX(), (float) tracks.getRight());
        }

        // Separador de fila
        g.setColour (juce::Colour (P::lineSoft));
        g.drawHorizontalLine (row.getBottom() - 1, (float) tracks.getX(), (float) tracks.getRight());
    }

    // --- Grid vertical ENCIMA de las pistas (no se pierde al bajar) ---
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (int bar = 0; bar <= bars; ++bar)
    {
        // Subdivisiones de beat (finas)
        if (bar < bars)
        {
            for (int beat = 1; beat < bpb; ++beat)
            {
                const float bx = beatToX (bar * bpb + beat);
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.drawVerticalLine ((int) bx, (float) tracks.getY(), (float) tracks.getBottom());
            }
        }

        // Linea de compas (mas marcada, ambar tenue) recorriendo toda la altura
        const float x = beatToX (bar * bpb);
        g.setColour (accent.withAlpha (0.22f));
        g.drawVerticalLine ((int) x, (float) ruler.getY(), (float) tracks.getBottom());

        // Numero de compas en la regla
        if (bar < bars)
        {
            g.setColour (juce::Colour (P::textMid));
            g.drawText (juce::String (bar + 1), (int) x + 5, ruler.getY(), 30, ruler.getHeight(),
                        juce::Justification::centredLeft, false);
        }
    }

    // Indicador de posicion del cursor en la regla
    if (hoverPos.x >= tracks.getX() && hoverPos.x <= tracks.getRight())
    {
        g.setColour (accent.withAlpha (0.6f));
        g.fillRect ((float) hoverPos.x - 0.5f, (float) ruler.getY(), 1.0f, (float) ruler.getHeight());
        juce::Path tri;
        tri.addTriangle ((float) hoverPos.x - 4.0f, (float) ruler.getBottom() - 5.0f,
                         (float) hoverPos.x + 4.0f, (float) ruler.getBottom() - 5.0f,
                         (float) hoverPos.x,        (float) ruler.getBottom());
        g.fillPath (tri);
    }

    // --- Guias de onsets detectados (convertidos a beats) ---
    const auto& analysis = processorRef.getLastAnalysis();
    if (analysis.valid)
    {
        const double bpm = currentBpm();
        g.setColour (accent.withAlpha (0.16f));
        for (double t : analysis.onsetTimes)
        {
            const double beats = t * bpm / 60.0;
            if (beats >= 0.0 && beats <= totalB)
                g.drawVerticalLine ((int) beatToX (beats), (float) tracks.getY(), (float) tracks.getBottom());
        }
    }

    // --- Pass B: curvas y keyframes por canal ---
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto row = getRowBounds (ch, numCh);
        const auto& chan = f.channels[(size_t) ch];
        const auto& kfs = chan.keyframes;
        if (kfs.empty())
            continue;

        const juce::Colour chanCol = chan.colour;

        juce::Path path;
        bool started = false;
        float prevY = 0.0f;

        for (size_t i = 0; i < kfs.size(); ++i)
        {
            const float kx = beatToX (kfs[i].timeBeats);
            const float ky = valueToY (kfs[i].value, row);

            if (! started)
            {
                path.startNewSubPath (kx, ky);
                started = true;
            }
            else
            {
                if (kfs[i - 1].stepped)
                    path.lineTo (kx, prevY);   // mantener valor anterior
                path.lineTo (kx, ky);
            }

            prevY = ky;
        }

        // Relleno tenue bajo la curva
        {
            juce::Path fill = path;
            const float lastX = beatToX (kfs.back().timeBeats);
            const float firstX = beatToX (kfs.front().timeBeats);
            fill.lineTo (lastX, (float) row.getBottom());
            fill.lineTo (firstX, (float) row.getBottom());
            fill.closeSubPath();
            g.setColour (chanCol.withAlpha (0.10f));
            g.fillPath (fill);
        }

        g.setColour (chanCol);
        g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // Puntos de keyframe
        for (size_t i = 0; i < kfs.size(); ++i)
        {
            const float kx = beatToX (kfs[i].timeBeats);
            const float ky = valueToY (kfs[i].value, row);
            const bool isDragged = (dragging && dragChannel == ch && dragKeyframe == (int) i);
            const auto col = kfs[i].stepped ? accent : chanCol;

            // Glow
            g.setColour (col.withAlpha (isDragged ? 0.5f : 0.25f));
            g.fillEllipse (kx - 7.0f, ky - 7.0f, 14.0f, 14.0f);

            g.setColour (isDragged ? juce::Colours::white : col);
            g.fillEllipse (kx - 4.0f, ky - 4.0f, 8.0f, 8.0f);
            g.setColour (juce::Colour (P::bg0));
            g.drawEllipse (kx - 4.0f, ky - 4.0f, 8.0f, 8.0f, 1.2f);
        }
    }

    // --- Pass C: clips de efecto (LFO) por canal, encima de las curvas ---
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto& chan = f.channels[(size_t) ch];
        const juce::Colour col = chan.colour;

        for (int i = 0; i < (int) chan.clips.size(); ++i)
        {
            const auto& cl = chan.clips[(size_t) i];
            const auto b = getClipBounds (ch, i, numCh);
            const bool active = (draggingClip && clipChannel == ch && clipIndex == i);

            // Cuerpo translucido
            g.setColour (col.withAlpha (active ? 0.22f : 0.14f));
            g.fillRoundedRectangle (b, 4.0f);

            // Forma de onda muestreada dentro del clip
            juce::Path wave;
            const int steps = juce::jmax (2, (int) b.getWidth());
            for (int s = 0; s <= steps; ++s)
            {
                const double local = (double) s / steps * cl.lengthBeats;
                const double pos   = (cl.periodBeats > 0.0 ? local / cl.periodBeats : 0.0) + cl.phase;
                const float  w     = effectWaveform (cl.type, pos - std::floor (pos), std::floor (pos));
                const float  vv    = juce::jlimit (0.0f, 255.0f, cl.low + (cl.high - cl.low) * w);
                const float  fx    = b.getX() + (float) s / (float) steps * b.getWidth();
                const float  fy    = b.getBottom() - 4.0f - (vv / 255.0f) * (b.getHeight() - 8.0f);
                if (s == 0) wave.startNewSubPath (fx, fy);
                else        wave.lineTo (fx, fy);
            }
            g.setColour (col.brighter (0.4f).withAlpha (0.9f));
            g.strokePath (wave, juce::PathStrokeType (1.4f));

            // Borde
            g.setColour (col.withAlpha (active ? 0.95f : 0.6f));
            g.drawRoundedRectangle (b, 4.0f, active ? 1.8f : 1.0f);

            // Asas de redimension en los bordes
            g.setColour (col.withAlpha (0.85f));
            g.fillRect (b.getX(), b.getY(), 2.5f, b.getHeight());
            g.fillRect (b.getRight() - 2.5f, b.getY(), 2.5f, b.getHeight());

            // Etiqueta con la forma de onda
            if (b.getWidth() > 36.0f)
            {
                g.setColour (col.brighter (0.6f));
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (effectTypeToString (cl.type),
                            b.reduced (5.0f, 2.0f).toNearestInt(),
                            juce::Justification::topLeft, false);
            }
        }
    }

    g.restoreState();   // fin del recorte del area derecha

    // --- Etiquetas de canal (columna izquierda), encima de todo ---
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto row = getRowBounds (ch, numCh);
        const auto& chan = f.channels[(size_t) ch];

        juce::Rectangle<int> labelArea (8, row.getY(), kLabelWidth - 8, row.getHeight());
        g.setColour ((ch == hoverRow && ! dragging) ? juce::Colour (0xff20242c)
                                                     : juce::Colour (0xff1a1d23));
        g.fillRect (labelArea);
        g.setColour (juce::Colour (P::lineSoft));
        g.drawHorizontalLine (labelArea.getBottom() - 1, (float) labelArea.getX(), (float) labelArea.getRight());

        // Muestra de color del canal (clicable para abrir el selector)
        const auto sw = getSwatchBounds (ch, numCh);
        g.setColour (chan.colour);
        g.fillRoundedRectangle (sw.toFloat(), 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (sw.toFloat(), 3.0f, 1.0f);

        auto textArea = labelArea.withTrimmedLeft (sw.getRight() - labelArea.getX() + 6);
        g.setColour (accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        const auto chName = chan.label.isNotEmpty() ? chan.label : channelTypeToString (chan.type);
        g.drawText (juce::String (ch + 1) + ". " + chName,
                    textArea, juce::Justification::centredLeft, true);
        g.setColour (juce::Colour (P::textDim));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("DMX " + juce::String (f.dmxAddressOf (ch)),
                    textArea.reduced (0, 2), juce::Justification::bottomLeft, true);
    }

    // Separador vertical entre etiquetas y pistas
    g.setColour (juce::Colour (P::line));
    g.drawVerticalLine (tracks.getX() - 1, (float) ruler.getY(), (float) tracks.getBottom());

    g.saveState();
    g.reduceClipRegion (ruler.getUnion (tracks));

    // --- Playhead del transporte (host o interno) ---
    {
        const double ppq = processorRef.getPlayheadBeats();
        if (ppq >= 0.0 && ppq <= totalBeats())
        {
            const float px = beatToX (ppq);
            const bool playing = processorRef.isPlayingNow();
            const juce::Colour ph = playing ? juce::Colour (0xff62d96b) : juce::Colour (0xffb0b6c0);

            // Glow + linea de posicion
            g.setColour (ph.withAlpha (0.25f));
            g.fillRect (px - 1.5f, (float) ruler.getY(), 3.0f, (float) (tracks.getBottom() - ruler.getY()));
            g.setColour (ph);
            g.fillRect (px - 0.75f, (float) ruler.getY(), 1.5f, (float) (tracks.getBottom() - ruler.getY()));

            // Marcador triangular en la regla
            juce::Path head;
            head.addTriangle (px - 5.0f, (float) ruler.getY(),
                              px + 5.0f, (float) ruler.getY(),
                              px,        (float) ruler.getY() + 7.0f);
            g.setColour (ph);
            g.fillPath (head);

            // Badge de compas:beat mientras se arrastra el playhead en la regla.
            if (scrubbing)
            {
                const int    barNo  = (int) (ppq / bpb) + 1;
                const double beatIn = ppq - (barNo - 1) * bpb + 1.0;
                juce::String txt = juce::String (barNo) + "." + juce::String (beatIn, 2);

                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                const int tw = 64, th = 18;
                int bx = (int) px + 8;
                int by = (int) ruler.getY() + 2;
                if (bx + tw > tracks.getRight()) bx = (int) px - tw - 8;

                juce::Rectangle<int> badge (bx, by, tw, th);
                g.setColour (juce::Colour (P::bg0).withAlpha (0.92f));
                g.fillRoundedRectangle (badge.toFloat(), 4.0f);
                g.setColour (ph.withAlpha (0.7f));
                g.drawRoundedRectangle (badge.toFloat(), 4.0f, 1.0f);
                g.setColour (juce::Colour (P::textHi));
                g.drawText (txt, badge, juce::Justification::centred, false);
            }
        }
    }

    // --- Etiqueta de valor del keyframe que se esta arrastrando ---
    if (dragging && dragChannel >= 0 && dragChannel < numCh)
    {
        const auto& kfs = f.channels[(size_t) dragChannel].keyframes;
        if (dragKeyframe >= 0 && dragKeyframe < (int) kfs.size())
        {
            const auto& k = kfs[(size_t) dragKeyframe];
            const float kx = beatToX (k.timeBeats);
            const float ky = valueToY (k.value, getRowBounds (dragChannel, numCh));

            const double beatPos = k.timeBeats;
            const int    barNo   = (int) (beatPos / bpb) + 1;
            const double beatIn  = beatPos - (barNo - 1) * bpb + 1.0;
            juce::String txt = juce::String ((int) k.value) + "  ·  " + juce::String (barNo)
                             + "." + juce::String (beatIn, 2);

            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            const int tw = 96, th = 18;
            int bx = (int) kx + 10;
            int by = (int) ky - th - 6;
            if (bx + tw > tracks.getRight()) bx = (int) kx - tw - 10;
            if (by < tracks.getY())          by = (int) ky + 8;

            juce::Rectangle<int> badge (bx, by, tw, th);
            g.setColour (juce::Colour (P::bg0).withAlpha (0.92f));
            g.fillRoundedRectangle (badge.toFloat(), 4.0f);
            g.setColour (accent.withAlpha (0.6f));
            g.drawRoundedRectangle (badge.toFloat(), 4.0f, 1.0f);
            g.setColour (juce::Colour (P::textHi));
            g.drawText (txt, badge, juce::Justification::centred, false);
        }
    }

    g.restoreState();

    // Marco de la zona de pistas
    g.setColour (juce::Colour (P::line));
    g.drawRect (tracks);
}

void TimelinePanel::resized()
{
    auto area = getLocalBounds().reduced (8);

    // Franja de controles
    auto controls = area.removeFromTop (44);

    auto block = [&controls] (juce::Label& lbl, juce::Component& c, int labelW, int compW)
    {
        lbl.setBounds (controls.removeFromLeft (labelW).withSizeKeepingCentre (labelW, 22));
        c.setBounds   (controls.removeFromLeft (compW).withSizeKeepingCentre (compW, 26));
        controls.removeFromLeft (10);
    };

    block (fixtureLabel, fixtureCombo, 46, 180);
    block (bpmLabel,     bpmSlider,    34, 96);
    block (barsLabel,    barsSlider,   64, 96);
    block (beatsLabel,   beatsSlider,  58, 96);
    snapButton.setBounds (controls.removeFromLeft (108).withSizeKeepingCentre (108, 24));
    controls.removeFromLeft (10);
    clipModeButton.setBounds (controls.removeFromLeft (80).withSizeKeepingCentre (80, 24));
    controls.removeFromLeft (10);

    // Controles de transporte interno
    rewindButton.setBounds (controls.removeFromLeft (34).withSizeKeepingCentre (34, 26));
    controls.removeFromLeft (4);
    playButton.setBounds (controls.removeFromLeft (58).withSizeKeepingCentre (58, 26));
    controls.removeFromLeft (4);
    stopButton.setBounds (controls.removeFromLeft (58).withSizeKeepingCentre (58, 26));
    controls.removeFromLeft (8);
    loopButton.setBounds (controls.removeFromLeft (70).withSizeKeepingCentre (70, 24));

    // Segunda franja: coreografia por IA (captura del DAW + generar).
    auto aiBar = area.removeFromTop (kAiBarHeight);
    captureButton.setBounds (aiBar.removeFromLeft (130).withSizeKeepingCentre (130, 24));
    aiBar.removeFromLeft (8);
    generateButton.setBounds (aiBar.removeFromLeft (104).withSizeKeepingCentre (104, 24));
    aiBar.removeFromLeft (12);
    aiStatusLabel.setBounds (aiBar.withSizeKeepingCentre (aiBar.getWidth(), 22));

    // Barra de scroll horizontal en la base, alineada con la zona de pistas.
    auto sb = getLocalBounds().reduced (8);
    auto sbRow = sb.removeFromBottom (kScrollBarHeight).withTrimmedLeft (kLabelWidth);
    hScroll.setBounds (sbRow);

    clampScroll();
    updateScrollBar();
}
