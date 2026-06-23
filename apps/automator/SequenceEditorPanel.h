#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../../source/FixtureModel.h"
#include "../../source/LuxLookAndFeel.h"
#include "ChoreographyEngine.h"
#include "PlaylistManager.h"
#include "DmxShow.h"

/**
    Pantalla "Creador": editor visual interactivo de coreografias MANUALES.

    Eliges una fixtura (p.ej. la barra LED de 22 secciones), ves sus segmentos y
    haces clic para encender/apagar cada uno con color e intensidad. Cada "estado"
    (que segmentos estan encendidos y de que color) dura un tiempo; encadenando
    estados de izquierda a derecha montas una secuencia que se reproduce en bucle.

    "Probar" recorre la secuencia en vivo (simulada aqui + DMX real si hay salida).
    Puedes fijar el canal/universo de inicio para probar en otra fixtura. Al
    guardar, la secuencia entra en la libreria como coreografia custom (manual)
    para asignarla por cancion.
*/
class SequenceEditorPanel : public juce::Component,
                            private juce::Timer
{
public:
    explicit SequenceEditorPanel (PlaylistManager& pm) : playlist (pm)
    {
        using P = LuxLookAndFeel::Palette;

        title.setText ("Creador de coreografias", juce::dontSendNotification);
        title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        addAndMakeVisible (title);

        hint.setText ("Fila Color (RGB) + fila Blanco: clic enciende/apaga/recolorea con el pincel (en Blanco solo cuenta la intensidad). Encadena estados para la secuencia.",
                      juce::dontSendNotification);
        hint.setFont (juce::FontOptions (12.0f));
        hint.setColour (juce::Label::textColourId, juce::Colour (P::textDim));
        addAndMakeVisible (hint);

        auto label = [this] (juce::Label& l, const juce::String& t)
        {
            l.setText (t, juce::dontSendNotification);
            l.setFont (juce::FontOptions (12.0f));
            l.setColour (juce::Label::textColourId, juce::Colour (LuxLookAndFeel::Palette::textMid));
            addAndMakeVisible (l);
        };

        // --- Fila superior: fixtura + cargar + guardar ---
        label (fixtureLabel, "Disenar para");
        fixtureCombo.onChange = [this] { onFixtureChanged(); };
        addAndMakeVisible (fixtureCombo);

        label (loadLabel, "Cargar");
        loadCombo.setTextWhenNothingSelected ("(nueva)");
        loadCombo.onChange = [this] { onLoadSelected(); };
        addAndMakeVisible (loadCombo);

        label (nameLabel, "Nombre");
        nameEditor.setText ("Mi secuencia", juce::dontSendNotification);
        addAndMakeVisible (nameEditor);

        label (targetLabel, "Aplicar a");
        addAndMakeVisible (targetCombo);

        newButton.setButtonText ("Nueva");
        newButton.onClick = [this] { startNew(); };
        addAndMakeVisible (newButton);

        saveButton.setButtonText ("Guardar coreografia");
        saveButton.onClick = [this] { saveToLibrary(); };
        addAndMakeVisible (saveButton);

        // --- Pincel: color + intensidad ---
        label (brushLabel, "Pincel");
        brushSwatch.setButtonText ("");
        brushSwatch.onClick = [this] { pickBrushColour(); };
        addAndMakeVisible (brushSwatch);
        updateBrushSwatch();

        // Colores rapidos (paleta fija) para no abrir el selector cada vez.
        for (auto c : presetColours)
        {
            auto* b = quickColourButtons.add (new juce::TextButton());
            b->setButtonText ("");
            b->setColour (juce::TextButton::buttonColourId, c);
            b->setColour (juce::TextButton::buttonOnColourId, c);
            b->onClick = [this, c] { setBrushColour (c); };
            addAndMakeVisible (b);
        }

        label (intensityLabel, "Intensidad");
        intensitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
        intensitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 22);
        intensitySlider.setRange (0.0, 100.0, 1.0);
        intensitySlider.setValue (100.0, juce::dontSendNotification);
        intensitySlider.setTextValueSuffix (" %");
        addAndMakeVisible (intensitySlider);

        allOnButton.setButtonText ("Todo");
        allOnButton.onClick = [this] { setAllSegments (true); };
        addAndMakeVisible (allOnButton);

        allOffButton.setButtonText ("Nada");
        allOffButton.onClick = [this] { setAllSegments (false); };
        addAndMakeVisible (allOffButton);

        // --- Estados (timeline) ---
        addStepButton.setButtonText ("+ Estado");
        addStepButton.onClick = [this] { addStep (false); };
        addAndMakeVisible (addStepButton);

        dupStepButton.setButtonText ("Duplicar");
        dupStepButton.onClick = [this] { addStep (true); };
        addAndMakeVisible (dupStepButton);

        delStepButton.setButtonText ("Borrar");
        delStepButton.onClick = [this] { deleteStep(); };
        addAndMakeVisible (delStepButton);

        unitButton.setButtonText ("Beats");
        unitButton.onClick = [this] { toggleUnit(); };
        addAndMakeVisible (unitButton);

        label (durationLabel, "Duracion");
        durationSlider.setSliderStyle (juce::Slider::IncDecButtons);
        durationSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 64, 22);
        durationSlider.setRange (0.25, 64.0, 0.25);
        durationSlider.setValue (1.0, juce::dontSendNotification);
        durationSlider.onValueChange = [this] { onDurationChanged(); };
        addAndMakeVisible (durationSlider);

        // --- Prueba ---
        testButton.setButtonText ("Probar");
        testButton.setClickingTogglesState (true);
        testButton.onClick = [this] { toggleTest(); };
        addAndMakeVisible (testButton);

        label (bpmLabel, "BPM");
        bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
        bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 22);
        bpmSlider.setRange (40.0, 240.0, 1.0);
        bpmSlider.setValue (120.0, juce::dontSendNotification);
        addAndMakeVisible (bpmSlider);

        label (startChanLabel, "Canal inicio");
        startChanSlider.setSliderStyle (juce::Slider::IncDecButtons);
        startChanSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 22);
        startChanSlider.setRange (1.0, 512.0, 1.0);
        startChanSlider.setValue (1.0, juce::dontSendNotification);
        addAndMakeVisible (startChanSlider);

        label (universeLabel, "Universo");
        universeSlider.setSliderStyle (juce::Slider::IncDecButtons);
        universeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 22);
        universeSlider.setRange (0.0, 7.0, 1.0);
        universeSlider.setValue (0.0, juce::dontSendNotification);
        addAndMakeVisible (universeSlider);

        refreshFromPlaylist();
        startNew();
    }

    ~SequenceEditorPanel() override { stopTimer(); }

    /** Refresca combos cuando cambia el rig / la libreria por fuera. */
    void refreshFromPlaylist()
    {
        const auto& rig = playlist.getRig();

        const int prevFixture = fixtureCombo.getSelectedId();
        fixtureCombo.clear (juce::dontSendNotification);
        targetCombo.clear (juce::dontSendNotification);
        designFixtures.clear();
        designInRig.clear();

        // "Aplicar a": la propia fixtura (por nombre) o todas. La coreografia se
        // guarda PARA la fixtura; no se toca el rig.
        targetCombo.addItem ("Solo esta fixtura", 1);
        targetCombo.addItem ("Todas las fixturas", 2);

        int id = 1;

        // 1) Fixturas que YA estan en el rig (el escenario del usuario).
        for (const auto& f : rig)
        {
            juce::String lbl = f.name;
            if (f.universe > 0 || f.startAddress > 0)
                lbl << "  (U" << f.universe << ":" << f.startAddress << ")";
            fixtureCombo.addItem (lbl, id);
            designFixtures.push_back (f);
            designInRig.push_back (true);
            ++id;
        }

        // 2) Equipos de la LIBRERIA que aun no estan en el rig. Puedes disenar para
        //    ellos; la secuencia se guarda para ese tipo de equipo. Si luego lo
        //    anades al escenario, ya tendra su coreografia custom.
        for (const auto& templ : playlist.getCustomFixtures())
        {
            bool already = false;
            for (const auto& f : rig)
                if (ChoreographyEngine::fixtureBaseName (f.name)
                        .equalsIgnoreCase (ChoreographyEngine::fixtureBaseName (templ.name)))
                { already = true; break; }
            if (already) continue;

            fixtureCombo.addItem (templ.name + "   · libreria", id);
            designFixtures.push_back (templ);
            designInRig.push_back (false);
            ++id;
        }

        const int total = (int) designFixtures.size();
        if (prevFixture > 0 && prevFixture <= total)
            fixtureCombo.setSelectedId (prevFixture, juce::dontSendNotification);
        else if (total > 0)
            fixtureCombo.setSelectedId (bestPixelDesignId(), juce::dontSendNotification);

        rebuildLoadCombo();
        onFixtureChanged();
    }

    /** Id (1-based) de la opcion con mas segmentos de pixel (la barra LED), para
        seleccionarla por defecto en vez del primer PAR. */
    int bestPixelDesignId() const
    {
        int bestId = 1, bestSegs = -1;
        for (int i = 0; i < (int) designFixtures.size(); ++i)
        {
            const auto layout = ChoreographyEngine::detectPixelLayout (designFixtures[(size_t) i]);
            const int segs = (int) juce::jmax (layout.rgb.size(), layout.white.size());
            if (segs > bestSegs) { bestSegs = segs; bestId = i + 1; }
        }
        return bestId;
    }

    bool isTesting() const noexcept { return testing; }

    /** Construye el frame DMX del estado actual de la prueba (para la salida). */
    std::vector<DmxShow::Universe> buildCurrentFrame() const
    {
        return buildFrame (previewStep());
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        using P = LuxLookAndFeel::Palette;
        g.fillAll (juce::Colour (P::bg0));

        drawSegmentGrid (g);
        drawStepsStrip (g);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        title.setBounds (r.removeFromTop (26));
        hint.setBounds (r.removeFromTop (20));
        r.removeFromTop (8);

        // Fila 1: fixtura + cargar + nombre + aplicar a.
        auto row1 = r.removeFromTop (26);
        fixtureLabel.setBounds (row1.removeFromLeft (84));
        fixtureCombo.setBounds (row1.removeFromLeft (200));
        row1.removeFromLeft (10);
        loadLabel.setBounds (row1.removeFromLeft (50));
        loadCombo.setBounds (row1.removeFromLeft (170));
        r.removeFromTop (8);

        auto row2 = r.removeFromTop (26);
        nameLabel.setBounds (row2.removeFromLeft (60));
        nameEditor.setBounds (row2.removeFromLeft (200));
        row2.removeFromLeft (10);
        targetLabel.setBounds (row2.removeFromLeft (62));
        targetCombo.setBounds (row2.removeFromLeft (200));
        row2.removeFromLeft (10);
        saveButton.setBounds (row2.removeFromRight (170));
        row2.removeFromRight (8);
        newButton.setBounds (row2.removeFromRight (80));
        r.removeFromTop (12);

        // Fila pincel.
        auto brushRow = r.removeFromTop (28);
        brushLabel.setBounds (brushRow.removeFromLeft (50));
        brushSwatch.setBounds (brushRow.removeFromLeft (54).reduced (0, 2));
        brushRow.removeFromLeft (10);
        intensityLabel.setBounds (brushRow.removeFromLeft (72));
        intensitySlider.setBounds (brushRow.removeFromLeft (220));
        brushRow.removeFromLeft (16);
        allOnButton.setBounds (brushRow.removeFromLeft (60));
        brushRow.removeFromLeft (6);
        allOffButton.setBounds (brushRow.removeFromLeft (60));
        r.removeFromTop (6);

        // Fila de colores rapidos (paleta fija).
        auto quickRow = r.removeFromTop (24);
        quickRow.removeFromLeft (50);   // alinea bajo el swatch del pincel
        for (auto* b : quickColourButtons)
        {
            b->setBounds (quickRow.removeFromLeft (26).reduced (0, 2));
            quickRow.removeFromLeft (4);
        }
        r.removeFromTop (10);

        // Zona de la rejilla de segmentos (reserva el resto menos la franja de estados).
        auto bottom = r.removeFromBottom (150);
        gridArea = r.reduced (2, 6);

        // Controles de estado (encima de la franja de estados).
        auto stepCtrl = bottom.removeFromTop (28);
        addStepButton.setBounds (stepCtrl.removeFromLeft (90));
        stepCtrl.removeFromLeft (6);
        dupStepButton.setBounds (stepCtrl.removeFromLeft (84));
        stepCtrl.removeFromLeft (6);
        delStepButton.setBounds (stepCtrl.removeFromLeft (74));
        stepCtrl.removeFromLeft (16);
        durationLabel.setBounds (stepCtrl.removeFromLeft (66));
        durationSlider.setBounds (stepCtrl.removeFromLeft (130));
        stepCtrl.removeFromLeft (8);
        unitButton.setBounds (stepCtrl.removeFromLeft (84));
        bottom.removeFromTop (6);

        // Franja de estados.
        stripArea = bottom.removeFromTop (60);
        bottom.removeFromTop (6);

        // Fila de prueba.
        auto testRow = bottom.removeFromTop (28);
        testButton.setBounds (testRow.removeFromLeft (90));
        testRow.removeFromLeft (12);
        bpmLabel.setBounds (testRow.removeFromLeft (34));
        bpmSlider.setBounds (testRow.removeFromLeft (110));
        testRow.removeFromLeft (10);
        startChanLabel.setBounds (testRow.removeFromLeft (80));
        startChanSlider.setBounds (testRow.removeFromLeft (110));
        testRow.removeFromLeft (10);
        universeLabel.setBounds (testRow.removeFromLeft (60));
        universeSlider.setBounds (testRow.removeFromLeft (96));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Clic en un segmento de la rejilla (fila 0 = color, fila 1 = blanco).
        const auto hit = segmentAt (e.position);
        if (hit.seg >= 0)
        {
            // Modo pincel: decide pintar o borrar segun el primer segmento, y aplica
            // esa misma accion a todos los segmentos que se arrastren (barrido).
            painting   = true;
            paintRow   = hit.row;
            paintErase = segmentIsSet (hit.row, hit.seg);
            applyPaint (hit.row, hit.seg);
            repaint();
            return;
        }
        // Clic en un estado de la franja.
        const int st = stepAtX (e.position);
        if (st >= 0)
        {
            selectStep (st);
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! painting)
            return;
        const auto hit = segmentAt (e.position);
        if (hit.seg >= 0 && hit.row == paintRow)
        {
            applyPaint (hit.row, hit.seg);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        painting = false;
        paintRow = -1;
    }

private:
    using Seq = ChoreographyEngine::ManualSequence;
    using Step = ChoreographyEngine::SeqStep;
    using Seg  = ChoreographyEngine::SegState;

    //==============================================================================
    int numSegments() const { return juce::jmax (1, seq.numSegments); }

    Step& curStep()
    {
        if (seq.steps.empty()) seq.steps.push_back (makeBlankStep());
        selectedStep = juce::jlimit (0, (int) seq.steps.size() - 1, selectedStep);
        return seq.steps[(size_t) selectedStep];
    }

    Step makeBlankStep() const
    {
        Step s;
        s.duration = seq.useBeats ? 1.0 : 0.5;
        s.segments.assign ((size_t) numSegments(), Seg{});
        return s;
    }

    void ensureSegmentCount()
    {
        for (auto& st : seq.steps)
            st.segments.resize ((size_t) numSegments());
        if (seq.steps.empty())
            seq.steps.push_back (makeBlankStep());
    }

    //==============================================================================
    void startNew()
    {
        loadedChoreoIndex = -1;
        loadCombo.setSelectedId (0, juce::dontSendNotification);
        seq = Seq{};
        seq.numSegments = detectSegmentCount();
        refreshLayoutInfo();
        seq.steps.clear();
        seq.steps.push_back (makeBlankStep());
        selectedStep = 0;
        nameEditor.setText ("Mi secuencia", juce::dontSendNotification);
        unitButton.setButtonText (seq.useBeats ? "Beats" : "Segundos");
        durationSlider.setValue (curStep().duration, juce::dontSendNotification);
        repaint();
    }

    int detectSegmentCount() const
    {
        const auto* f = currentFixture();
        if (f == nullptr) return 22;
        const auto layout = ChoreographyEngine::detectPixelLayout (*f);
        const int n = (int) layout.rgb.size();
        return n > 0 ? n : 1;
    }

    const Fixture* currentFixture() const
    {
        const int idx = fixtureCombo.getSelectedId() - 1;
        if (idx >= 0 && idx < (int) designFixtures.size())
            return &designFixtures[(size_t) idx];
        return nullptr;
    }

    /** True si la fixtura elegida para disenar aun NO esta en el rig (es de la
        libreria); la coreografia se guarda igual para ese tipo de equipo. */
    bool currentIsLibraryOnly() const
    {
        const int idx = fixtureCombo.getSelectedId() - 1;
        return idx >= 0 && idx < (int) designInRig.size() && ! designInRig[(size_t) idx];
    }

    void onFixtureChanged()
    {
        seq.numSegments = detectSegmentCount();
        refreshLayoutInfo();
        ensureSegmentCount();

        // Por defecto la secuencia se aplica solo a la fixtura elegida.
        targetCombo.setSelectedId (1, juce::dontSendNotification);

        if (const auto* f = currentFixture())
        {
            startChanSlider.setValue (f->startAddress > 0 ? f->startAddress : 1, juce::dontSendNotification);
            universeSlider.setValue (f->universe, juce::dontSendNotification);
        }
        repaint();
    }

    void refreshLayoutInfo()
    {
        numWhite = 0;
        fixtureHasWhite = false;
        if (const auto* f = currentFixture())
        {
            const auto layout = ChoreographyEngine::detectPixelLayout (*f);
            numWhite = (int) layout.white.size();
            fixtureHasWhite = numWhite > 0;
        }
    }

    int gridRows() const { return fixtureHasWhite ? 2 : 1; }

    void rebuildLoadCombo()
    {
        loadCombo.clear (juce::dontSendNotification);
        loadIndices.clear();
        const auto& lib = playlist.getChoreoLibrary();
        int id = 1;
        for (int i = 0; i < (int) lib.size(); ++i)
            if (lib[(size_t) i].manual)
            {
                loadCombo.addItem (lib[(size_t) i].name, id++);
                loadIndices.add (i);
            }
    }

    void onLoadSelected()
    {
        const int sel = loadCombo.getSelectedId() - 1;
        if (sel < 0 || sel >= loadIndices.size())
            return;
        const int libIdx = loadIndices[sel];
        const auto& c = playlist.getChoreoLibrary()[(size_t) libIdx];
        loadedChoreoIndex = libIdx;
        seq = c.sequence;
        if (seq.steps.empty()) seq.steps.push_back (makeBlankStep());
        selectedStep = 0;
        nameEditor.setText (c.name, juce::dontSendNotification);

        // Selecciona el target guardado: vacio = Todas (id 2), si no = Solo esta (id 1).
        targetCombo.setSelectedId (c.targetKey.isEmpty() ? 2 : 1, juce::dontSendNotification);

        unitButton.setButtonText (seq.useBeats ? "Beats" : "Segundos");
        durationSlider.setValue (curStep().duration, juce::dontSendNotification);
        refreshLayoutInfo();
        ensureSegmentCount();
        repaint();
    }

    //==============================================================================
    void updateBrushSwatch()
    {
        brushSwatch.setColour (juce::TextButton::buttonColourId, brushColour);
        brushSwatch.repaint();
    }

    void setBrushColour (juce::Colour c)
    {
        brushColour = c;
        updateBrushSwatch();
        repaint();
    }

    void pickBrushColour()
    {
        auto sel = std::make_unique<juce::ColourSelector> (
            juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                | juce::ColourSelector::showColourspace);
        sel->setCurrentColour (brushColour);
        sel->setSize (260, 280);
        sel->addChangeListener (brushListener.get());
        juce::CallOutBox::launchAsynchronously (std::move (sel), brushSwatch.getScreenBounds(), nullptr);
    }

    /** True si el segmento ya esta "puesto" con los ajustes actuales del pincel.
        Sirve para decidir, al iniciar un barrido, si se pinta o se borra. */
    bool segmentIsSet (int row, int seg)
    {
        auto& st = curStep();
        if (seg < 0 || seg >= (int) st.segments.size())
            return false;
        auto& sg = st.segments[(size_t) seg];
        const float bi = (float) (intensitySlider.getValue() / 100.0);

        if (row == 1)
            return sg.whiteOn && std::abs (sg.white - bi) <= 0.01f;

        const juce::uint8 br = brushColour.getRed(), bg = brushColour.getGreen(), bb = brushColour.getBlue();
        return sg.on && sg.r == br && sg.g == bg && sg.b == bb && std::abs (sg.intensity - bi) <= 0.01f;
    }

    /** Pinta (paintErase=false) o borra (true) un segmento con los ajustes del
        pincel. Idempotente: repasar el mismo segmento durante el barrido no alterna. */
    void applyPaint (int row, int seg)
    {
        auto& st = curStep();
        if (seg < 0 || seg >= (int) st.segments.size())
            return;
        auto& sg = st.segments[(size_t) seg];
        const float bi = (float) (intensitySlider.getValue() / 100.0);

        if (row == 1)   // capa BLANCO
        {
            if (paintErase) { sg.whiteOn = false; }
            else            { sg.whiteOn = true; sg.white = bi; }
            return;
        }

        // capa COLOR (RGB).
        if (paintErase)
        {
            sg.on = false;
        }
        else
        {
            sg.on = true;
            sg.r = brushColour.getRed();
            sg.g = brushColour.getGreen();
            sg.b = brushColour.getBlue();
            sg.intensity = bi;
        }
    }

    void setAllSegments (bool on)
    {
        auto& st = curStep();
        const juce::uint8 br = brushColour.getRed(), bg = brushColour.getGreen(), bb = brushColour.getBlue();
        const float bi = (float) (intensitySlider.getValue() / 100.0);
        for (auto& sg : st.segments)
        {
            sg.on = on;
            if (on) { sg.r = br; sg.g = bg; sg.b = bb; sg.intensity = bi; }
        }
        repaint();
    }

    //==============================================================================
    void addStep (bool duplicate)
    {
        Step s = duplicate && ! seq.steps.empty() ? curStep() : makeBlankStep();
        s.segments.resize ((size_t) numSegments());
        seq.steps.insert (seq.steps.begin() + selectedStep + 1, std::move (s));
        selectedStep++;
        durationSlider.setValue (curStep().duration, juce::dontSendNotification);
        repaint();
    }

    void deleteStep()
    {
        if (seq.steps.size() <= 1)
            return;
        seq.steps.erase (seq.steps.begin() + selectedStep);
        selectedStep = juce::jmax (0, selectedStep - 1);
        durationSlider.setValue (curStep().duration, juce::dontSendNotification);
        repaint();
    }

    void selectStep (int i)
    {
        selectedStep = juce::jlimit (0, (int) seq.steps.size() - 1, i);
        durationSlider.setValue (curStep().duration, juce::dontSendNotification);
    }

    void onDurationChanged()
    {
        curStep().duration = durationSlider.getValue();
        repaint();
    }

    void toggleUnit()
    {
        seq.useBeats = ! seq.useBeats;
        unitButton.setButtonText (seq.useBeats ? "Beats" : "Segundos");
        repaint();
    }

    //==============================================================================
    void toggleTest()
    {
        testing = testButton.getToggleState();
        testButton.setButtonText (testing ? "Probando" : "Probar");
        if (testing)
        {
            testStartMs = juce::Time::getMillisecondCounterHiRes();
            startTimerHz (44);
        }
        else
        {
            stopTimer();
            repaint();
        }
    }

    void timerCallback() override
    {
        repaint();   // anima la rejilla; AutomatorComponent emite el frame por DMX.
    }

    double testElapsedSec() const
    {
        return (juce::Time::getMillisecondCounterHiRes() - testStartMs) / 1000.0;
    }

    /** Indice del estado a mostrar/emitir: el activo segun el reloj si se prueba,
        o el seleccionado si se edita. */
    int previewStep() const
    {
        if (! testing || seq.steps.empty())
            return juce::jlimit (0, juce::jmax (0, (int) seq.steps.size() - 1), selectedStep);

        const double bpm = juce::jmax (1.0, bpmSlider.getValue());
        double cycle = 0.0;
        std::vector<double> secs (seq.steps.size(), 0.0);
        for (size_t i = 0; i < seq.steps.size(); ++i)
        {
            double d = juce::jmax (0.0, seq.steps[i].duration);
            if (seq.useBeats) d *= 60.0 / bpm;       // beats -> segundos
            secs[i] = juce::jmax (1.0e-3, d);
            cycle  += secs[i];
        }
        if (cycle <= 0.0) return 0;
        double m = std::fmod (testElapsedSec(), cycle);
        double acc = 0.0;
        for (size_t i = 0; i < secs.size(); ++i)
        {
            acc += secs[i];
            if (m < acc) return (int) i;
        }
        return (int) secs.size() - 1;
    }

    std::vector<DmxShow::Universe> buildFrame (int stepIdx) const
    {
        const int uni   = juce::jlimit (0, 7, (int) universeSlider.getValue());
        const int start = juce::jlimit (1, 512, (int) startChanSlider.getValue());

        std::vector<DmxShow::Universe> frame ((size_t) uni + 1);
        for (auto& u : frame) u.fill (0);

        const auto* f = currentFixture();
        if (f == nullptr || seq.steps.empty())
            return frame;

        const auto layout = ChoreographyEngine::detectPixelLayout (*f);
        const auto& st = seq.steps[(size_t) juce::jlimit (0, (int) seq.steps.size() - 1, stepIdx)];
        const int nSeg = (int) st.segments.size();

        auto put = [&] (int chOffset, juce::uint8 v)
        {
            const int addr = start + chOffset;   // chOffset = indice de canal dentro de la fixtura (0-based)
            if (addr >= 1 && addr <= 512)
                frame[(size_t) uni][(size_t) (addr - 1)] = v;
        };

        for (int s = 0; s < (int) layout.rgb.size(); ++s)
        {
            Seg sg; if (s < nSeg) sg = st.segments[(size_t) s];
            const float lvl = sg.on ? juce::jlimit (0.0f, 1.0f, sg.intensity) : 0.0f;
            put (layout.rgb[(size_t) s][0], (juce::uint8) juce::jlimit (0.0f, 255.0f, sg.r * lvl));
            put (layout.rgb[(size_t) s][1], (juce::uint8) juce::jlimit (0.0f, 255.0f, sg.g * lvl));
            put (layout.rgb[(size_t) s][2], (juce::uint8) juce::jlimit (0.0f, 255.0f, sg.b * lvl));
        }
        for (int s = 0; s < (int) layout.white.size(); ++s)
        {
            Seg sg; if (s < nSeg) sg = st.segments[(size_t) s];
            float w = 0.0f;
            if (sg.whiteOn)
            {
                w = juce::jlimit (0.0f, 1.0f, sg.white);
            }
            else if (sg.on)
            {
                const int mx = juce::jmax (sg.r, juce::jmax (sg.g, sg.b));
                const int mn = juce::jmin (sg.r, juce::jmin (sg.g, sg.b));
                const float sat = mx > 0 ? (float) (mx - mn) / (float) mx : 0.0f;
                if (sat < 0.25f) w = juce::jlimit (0.0f, 1.0f, sg.intensity) * (mx / 255.0f);
            }
            put (layout.white[(size_t) s], (juce::uint8) juce::jlimit (0.0f, 255.0f, w * 255.0f));
        }
        return frame;
    }

    //==============================================================================
    void saveToLibrary()
    {
        ChoreographyEngine::Choreography c;
        c.name   = nameEditor.getText().trim();
        if (c.name.isEmpty()) c.name = "Secuencia";
        c.manual = true;

        // "Solo esta fixtura" -> se guarda PARA esa fixtura (por nombre, estable).
        // "Todas las fixturas" -> targetKey vacio. No se modifica el rig.
        if (targetCombo.getSelectedId() == 2)
            c.targetKey = {};
        else if (const auto* f = currentFixture())
            c.targetKey = f->name;

        c.style.name = c.name;
        c.sequence = seq;

        if (loadedChoreoIndex >= 0 && loadedChoreoIndex < (int) playlist.getChoreoLibrary().size()
            && playlist.getChoreoLibrary()[(size_t) loadedChoreoIndex].manual)
            playlist.updateChoreo (loadedChoreoIndex, c);
        else
            loadedChoreoIndex = playlist.addChoreo (c);

        rebuildLoadCombo();
        // Re-selecciona en el combo de carga.
        for (int i = 0; i < loadIndices.size(); ++i)
            if (loadIndices[i] == loadedChoreoIndex)
                loadCombo.setSelectedId (i + 1, juce::dontSendNotification);

        const juce::String msg = c.targetKey.isEmpty()
            ? "Coreografia guardada para todas las fixturas. "
            : "Coreografia guardada para \"" + c.targetKey + "\". Si anades ese equipo al escenario, ya vendra con esta coreografia. ";

        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon, "Coreografias",
            msg + "Asignala a una cancion con \"Coreografias...\" en el Reproductor.");
    }

    //==============================================================================
    // Dibujo de la rejilla de segmentos.
    struct Hit { int row = -1; int seg = -1; };

    juce::Rectangle<float> rowsArea() const
    {
        auto a = gridArea.toFloat();
        a.removeFromBottom (18.0f);   // espacio para la etiqueta inferior
        return a;
    }

    juce::Rectangle<float> rowBounds (int row) const
    {
        auto a = rowsArea();
        const int rows = gridRows();
        const float rowGap = 8.0f;
        const float rowH = (a.getHeight() - rowGap * (rows - 1)) / (float) rows;
        return { a.getX(), a.getY() + (rowH + rowGap) * row, a.getWidth(), rowH };
    }

    juce::Rectangle<float> segmentBounds (int seg, int n, int row) const
    {
        if (n <= 0) return {};
        auto rb = rowBounds (row);
        const float gap = 3.0f;
        const float w = (rb.getWidth() - gap * (n - 1)) / (float) n;
        return { rb.getX() + (w + gap) * seg, rb.getY(), w, rb.getHeight() };
    }

    Hit segmentAt (juce::Point<float> p) const
    {
        const int n = numSegments();
        for (int row = 0; row < gridRows(); ++row)
            for (int s = 0; s < n; ++s)
                if (segmentBounds (s, n, row).contains (p))
                    return { row, s };
        return {};
    }

    void drawSegmentGrid (juce::Graphics& g) const
    {
        using P = LuxLookAndFeel::Palette;
        const int n = numSegments();
        if (seq.steps.empty()) return;

        const int shownStep = previewStep();
        const auto& st = seq.steps[(size_t) juce::jlimit (0, (int) seq.steps.size() - 1, shownStep)];

        // Fondo de la zona.
        g.setColour (juce::Colour (P::bg1));
        g.fillRoundedRectangle (gridArea.toFloat(), 6.0f);

        // --- Fila 0: COLOR (RGB) ---
        for (int s = 0; s < n; ++s)
        {
            auto full = segmentBounds (s, n, 0).reduced (1.0f);
            auto b = full;
            Seg sg; if (s < (int) st.segments.size()) sg = st.segments[(size_t) s];

            if (sg.on)
            {
                const float i = juce::jlimit (0.0f, 1.0f, sg.intensity);
                juce::Colour col ((juce::uint8) (sg.r * i), (juce::uint8) (sg.g * i), (juce::uint8) (sg.b * i));
                g.setColour (col);
                g.fillRoundedRectangle (b, 4.0f);
                g.setColour (juce::Colours::white.withAlpha (0.10f));
                g.fillRoundedRectangle (b.removeFromTop (b.getHeight() * 0.35f), 4.0f);
            }
            else
            {
                g.setColour (juce::Colour (P::control));
                g.fillRoundedRectangle (b, 4.0f);
            }
            g.setColour (juce::Colour (P::line));
            g.drawRoundedRectangle (full, 4.0f, 1.0f);
        }

        // --- Fila 1: BLANCO (solo si la fixtura tiene canal blanco) ---
        if (fixtureHasWhite)
        {
            for (int s = 0; s < n; ++s)
            {
                auto full = segmentBounds (s, n, 1).reduced (1.0f);
                auto b = full;
                Seg sg; if (s < (int) st.segments.size()) sg = st.segments[(size_t) s];

                if (sg.whiteOn)
                {
                    const float w = juce::jlimit (0.0f, 1.0f, sg.white);
                    const juce::uint8 v = (juce::uint8) (235.0f * w + 20.0f);
                    g.setColour (juce::Colour (v, v, v));
                    g.fillRoundedRectangle (b, 4.0f);
                }
                else
                {
                    g.setColour (juce::Colour (P::control).darker (0.25f));
                    g.fillRoundedRectangle (b, 4.0f);
                }
                g.setColour (juce::Colour (P::line));
                g.drawRoundedRectangle (full, 4.0f, 1.0f);
            }

            // Etiquetas de fila.
            g.setColour (juce::Colour (P::textDim));
            g.setFont (juce::FontOptions (10.0f));
            auto r0 = rowBounds (0); auto r1 = rowBounds (1);
            g.drawText ("Color",  juce::Rectangle<int> ((int) r0.getX() + 2, (int) r0.getY() + 1, 60, 12), juce::Justification::topLeft);
            g.drawText ("Blanco", juce::Rectangle<int> ((int) r1.getX() + 2, (int) r1.getY() + 1, 60, 12), juce::Justification::topLeft);
        }

        // Numero de segmentos.
        g.setColour (juce::Colour (P::textDim));
        g.setFont (juce::FontOptions (11.0f));
        auto info = gridArea;
        juce::String txt = juce::String (n) + " segmentos";
        if (fixtureHasWhite) txt << " (color + blanco)";
        txt << "  ·  estado " << juce::String (shownStep + 1) << "/" << juce::String ((int) seq.steps.size());
        g.drawText (txt, info.removeFromBottom (16), juce::Justification::centredRight);
    }

    //==============================================================================
    // Franja de estados (timeline).
    juce::Rectangle<float> stepBounds (int i, int count) const
    {
        if (count <= 0) return {};
        auto a = stripArea.toFloat();
        const float gap = 4.0f;
        const float minW = 48.0f;
        float w = juce::jmax (minW, (a.getWidth() - gap * (count - 1)) / (float) count);
        const float x = a.getX() + (w + gap) * i;
        return { x, a.getY(), w, a.getHeight() };
    }

    int stepAtX (juce::Point<float> p) const
    {
        const int count = (int) seq.steps.size();
        for (int i = 0; i < count; ++i)
            if (stepBounds (i, count).contains (p))
                return i;
        return -1;
    }

    void drawStepsStrip (juce::Graphics& g) const
    {
        using P = LuxLookAndFeel::Palette;
        const int count = (int) seq.steps.size();
        const int shown = previewStep();
        const int n = numSegments();

        for (int i = 0; i < count; ++i)
        {
            auto b = stepBounds (i, count);
            const bool sel = (i == selectedStep);
            const bool live = testing && (i == shown);

            g.setColour (juce::Colour (sel ? P::controlHi : P::control));
            g.fillRoundedRectangle (b.reduced (1.0f), 4.0f);

            // Mini-vista del estado: franjas de color de sus segmentos.
            const auto& st = seq.steps[(size_t) i];
            auto inner = b.reduced (4.0f);
            inner.removeFromBottom (12.0f);
            const float sw = inner.getWidth() / (float) juce::jmax (1, n);
            for (int s = 0; s < n; ++s)
            {
                Seg sg; if (s < (int) st.segments.size()) sg = st.segments[(size_t) s];
                if (sg.on)
                {
                    const float ii = juce::jlimit (0.0f, 1.0f, sg.intensity);
                    g.setColour (juce::Colour ((juce::uint8) (sg.r * ii), (juce::uint8) (sg.g * ii), (juce::uint8) (sg.b * ii)));
                    g.fillRect (inner.getX() + sw * s, inner.getY(), juce::jmax (1.0f, sw - 0.5f), inner.getHeight());
                }
            }

            // Etiqueta: numero + duracion.
            g.setColour (juce::Colour (sel ? P::textHi : P::textMid));
            g.setFont (juce::FontOptions (10.0f));
            const juce::String dur = juce::String (st.duration, st.duration < 1.0 ? 2 : 2)
                                   + (seq.useBeats ? "b" : "s");
            g.drawText (juce::String (i + 1) + " · " + dur,
                        b.reduced (3.0f).removeFromBottom (12.0f).toNearestInt(),
                        juce::Justification::centred);

            // Borde: ambar si seleccionado, verde si activo en la prueba.
            g.setColour (live ? juce::Colours::limegreen
                              : juce::Colour (sel ? P::accent : P::line));
            g.drawRoundedRectangle (b.reduced (1.0f), 4.0f, live || sel ? 2.0f : 1.0f);
        }
    }

    //==============================================================================
    // Listener interno para el selector de color del pincel.
    struct BrushColourListener : public juce::ChangeListener
    {
        SequenceEditorPanel& owner;
        explicit BrushColourListener (SequenceEditorPanel& o) : owner (o) {}
        void changeListenerCallback (juce::ChangeBroadcaster* src) override
        {
            if (auto* cs = dynamic_cast<juce::ColourSelector*> (src))
            {
                owner.brushColour = cs->getCurrentColour();
                owner.updateBrushSwatch();
                owner.repaint();
            }
        }
    };

    PlaylistManager& playlist;
    Seq  seq;
    int  selectedStep = 0;
    int  loadedChoreoIndex = -1;
    int  numWhite = 0;            // segmentos de canal blanco de la fixtura elegida
    bool fixtureHasWhite = false;
    juce::Colour brushColour { juce::Colour (0xffff2a2a) };

    // Estado del barrido (pintar/borrar arrastrando por la rejilla).
    bool painting   = false;
    bool paintErase = false;
    int  paintRow   = -1;

    bool   testing = false;
    double testStartMs = 0.0;

    juce::Rectangle<int> gridArea, stripArea;

    juce::Label title, hint;
    juce::Label fixtureLabel, loadLabel, nameLabel, targetLabel;
    juce::ComboBox fixtureCombo, loadCombo, targetCombo;
    juce::TextEditor nameEditor;
    juce::TextButton newButton, saveButton;
    juce::Array<int>  loadIndices;

    // Opciones del combo "Disenar para": rig + equipos de la libreria no patcheados.
    std::vector<Fixture> designFixtures;
    std::vector<bool>    designInRig;

    juce::Label brushLabel, intensityLabel;
    juce::TextButton brushSwatch, allOnButton, allOffButton;
    juce::OwnedArray<juce::TextButton> quickColourButtons;
    juce::Slider intensitySlider;

    // Paleta fija de colores rapidos (12).
    const std::vector<juce::Colour> presetColours {
        juce::Colour (0xffff0000), juce::Colour (0xffff7f00), juce::Colour (0xffffd400),
        juce::Colour (0xff37e000), juce::Colour (0xff00c8a0), juce::Colour (0xff00aaff),
        juce::Colour (0xff0040ff), juce::Colour (0xff7a2bff), juce::Colour (0xffff2bd0),
        juce::Colour (0xffff5fa0), juce::Colour (0xffffffff), juce::Colour (0xffff9d5c)
    };

    juce::Label durationLabel;
    juce::TextButton addStepButton, dupStepButton, delStepButton, unitButton;
    juce::Slider durationSlider;

    juce::Label bpmLabel, startChanLabel, universeLabel;
    juce::TextButton testButton;
    juce::Slider bpmSlider, startChanSlider, universeSlider;

    std::unique_ptr<BrushColourListener> brushListener { std::make_unique<BrushColourListener> (*this) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequenceEditorPanel)
};
