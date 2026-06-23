#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../../source/FixtureModel.h"
#include "../../source/Sequence.h"
#include "../../source/LuxLookAndFeel.h"
#include "DmxShow.h"
#include <algorithm>

/**
    Piano-roll MANUAL para el Automator (Fase 4).

    Edita directamente un DmxShow (la coreografia "manual" de un tema): una pista
    por canal del equipo seleccionado, con keyframes (valor 0..255 en el tiempo,
    en compases/beats sincronizados al BPM del show). Misma interaccion que el
    timeline del VST:

      - Click en zona vacia: crea un keyframe (y permite arrastrarlo).
      - Arrastrar keyframe: mueve tiempo (X) y valor (Y).
      - Doble click sobre keyframe: lo borra.
      - Click derecho sobre keyframe: alterna rampa/step.
      - Click en la muestra de color de la etiqueta: cambia el color del canal.

    El playhead se dibuja desde un callback externo (posicion del reproductor),
    para previsualizar mientras suena el tema.
*/
class ManualShowEditor : public juce::Component,
                         private juce::Timer,
                         private juce::ChangeListener,
                         private juce::ScrollBar::Listener
{
public:
    using P = LuxLookAndFeel::Palette;

    // Llamado tras cada edicion (para que el PlaylistManager marque/guarde).
    std::function<void()> onChanged;
    // Posicion actual del reproductor en segundos (o <0 si no aplica).
    std::function<double()> getPlayheadSeconds;
    // True si el tema esta sonando ahora.
    std::function<bool()> isPlaying;
    // Reproduce / pausa el tema que se esta editando (barra espaciadora).
    std::function<void()> onTogglePlay;
    // Mueve el reproductor a una posicion en segundos (clic en la regla).
    std::function<void (double)> onSeekSeconds;

    ManualShowEditor (DmxShow& showToEdit, const juce::String& trackName)
        : show (showToEdit)
    {
        title.setText ("Coreografia manual - " + trackName, juce::dontSendNotification);
        title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        addAndMakeVisible (title);

        fixtureLabel.setText ("Equipo", juce::dontSendNotification);
        fixtureLabel.setFont (juce::FontOptions (12.0f));
        fixtureLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        addAndMakeVisible (fixtureLabel);

        fixtureCombo.setTextWhenNothingSelected ("(sin equipos)");
        fixtureCombo.onChange = [this] { repaint(); };
        addAndMakeVisible (fixtureCombo);

        auto setupInc = [this] (juce::Label& lbl, juce::Slider& s, const juce::String& text,
                                double min, double max, double value)
        {
            lbl.setText (text, juce::dontSendNotification);
            lbl.setFont (juce::FontOptions (12.0f));
            lbl.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
            addAndMakeVisible (lbl);

            s.setSliderStyle (juce::Slider::IncDecButtons);
            s.setRange (min, max, 1.0);
            s.setValue (value, juce::dontSendNotification);
            s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 50, 22);
            addAndMakeVisible (s);
        };

        setupInc (bpmLabel,  bpmSlider,  "BPM",      40.0, 300.0, show.bpm > 0.0 ? show.bpm : 120.0);
        setupInc (beatsLabel, beatsSlider, "x compas", 1.0, 12.0, 4.0);
        bpmSlider.onValueChange   = [this] { show.bpm = bpmSlider.getValue(); markChanged(); repaint(); };
        beatsSlider.onValueChange = [this] { repaint(); };

        snapButton.setButtonText ("Snap a beat");
        snapButton.setToggleState (true, juce::dontSendNotification);
        snapButton.setColour (juce::ToggleButton::textColourId, juce::Colour (P::textMid));
        addAndMakeVisible (snapButton);

        clipModeButton.setButtonText ("Efectos");
        clipModeButton.setColour (juce::ToggleButton::textColourId, juce::Colour (P::accent));
        clipModeButton.setTooltip ("Modo efectos: arrastra en una pista para crear un clip LFO; "
                                   "arrastra el cuerpo para moverlo, los bordes para redimensionar, "
                                   "doble clic para editar forma de onda/periodo, clic derecho para borrar.");
        clipModeButton.onClick = [this] { updateHint(); repaint(); };
        addAndMakeVisible (clipModeButton);

        multiTrackButton.setButtonText ("Multipista");
        multiTrackButton.setClickingTogglesState (true);
        multiTrackButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (P::accent));
        multiTrackButton.setColour (juce::TextButton::textColourOnId,  juce::Colour (P::bg0));
        multiTrackButton.setColour (juce::TextButton::textColourOffId, juce::Colour (P::textMid));
        multiTrackButton.setTooltip ("Multipista: una pista por equipo (auto desde los Stems). En cada "
                                     "pista elige a la izquierda que canal mostrar/editar.");
        multiTrackButton.onClick = [this]
        {
            multiTrack = multiTrackButton.getToggleState();
            ensureMultiChannel();
            fixtureCombo.setVisible (! multiTrack);
            fixtureLabel.setVisible (! multiTrack);
            updateHint();
            repaint();
        };
        addAndMakeVisible (multiTrackButton);

        undoButton.setButtonText ("Deshacer");
        undoButton.setTooltip ("Deshace la ultima edicion (Ctrl+Z).");
        undoButton.onClick = [this] { undo(); };
        addAndMakeVisible (undoButton);

        hScroll.setAutoHide (false);
        hScroll.addListener (this);
        addAndMakeVisible (hScroll);

        hint.setFont (juce::FontOptions (11.0f));
        hint.setColour (juce::Label::textColourId, juce::Colour (P::textDim));
        hint.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (hint);
        updateHint();

        refreshFixtures();
        setWantsKeyboardFocus (true);
        setSize (940, 620);
        startTimerHz (30);
    }

    ~ManualShowEditor() override { stopTimer(); }

    //==============================================================================
    void resized() override
    {
        auto area = getLocalBounds().reduced (10);

        auto top = area.removeFromTop (26);
        title.setBounds (top.removeFromLeft (360));
        hint.setBounds (top);

        auto controls = area.removeFromTop (34);
        auto block = [&controls] (juce::Label& lbl, juce::Component& c, int lw, int cw)
        {
            lbl.setBounds (controls.removeFromLeft (lw).withSizeKeepingCentre (lw, 22));
            c.setBounds   (controls.removeFromLeft (cw).withSizeKeepingCentre (cw, 26));
            controls.removeFromLeft (10);
        };
        block (fixtureLabel, fixtureCombo, 46, 200);
        block (bpmLabel,     bpmSlider,    34, 92);
        block (beatsLabel,   beatsSlider,  58, 92);
        snapButton.setBounds (controls.removeFromLeft (110).withSizeKeepingCentre (110, 24));
        controls.removeFromLeft (10);
        clipModeButton.setBounds (controls.removeFromLeft (90).withSizeKeepingCentre (90, 24));
        controls.removeFromLeft (10);
        multiTrackButton.setBounds (controls.removeFromLeft (96).withSizeKeepingCentre (96, 24));
        controls.removeFromLeft (10);
        undoButton.setBounds (controls.removeFromLeft (84).withSizeKeepingCentre (84, 24));

        area.removeFromTop (6);
        body = area;

        hScroll.setBounds (body.getX() + kLabelWidth, body.getBottom() - kScrollBarHeight,
                           juce::jmax (10, body.getWidth() - kLabelWidth), kScrollBarHeight);
        updateScrollBar();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if ((k.getModifiers().isCtrlDown() || k.getModifiers().isCommandDown())
            && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
        {
            undo();
            return true;
        }
        // Barra espaciadora: reproduce / pausa el tema que se edita aqui.
        if (k == juce::KeyPress::spaceKey)
        {
            if (onTogglePlay) { onTogglePlay(); return true; }
        }
        return false;
    }

private:
    //==============================================================================
    void timerCallback() override
    {
        if (isPlaying && isPlaying())
        {
            followPlayhead();
            repaint();
        }
    }

    // Mientras suena, desplaza la vista para que el playhead no se salga (solo
    // tiene efecto si hay zoom, es decir, no cabe todo el tema en pantalla).
    void followPlayhead()
    {
        if (! getPlayheadSeconds) return;
        const double secs = getPlayheadSeconds();
        if (secs < 0.0) return;

        const double ppq = secs * currentBpm() / 60.0;
        const double vis = visibleBeats();
        if (vis >= totalBeats() - 1.0e-6) return;   // todo visible: nada que seguir

        // Si el playhead se acerca al borde derecho (o se salio), recoloca la
        // ventana dejando un margen a la izquierda; si va por detras, lo centra.
        const double margin = vis * 0.15;
        if (ppq > scrollBeats + vis - margin || ppq < scrollBeats)
        {
            scrollBeats = ppq - margin;
            clampScroll();
            updateScrollBar();
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster* src) override
    {
        if (auto* cs = dynamic_cast<juce::ColourSelector*> (src))
        {
            const int fi = colourEditFixture;
            if (fi >= 0 && fi < (int) show.fixtures.size() && colourEditChannel >= 0
                && colourEditChannel < (int) show.fixtures[(size_t) fi].channels.size())
            {
                show.fixtures[(size_t) fi].channels[(size_t) colourEditChannel].colour = cs->getCurrentColour();
                markChanged();
                repaint();
            }
        }
    }

    void refreshFixtures()
    {
        const int keep = fixtureCombo.getSelectedId();
        fixtureCombo.clear (juce::dontSendNotification);
        for (int i = 0; i < (int) show.fixtures.size(); ++i)
        {
            const auto& f = show.fixtures[(size_t) i];
            fixtureCombo.addItem (juce::String (i + 1) + ". " + f.name, i + 1);
        }
        if (keep >= 1 && keep <= (int) show.fixtures.size())
            fixtureCombo.setSelectedId (keep, juce::dontSendNotification);
        else if (! show.fixtures.empty())
            fixtureCombo.setSelectedId (1, juce::dontSendNotification);
        lastFixtureCount = (int) show.fixtures.size();
        ensureMultiChannel();
    }

    // Canal por defecto a mostrar en multipista para cada equipo: el Dimmer si lo
    // hay, si no el primero.
    void ensureMultiChannel()
    {
        multiChannel.resize (show.fixtures.size());
        for (size_t i = 0; i < show.fixtures.size(); ++i)
        {
            const auto& chs = show.fixtures[i].channels;
            if (multiChannel[i] < 0 || multiChannel[i] >= (int) chs.size())
            {
                int best = 0;
                for (int c = 0; c < (int) chs.size(); ++c)
                    if (chs[(size_t) c].type == ChannelType::Dimmer) { best = c; break; }
                multiChannel[i] = chs.empty() ? 0 : best;
            }
        }
    }

    // Una pista por (equipo, canal). En modo normal: todos los canales del equipo
    // elegido. En multipista: un equipo por fila (auto desde los Stems) mostrando el
    // canal seleccionado en su droplist.
    struct RowRef { int fixture = -1; int channel = -1; };

    std::vector<RowRef> rows() const
    {
        std::vector<RowRef> r;
        if (multiTrack)
        {
            for (int fi = 0; fi < (int) show.fixtures.size(); ++fi)
            {
                const auto& f = show.fixtures[(size_t) fi];
                if (f.channels.empty()) continue;
                int ch = (fi < (int) multiChannel.size()) ? multiChannel[(size_t) fi] : 0;
                ch = juce::jlimit (0, (int) f.channels.size() - 1, ch);
                r.push_back ({ fi, ch });
            }
        }
        else
        {
            const int fi = currentFixtureIndex();
            if (fi >= 0)
                for (int ch = 0; ch < (int) show.fixtures[(size_t) fi].channels.size(); ++ch)
                    r.push_back ({ fi, ch });
        }
        return r;
    }

    //==============================================================================
    // Historial (deshacer): snapshot del show completo antes de cada edicion.
    void pushUndo()
    {
        undoStack.push_back (show.toValueTree());
        if (undoStack.size() > 80) undoStack.erase (undoStack.begin());
    }

    void undo()
    {
        if (undoStack.empty()) return;
        show = DmxShow::fromValueTree (undoStack.back());
        undoStack.pop_back();
        refreshFixtures();
        markChanged();
        repaint();
    }

    void markChanged() { if (onChanged) onChanged(); }

    void updateHint()
    {
        if (multiTrack)
            hint.setText ("Multipista: clic en el nombre (izq) = elegir canal · "
                          "Ctrl+rueda = zoom · Ctrl+Z = deshacer",
                          juce::dontSendNotification);
        else if (clipModeButton.getToggleState())
            hint.setText ("Efectos: arrastra = crear clip · mueve cuerpo/bordes · "
                          "doble clic = editar · clic derecho = borrar",
                          juce::dontSendNotification);
        else
            hint.setText ("Click = keyframe · arrastra = mover · doble click = borrar · "
                          "clic derecho = rampa/step · Ctrl+rueda = zoom · Ctrl+Z = deshacer",
                          juce::dontSendNotification);
    }

    int currentFixtureIndex() const
    {
        const int id = fixtureCombo.getSelectedId();
        if (id <= 0 || id > (int) show.fixtures.size())
            return show.fixtures.empty() ? -1 : 0;
        return id - 1;
    }

    int    beatsPerBar() const { return (int) beatsSlider.getValue(); }
    double currentBpm()  const { return juce::jmax (1.0, bpmSlider.getValue()); }

    double totalBeats() const
    {
        const double secs = show.lengthSeconds > 0.0 ? show.lengthSeconds : 30.0;
        return juce::jmax ((double) beatsPerBar(), secs * currentBpm() / 60.0);
    }

    //==============================================================================
    juce::Rectangle<int> getRulerBounds() const
    {
        return body.withHeight (kRulerHeight).withTrimmedLeft (kLabelWidth);
    }

    juce::Rectangle<int> getTracksBounds() const
    {
        auto a = body;
        a.removeFromTop (kRulerHeight);
        a.removeFromBottom (kScrollBarHeight + 2);
        return a.withTrimmedLeft (kLabelWidth);
    }

    juce::Rectangle<int> getRowBounds (int ch, int numCh) const
    {
        auto t = getTracksBounds();
        if (numCh <= 0) return t;
        const float rowH = (float) t.getHeight() / (float) numCh;
        return { t.getX(), t.getY() + (int) (ch * rowH), t.getWidth(), (int) rowH };
    }

    juce::Rectangle<int> getSwatchBounds (int ch, int numCh) const
    {
        const auto row = getRowBounds (ch, numCh);
        const int sz = juce::jlimit (8, 16, row.getHeight() - 8);
        return { body.getX() + 4, row.getCentreY() - sz / 2, sz, sz };
    }

    // --- Zoom / scroll horizontal ---
    double pixelsPerBeat() const
    {
        auto t = getTracksBounds();
        return (double) juce::jmax (1, t.getWidth()) / juce::jmax (1.0, totalBeats()) * zoom;
    }
    double visibleBeats() const { return totalBeats() / juce::jmax (1.0, zoom); }

    void clampScroll()
    {
        scrollBeats = juce::jlimit (0.0, juce::jmax (0.0, totalBeats() - visibleBeats()), scrollBeats);
    }

    void updateScrollBar()
    {
        hScroll.setRangeLimits (0.0, juce::jmax (1.0, totalBeats()), juce::dontSendNotification);
        hScroll.setCurrentRange (scrollBeats, visibleBeats(), juce::dontSendNotification);
    }

    void scrollBarMoved (juce::ScrollBar*, double newStart) override
    {
        if (std::abs (newStart - scrollBeats) > 1.0e-6)
        {
            scrollBeats = newStart;
            repaint();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        {
            const double beatUnderCursor = xToBeat (e.position.x);
            zoom = juce::jlimit (1.0, 80.0, zoom * (w.deltaY > 0 ? 1.18 : 1.0 / 1.18));
            auto t = getTracksBounds();
            scrollBeats = beatUnderCursor - (e.position.x - t.getX()) / pixelsPerBeat();
            clampScroll();
            updateScrollBar();
            repaint();
        }
        else
        {
            scrollBeats -= (w.deltaY + w.deltaX) * visibleBeats() * 0.25;
            clampScroll();
            updateScrollBar();
            repaint();
        }
    }

    float beatToX (double beats) const
    {
        auto t = getTracksBounds();
        return (float) t.getX() + (float) ((beats - scrollBeats) * pixelsPerBeat());
    }

    double xToBeat (float x) const
    {
        auto t = getTracksBounds();
        return juce::jlimit (0.0, totalBeats(),
                             scrollBeats + (x - t.getX()) / juce::jmax (1.0e-6, pixelsPerBeat()));
    }

    float valueToY (float value, juce::Rectangle<int> row) const
    {
        auto r = row.reduced (0, 6);
        const float frac = juce::jlimit (0.0f, 1.0f, value / 255.0f);
        return (float) r.getBottom() - frac * (float) r.getHeight();
    }

    float yToValue (float y, juce::Rectangle<int> row) const
    {
        auto r = row.reduced (0, 6);
        const float frac = ((float) r.getBottom() - y) / (float) juce::jmax (1, r.getHeight());
        return juce::jlimit (0.0f, 255.0f, frac * 255.0f);
    }

    bool hitTestKeyframe (juce::Point<int> p, const std::vector<RowRef>& rws,
                          int& rowOut, int& kfOut) const
    {
        const int numRows = (int) rws.size();
        for (int r = 0; r < numRows; ++r)
        {
            const auto row = getRowBounds (r, numRows);
            const auto& fx = show.fixtures[(size_t) rws[(size_t) r].fixture];
            const auto& kfs = fx.channels[(size_t) rws[(size_t) r].channel].keyframes;
            for (int i = 0; i < (int) kfs.size(); ++i)
            {
                const float kx = beatToX (kfs[(size_t) i].timeBeats);
                const float ky = valueToY (kfs[(size_t) i].value, row);
                if (p.toFloat().getDistanceFrom ({ kx, ky }) <= 8.0f)
                {
                    rowOut = r; kfOut = i; return true;
                }
            }
        }
        return false;
    }

    void openColourPicker (int fi, int ch)
    {
        if (fi < 0 || fi >= (int) show.fixtures.size()) return;
        auto& f = show.fixtures[(size_t) fi];
        if (ch < 0 || ch >= (int) f.channels.size()) return;

        pushUndo();
        colourEditFixture = fi;
        colourEditChannel = ch;
        auto selector = std::make_unique<juce::ColourSelector> (
            juce::ColourSelector::showColourAtTop
            | juce::ColourSelector::showColourspace
            | juce::ColourSelector::editableColour);
        selector->setName ("Color del canal");
        selector->setCurrentColour (f.channels[(size_t) ch].colour, juce::dontSendNotification);
        selector->setSize (240, 280);
        selector->addChangeListener (this);
        const auto sa = getScreenBounds().withWidth (1).withHeight (1)
                            .translated (getMouseXYRelative().x, getMouseXYRelative().y);
        juce::CallOutBox::launchAsynchronously (std::move (selector), sa, nullptr);
    }

    //==============================================================================
    // Clips de efecto (LFO)

    juce::Rectangle<float> getClipBounds (int fi, int channelIndex, int clipIdx,
                                          int rowIndex, int numRows) const
    {
        if (fi < 0 || fi >= (int) show.fixtures.size()) return {};
        const auto& f = show.fixtures[(size_t) fi];
        if (channelIndex < 0 || channelIndex >= (int) f.channels.size()) return {};
        const auto& clips = f.channels[(size_t) channelIndex].clips;
        if (clipIdx < 0 || clipIdx >= (int) clips.size()) return {};

        const auto& cl = clips[(size_t) clipIdx];
        const auto row = getRowBounds (rowIndex, numRows).reduced (0, 4);
        const float x0 = beatToX (cl.startBeats);
        const float x1 = beatToX (cl.startBeats + cl.lengthBeats);
        return { x0, (float) row.getY(), juce::jmax (3.0f, x1 - x0), (float) row.getHeight() };
    }

    // edgeOut: 0=cuerpo, 1=borde izq, 2=borde der.
    bool hitTestClip (juce::Point<int> p, const std::vector<RowRef>& rws,
                      int& rowOut, int& clipOut, int& edgeOut) const
    {
        const int numRows = (int) rws.size();
        for (int r = 0; r < numRows; ++r)
        {
            const int fi = rws[(size_t) r].fixture;
            const int ch = rws[(size_t) r].channel;
            const auto& clips = show.fixtures[(size_t) fi].channels[(size_t) ch].clips;
            for (int i = 0; i < (int) clips.size(); ++i)
            {
                const auto b = getClipBounds (fi, ch, i, r, numRows);
                if (b.contains (p.toFloat()))
                {
                    rowOut = r; clipOut = i;
                    if (p.x - b.getX() <= 6.0f)            edgeOut = 1;
                    else if (b.getRight() - p.x <= 6.0f)   edgeOut = 2;
                    else                                   edgeOut = 0;
                    return true;
                }
            }
        }
        return false;
    }

    void openClipEditor (int fi, int channelIndex, int clipIdx)
    {
        if (fi < 0 || fi >= (int) show.fixtures.size()) return;
        auto& f = show.fixtures[(size_t) fi];
        if (channelIndex < 0 || channelIndex >= (int) f.channels.size()) return;
        auto& clips = f.channels[(size_t) channelIndex].clips;
        if (clipIdx < 0 || clipIdx >= (int) clips.size()) return;

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
                    if (fi < (int) show.fixtures.size())
                    {
                        auto& fx = show.fixtures[(size_t) fi];
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
                                markChanged();
                            }
                        }
                    }
                }
                repaint();
            }), true);
    }

    //==============================================================================
    // Menu desplegable de canal para una pista en modo multipista.
    void openChannelMenu (int fi)
    {
        if (fi < 0 || fi >= (int) show.fixtures.size()) return;
        const auto& f = show.fixtures[(size_t) fi];
        const int cur = (fi < (int) multiChannel.size()) ? multiChannel[(size_t) fi] : 0;

        juce::PopupMenu m;
        for (int c = 0; c < (int) f.channels.size(); ++c)
        {
            const auto& ch = f.channels[(size_t) c];
            const auto nm = ch.label.isNotEmpty() ? ch.label : channelTypeToString (ch.type);
            m.addItem (c + 1, juce::String (c + 1) + ". " + nm + "  (DMX " + juce::String (f.dmxAddressOf (c)) + ")",
                       true, c == cur);
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this, fi] (int r)
                         {
                             if (r > 0 && fi < (int) multiChannel.size())
                             {
                                 multiChannel[(size_t) fi] = r - 1;
                                 repaint();
                             }
                         });
    }

    int rowAtY (const std::vector<RowRef>& rws, float y) const
    {
        const int numRows = (int) rws.size();
        if (numRows <= 0) return -1;
        const auto tracks = getTracksBounds();
        const float rowH = (float) tracks.getHeight() / (float) numRows;
        return juce::jlimit (0, numRows - 1, (int) ((y - tracks.getY()) / rowH));
    }

    // Mueve el reproductor a la posicion (x) de la regla.
    void scrubTo (float x)
    {
        double beats = xToBeat (x);
        if (snapButton.getToggleState())
            beats = juce::jlimit (0.0, totalBeats(), std::round (beats));
        if (onSeekSeconds)
            onSeekSeconds (beats * 60.0 / currentBpm());
        repaint();
    }

    //==============================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto rws = rows();
        const int numRows = (int) rws.size();
        if (numRows == 0) return;

        // Clic en la regla (franja de compases): mueve el playhead (scrub).
        if (getRulerBounds().contains (e.getPosition()) && ! e.mods.isPopupMenu())
        {
            scrubbing = true;
            scrubTo (e.position.x);
            return;
        }

        // Multipista: clic en la etiqueta (izquierda) abre el droplist de canal.
        if (multiTrack && e.x < kLabelWidth && ! e.mods.isPopupMenu())
        {
            const int r = rowAtY (rws, e.position.y);
            if (r >= 0)
            {
                // No abrir el menu si se hizo clic justo en la muestra de color.
                if (! getSwatchBounds (r, numRows).contains (e.getPosition()))
                {
                    openChannelMenu (rws[(size_t) r].fixture);
                    return;
                }
            }
        }

        // --- Modo efectos: crear / mover / redimensionar / borrar clips LFO ---
        if (clipModeButton.getToggleState())
        {
            int hr = -1, hi = -1, edge = 0;
            if (hitTestClip (e.getPosition(), rws, hr, hi, edge))
            {
                const int cfi = rws[(size_t) hr].fixture;
                const int cch = rws[(size_t) hr].channel;
                if (e.mods.isPopupMenu())   // clic derecho borra
                {
                    pushUndo();
                    auto& clips = show.fixtures[(size_t) cfi].channels[(size_t) cch].clips;
                    clips.erase (clips.begin() + hi);
                    markChanged(); repaint();
                    return;
                }
                pushUndo();
                draggingClip = true;
                clipRow      = hr;
                clipFixture  = cfi;
                clipChannel  = cch;
                clipIndex    = hi;
                clipDragMode = edge;
                clipGrabOffset = xToBeat (e.position.x)
                               - show.fixtures[(size_t) cfi].channels[(size_t) cch].clips[(size_t) hi].startBeats;
                return;
            }

            if (e.mods.isPopupMenu()) return;

            const auto tracks = getTracksBounds();
            if (! tracks.contains (e.getPosition())) return;

            const int r = rowAtY (rws, e.position.y);
            const int cfi = rws[(size_t) r].fixture;
            const int cch = rws[(size_t) r].channel;

            double start = xToBeat (e.position.x);
            if (snapButton.getToggleState())
                start = juce::jlimit (0.0, totalBeats(), std::round (start));

            EffectClip clip;
            clip.startBeats  = start;
            clip.lengthBeats = juce::jmax (0.25, juce::jmin (1.0, totalBeats() - start));
            clip.type        = EffectType::Sine;
            clip.periodBeats = 1.0;
            clip.low         = 0.0f;
            clip.high        = 255.0f;

            pushUndo();
            auto& clips = show.fixtures[(size_t) cfi].channels[(size_t) cch].clips;
            clips.push_back (clip);

            draggingClip = true;
            clipRow      = r;
            clipFixture  = cfi;
            clipChannel  = cch;
            clipIndex    = (int) clips.size() - 1;
            clipDragMode = 2;   // redimensionar borde derecho al arrastrar
            markChanged(); repaint();
            return;
        }

        // Click en la muestra de color
        if (! e.mods.isPopupMenu())
            for (int r = 0; r < numRows; ++r)
                if (getSwatchBounds (r, numRows).contains (e.getPosition()))
                {
                    openColourPicker (rws[(size_t) r].fixture, rws[(size_t) r].channel);
                    return;
                }

        int hitRow = -1, hitKf = -1;
        const bool onKf = hitTestKeyframe (e.getPosition(), rws, hitRow, hitKf);

        // Click derecho sobre keyframe: alterna rampa/step.
        if (e.mods.isPopupMenu())
        {
            if (onKf)
            {
                pushUndo();
                auto& k = show.fixtures[(size_t) rws[(size_t) hitRow].fixture]
                              .channels[(size_t) rws[(size_t) hitRow].channel].keyframes[(size_t) hitKf];
                k.stepped = ! k.stepped;
                markChanged(); repaint();
            }
            return;
        }

        if (onKf)
        {
            pushUndo();
            dragging = true;
            dragFixture = rws[(size_t) hitRow].fixture;
            dragChannel = rws[(size_t) hitRow].channel;
            dragKeyframe = hitKf;
            return;
        }

        // Click en zona vacia: crear keyframe.
        const auto tracks = getTracksBounds();
        if (! tracks.contains (e.getPosition())) return;

        const int r = rowAtY (rws, e.position.y);
        const int cfi = rws[(size_t) r].fixture;
        const int cch = rws[(size_t) r].channel;
        const auto row = getRowBounds (r, numRows);

        Keyframe k;
        k.timeBeats = xToBeat (e.position.x);
        if (snapButton.getToggleState())
            k.timeBeats = juce::jlimit (0.0, totalBeats(), std::round (k.timeBeats));
        k.value = std::round (yToValue (e.position.y, row));

        pushUndo();
        auto& kfs = show.fixtures[(size_t) cfi].channels[(size_t) cch].keyframes;
        kfs.push_back (k);
        sortKeyframes (kfs);
        for (int i = 0; i < (int) kfs.size(); ++i)
            if (kfs[(size_t) i].timeBeats == k.timeBeats && kfs[(size_t) i].value == k.value)
            {
                dragKeyframe = i; break;
            }
        dragging = true; dragFixture = cfi; dragChannel = cch;
        markChanged(); repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Scrub en la regla.
        if (scrubbing)
        {
            scrubTo (e.position.x);
            return;
        }

        // Arrastre de un clip de efecto (mover o redimensionar).
        if (draggingClip)
        {
            if (clipFixture < 0 || clipFixture >= (int) show.fixtures.size()) return;
            auto& f = show.fixtures[(size_t) clipFixture];
            if (clipChannel < 0 || clipChannel >= (int) f.channels.size()) return;
            auto& clips = f.channels[(size_t) clipChannel].clips;
            if (clipIndex < 0 || clipIndex >= (int) clips.size()) return;

            auto& cl = clips[(size_t) clipIndex];
            double b = xToBeat (e.position.x);
            if (snapButton.getToggleState())
                b = std::round (b);

            if (clipDragMode == 0)        // mover
                cl.startBeats = juce::jlimit (0.0, juce::jmax (0.0, totalBeats() - cl.lengthBeats),
                                              b - clipGrabOffset);
            else if (clipDragMode == 2)   // redimensionar borde derecho
                cl.lengthBeats = juce::jlimit (0.25, totalBeats() - cl.startBeats, b - cl.startBeats);
            else                          // redimensionar borde izquierdo
            {
                const double end = cl.startBeats + cl.lengthBeats;
                const double ns  = juce::jlimit (0.0, end - 0.25, b);
                cl.startBeats  = ns;
                cl.lengthBeats = end - ns;
            }
            markChanged(); repaint();
            return;
        }

        if (! dragging || dragFixture < 0 || dragChannel < 0 || dragKeyframe < 0) return;
        if (dragFixture >= (int) show.fixtures.size()) return;
        auto& f = show.fixtures[(size_t) dragFixture];
        if (dragChannel >= (int) f.channels.size()) return;
        auto& kfs = f.channels[(size_t) dragChannel].keyframes;
        if (dragKeyframe >= (int) kfs.size()) return;

        // Fila visual de este keyframe (para mapear Y -> valor).
        const auto rws = rows();
        int dragRow = -1;
        for (int r = 0; r < (int) rws.size(); ++r)
            if (rws[(size_t) r].fixture == dragFixture && rws[(size_t) r].channel == dragChannel)
            { dragRow = r; break; }
        if (dragRow < 0) return;
        const auto row = getRowBounds (dragRow, (int) rws.size());

        double beats = xToBeat (e.position.x);
        if (snapButton.getToggleState())
            beats = juce::jlimit (0.0, totalBeats(), std::round (beats));
        kfs[(size_t) dragKeyframe].timeBeats = beats;
        kfs[(size_t) dragKeyframe].value     = std::round (yToValue (e.position.y, row));

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
        markChanged(); repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (scrubbing) { scrubbing = false; return; }
        if (draggingClip)
        {
            markChanged();
            draggingClip = false; clipRow = -1; clipFixture = -1; clipChannel = -1;
            clipIndex = -1; clipDragMode = 0;
            return;
        }
        if (dragging) markChanged();
        dragging = false; dragFixture = -1; dragChannel = -1; dragKeyframe = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto rws = rows();
        if (rws.empty()) return;

        if (clipModeButton.getToggleState())
        {
            int hr = -1, hi = -1, edge = 0;
            if (hitTestClip (e.getPosition(), rws, hr, hi, edge))
                openClipEditor (rws[(size_t) hr].fixture, rws[(size_t) hr].channel, hi);
            return;
        }

        int hitRow = -1, hitKf = -1;
        if (hitTestKeyframe (e.getPosition(), rws, hitRow, hitKf))
        {
            pushUndo();
            auto& kfs = show.fixtures[(size_t) rws[(size_t) hitRow].fixture]
                            .channels[(size_t) rws[(size_t) hitRow].channel].keyframes;
            kfs.erase (kfs.begin() + hitKf);
            markChanged(); repaint();
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        // Cursor de "mano" sobre la regla para indicar que se puede arrastrar.
        setMouseCursor (getRulerBounds().contains (e.getPosition())
                          ? juce::MouseCursor::PointingHandCursor
                          : juce::MouseCursor::NormalCursor);

        const auto rws = rows();
        const auto tracks = getTracksBounds();
        int newHover = -1;
        if (! rws.empty() && tracks.contains (e.getPosition()))
            newHover = rowAtY (rws, e.position.y);
        if (newHover != hoverRow) { hoverRow = newHover; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverRow != -1) { hoverRow = -1; repaint(); }
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (P::bg1));

        if (lastFixtureCount != (int) show.fixtures.size())
            refreshFixtures();

        const auto rws = rows();
        const int numRows = (int) rws.size();
        if (numRows == 0)
        {
            g.setColour (juce::Colour (P::textDim));
            g.setFont (juce::FontOptions (15.0f));
            g.drawText ("El show no tiene equipos.", body, juce::Justification::centred, true);
            return;
        }

        updateScrollBar();

        const auto tracks = getTracksBounds();
        const auto ruler  = getRulerBounds();
        const int  bpb    = beatsPerBar();
        const double totalB = totalBeats();
        const int  bars   = juce::jmax (1, (int) std::ceil (totalB / bpb));
        const juce::Colour accent { P::accent };

        g.setColour (juce::Colour (P::bg0));
        g.fillRect (ruler);
        g.fillRect (tracks);

        // Recorta a regla + pistas para que las curvas no invadan la columna de
        // etiquetas al hacer scroll/zoom.
        {
            juce::Graphics::ScopedSaveState clipState (g);
            g.reduceClipRegion (ruler.getUnion (tracks));

            // Rango de compases / beats visibles (culling): con zoom solo se
            // recorre y dibuja lo que cae dentro de la ventana, no todo el tema.
            const double visB     = visibleBeats();
            const double loBeatV  = scrollBeats;
            const double hiBeatV  = scrollBeats + visB;
            const int firstBar = juce::jmax (0, (int) std::floor (loBeatV / bpb));
            const int lastBar  = juce::jmin (bars, (int) std::ceil (hiBeatV / bpb) + 1);

            // Zebra de compases
            for (int bar = firstBar; bar < lastBar; ++bar)
                if (bar % 2 == 1)
                {
                    const float x0 = beatToX (bar * bpb);
                    const float x1 = beatToX ((bar + 1) * bpb);
                    g.setColour (juce::Colours::white.withAlpha (0.022f));
                    g.fillRect (x0, (float) tracks.getY(), x1 - x0, (float) tracks.getHeight());
                }

            // Pass A: fondos de fila, hover, guias de valor, separadores
            for (int r = 0; r < numRows; ++r)
            {
                const auto row = getRowBounds (r, numRows);
                const auto& chan = show.fixtures[(size_t) rws[(size_t) r].fixture]
                                       .channels[(size_t) rws[(size_t) r].channel];
                const auto chanCol = chan.colour;
                g.setColour ((r % 2 == 0) ? juce::Colour (0xff14171c) : juce::Colour (0xff111419));
                g.fillRect (row);
                g.setColour (chanCol.withAlpha (0.05f));
                g.fillRect (row);
                if (r == hoverRow && ! dragging)
                {
                    g.setColour (accent.withAlpha (0.05f));
                    g.fillRect (row);
                }
                auto valueArea = row.reduced (0, 6);
                for (int q = 1; q <= 3; ++q)
                {
                    const float y = (float) valueArea.getY() + (float) valueArea.getHeight() * (q / 4.0f);
                    g.setColour (juce::Colours::white.withAlpha (q == 2 ? 0.06f : 0.03f));
                    g.drawHorizontalLine ((int) y, (float) tracks.getX(), (float) tracks.getRight());
                }
                g.setColour (juce::Colour (P::lineSoft));
                g.drawHorizontalLine (row.getBottom() - 1, (float) tracks.getX(), (float) tracks.getRight());
            }

            // Grid vertical + numeros de compas (solo compases visibles)
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            for (int bar = firstBar; bar <= lastBar; ++bar)
            {
                if (bar < bars)
                    for (int beat = 1; beat < bpb; ++beat)
                    {
                        const float bx = beatToX (bar * bpb + beat);
                        g.setColour (juce::Colours::white.withAlpha (0.05f));
                        g.drawVerticalLine ((int) bx, (float) tracks.getY(), (float) tracks.getBottom());
                    }
                const float x = beatToX (bar * bpb);
                g.setColour (accent.withAlpha (0.22f));
                g.drawVerticalLine ((int) x, (float) ruler.getY(), (float) tracks.getBottom());
                if (bar < bars)
                {
                    g.setColour (juce::Colour (P::textMid));
                    g.drawText (juce::String (bar + 1), (int) x + 5, ruler.getY(), 30, ruler.getHeight(),
                                juce::Justification::centredLeft, false);
                }
            }

            // Pass B: curvas + keyframes (solo el tramo visible de cada fila)
            const double loBeatK = loBeatV - 0.0001;
            const double hiBeatK = hiBeatV + 0.0001;
            for (int r = 0; r < numRows; ++r)
            {
                const auto row = getRowBounds (r, numRows);
                const auto& chan = show.fixtures[(size_t) rws[(size_t) r].fixture]
                                       .channels[(size_t) rws[(size_t) r].channel];
                const auto& kfs = chan.keyframes;
                if (kfs.empty()) continue;
                const juce::Colour chanCol = chan.colour;

                // Indices de keyframes a dibujar: rango visible + 1 keyframe a cada
                // lado para que la curva entre/salga bien por los bordes.
                const int nkf = (int) kfs.size();
                int loIdx = 0, hiIdx = nkf - 1;
                for (int i = 0; i < nkf; ++i)
                    if (kfs[(size_t) i].timeBeats <= loBeatK) loIdx = i; else break;
                for (int i = nkf - 1; i >= 0; --i)
                    if (kfs[(size_t) i].timeBeats >= hiBeatK) hiIdx = i; else break;

                juce::Path path;
                bool started = false;
                float prevY = 0.0f;
                for (int i = loIdx; i <= hiIdx; ++i)
                {
                    const float kx = beatToX (kfs[(size_t) i].timeBeats);
                    const float ky = valueToY (kfs[(size_t) i].value, row);
                    if (! started) { path.startNewSubPath (kx, ky); started = true; }
                    else
                    {
                        if (kfs[(size_t) i - 1].stepped) path.lineTo (kx, prevY);
                        path.lineTo (kx, ky);
                    }
                    prevY = ky;
                }
                if (started)
                {
                    juce::Path fill = path;
                    fill.lineTo (beatToX (kfs[(size_t) hiIdx].timeBeats), (float) row.getBottom());
                    fill.lineTo (beatToX (kfs[(size_t) loIdx].timeBeats), (float) row.getBottom());
                    fill.closeSubPath();
                    g.setColour (chanCol.withAlpha (0.10f));
                    g.fillPath (fill);

                    g.setColour (chanCol);
                    g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                              juce::PathStrokeType::rounded));
                }
                for (int i = loIdx; i <= hiIdx; ++i)
                {
                    const float kx = beatToX (kfs[(size_t) i].timeBeats);
                    const float ky = valueToY (kfs[(size_t) i].value, row);
                    const bool dr = (dragging && dragFixture == rws[(size_t) r].fixture
                                     && dragChannel == rws[(size_t) r].channel && dragKeyframe == i);
                    const auto col = kfs[(size_t) i].stepped ? accent : chanCol;
                    g.setColour (col.withAlpha (dr ? 0.5f : 0.25f));
                    g.fillEllipse (kx - 7.0f, ky - 7.0f, 14.0f, 14.0f);
                    g.setColour (dr ? juce::Colours::white : col);
                    g.fillEllipse (kx - 4.0f, ky - 4.0f, 8.0f, 8.0f);
                    g.setColour (juce::Colour (P::bg0));
                    g.drawEllipse (kx - 4.0f, ky - 4.0f, 8.0f, 8.0f, 1.2f);
                }
            }

            // Pass C: clips de efecto (LFO) encima de las curvas
            for (int r = 0; r < numRows; ++r)
            {
                const int cfi = rws[(size_t) r].fixture;
                const int cch = rws[(size_t) r].channel;
                const auto& chan = show.fixtures[(size_t) cfi].channels[(size_t) cch];
                const juce::Colour col = chan.colour;
                for (int i = 0; i < (int) chan.clips.size(); ++i)
                {
                    const auto& cl = chan.clips[(size_t) i];
                    const auto b = getClipBounds (cfi, cch, i, r, numRows);
                    const bool active = (draggingClip && clipFixture == cfi && clipChannel == cch && clipIndex == i);

                    g.setColour (col.withAlpha (active ? 0.22f : 0.14f));
                    g.fillRoundedRectangle (b, 4.0f);

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

                    g.setColour (col.withAlpha (active ? 0.95f : 0.6f));
                    g.drawRoundedRectangle (b, 4.0f, active ? 1.8f : 1.0f);

                    g.setColour (col.withAlpha (0.85f));
                    g.fillRect (b.getX(), b.getY(), 2.5f, b.getHeight());
                    g.fillRect (b.getRight() - 2.5f, b.getY(), 2.5f, b.getHeight());

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
        } // fin del recorte

        // Etiquetas (columna izquierda) - fuera del recorte
        const auto chevron = juce::String::fromUTF8 ("  \xE2\x96\xBE");   // triangulo abajo (droplist)
        for (int r = 0; r < numRows; ++r)
        {
            const int cfi = rws[(size_t) r].fixture;
            const int cch = rws[(size_t) r].channel;
            const auto& fx = show.fixtures[(size_t) cfi];
            const auto& chan = fx.channels[(size_t) cch];
            const auto row = getRowBounds (r, numRows);
            juce::Rectangle<int> labelArea (body.getX(), row.getY(), kLabelWidth - 4, row.getHeight());
            g.setColour ((r == hoverRow && ! dragging) ? juce::Colour (0xff20242c)
                                                       : juce::Colour (0xff1a1d23));
            g.fillRect (labelArea);
            g.setColour (juce::Colour (P::lineSoft));
            g.drawHorizontalLine (labelArea.getBottom() - 1, (float) labelArea.getX(), (float) labelArea.getRight());

            const auto sw = getSwatchBounds (r, numRows);
            g.setColour (chan.colour);
            g.fillRoundedRectangle (sw.toFloat(), 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawRoundedRectangle (sw.toFloat(), 3.0f, 1.0f);

            auto textArea = labelArea.withTrimmedLeft (sw.getRight() - labelArea.getX() + 6);
            const auto chName = chan.label.isNotEmpty() ? chan.label : channelTypeToString (chan.type);

            if (multiTrack)
            {
                // Equipo arriba + canal (droplist) abajo.
                g.setColour (juce::Colour (P::textHi));
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (juce::String (cfi + 1) + ". " + fx.name,
                            textArea.removeFromTop (row.getHeight() / 2),
                            juce::Justification::bottomLeft, true);
                g.setColour (accent);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (chName + chevron, textArea, juce::Justification::topLeft, true);
            }
            else
            {
                g.setColour (accent);
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText (juce::String (cch + 1) + ". " + chName,
                            textArea, juce::Justification::centredLeft, true);
                g.setColour (juce::Colour (P::textDim));
                g.setFont (juce::FontOptions (10.0f));
                g.drawText ("DMX " + juce::String (fx.dmxAddressOf (cch)),
                            textArea.reduced (0, 2), juce::Justification::bottomLeft, true);
            }
        }

        g.setColour (juce::Colour (P::line));
        g.drawVerticalLine (tracks.getX() - 1, (float) ruler.getY(), (float) tracks.getBottom());

        // Playhead (posicion del reproductor) - recortado a las pistas
        if (getPlayheadSeconds)
        {
            juce::Graphics::ScopedSaveState clipState (g);
            g.reduceClipRegion (ruler.getUnion (tracks));
            const double secs = getPlayheadSeconds();
            if (secs >= 0.0)
            {
                const double ppq = secs * currentBpm() / 60.0;
                if (ppq >= 0.0 && ppq <= totalB)
                {
                    const float px = beatToX (ppq);
                    const bool playing = isPlaying && isPlaying();
                    const juce::Colour ph = playing ? juce::Colour (0xff62d96b) : juce::Colour (0xffb0b6c0);
                    g.setColour (ph.withAlpha (0.25f));
                    g.fillRect (px - 1.5f, (float) ruler.getY(), 3.0f, (float) (tracks.getBottom() - ruler.getY()));
                    g.setColour (ph);
                    g.fillRect (px - 0.75f, (float) ruler.getY(), 1.5f, (float) (tracks.getBottom() - ruler.getY()));
                    juce::Path head;
                    head.addTriangle (px - 5.0f, (float) ruler.getY(),
                                      px + 5.0f, (float) ruler.getY(),
                                      px,        (float) ruler.getY() + 7.0f);
                    g.setColour (ph);
                    g.fillPath (head);
                }
            }
        }
    }

    //==============================================================================
    DmxShow& show;

    juce::Label    title, fixtureLabel, bpmLabel, beatsLabel, hint;
    juce::ComboBox fixtureCombo;
    juce::Slider   bpmSlider, beatsSlider;
    juce::ToggleButton snapButton;
    juce::TextButton   multiTrackButton;
    juce::TextButton   undoButton;
    juce::ScrollBar    hScroll { false };

    juce::Rectangle<int> body;   // zona del piano-roll (regla + pistas + etiquetas)
    int  lastFixtureCount = -1;
    int  hoverRow = -1;
    int  colourEditFixture = -1;
    int  colourEditChannel = -1;

    // Multipista: una pista por equipo; canal mostrado por equipo.
    bool             multiTrack = false;
    std::vector<int> multiChannel;

    // Zoom / scroll horizontal (1 = todo visible).
    double zoom = 1.0;
    double scrollBeats = 0.0;

    // Historial de deshacer (snapshots del show).
    std::vector<juce::ValueTree> undoStack;

    bool dragging = false;
    int  dragFixture = -1;
    int  dragChannel = -1;
    int  dragKeyframe = -1;

    bool scrubbing = false;     // arrastrando en la regla para mover el playhead

    // Estado de arrastre de clips de efecto
    juce::ToggleButton clipModeButton;
    bool   draggingClip = false;
    int    clipRow      = -1;
    int    clipFixture  = -1;
    int    clipChannel  = -1;
    int    clipIndex    = -1;
    int    clipDragMode = 0;        // 0=mover, 1=redimensionar izq, 2=redimensionar der
    double clipGrabOffset = 0.0;    // offset (beats) entre el cursor y el inicio del clip al mover

    static constexpr int kLabelWidth     = 130;
    static constexpr int kRulerHeight    = 22;
    static constexpr int kScrollBarHeight = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ManualShowEditor)
};
