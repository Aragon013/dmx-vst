#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../../source/LuxLookAndFeel.h"
#include "PlaylistManager.h"
#include "PreferredColorsPanel.h"   // SwatchColourSelector (muestras de color fijas)
#include <vector>
#include <functional>

/**
    Ventana de PROPIEDADES de una cancion (clic derecho -> Propiedades).

    Reune los ajustes especificos del tema en un solo sitio:
      - Preferencia de colores: hasta 6 colores; la coreografia usara SOLO esos
        (vacio = identidad automatica por chroma del tema).
      - Coreografia: fuente Auto (IA) o Manual (piano roll), hornear desde IA,
        editar en el piano roll, descartar la manual.
      - Analisis: re-entrenar (re-separar stems y re-analizar).

    Opera directamente sobre el PlaylistManager y avisa al host con onChanged
    para refrescar el escenario/preview. La edicion en piano roll se delega via
    onEditManual (la abre el componente padre).
*/
class SongPropertiesPanel : public juce::Component,
                            private juce::ChangeListener
{
public:
    static constexpr int kMaxSlots = 9;

    /** Avisar al host de que algo cambio (para refrescar visuales). */
    std::function<void()> onChanged;
    /** Pedir al host que abra el piano roll de este tema (por indice). */
    std::function<void (int)> onEditManual;

    SongPropertiesPanel (PlaylistManager& pmRef, int trackIndex)
        : pm (pmRef), index (trackIndex)
    {
        using P = LuxLookAndFeel::Palette;

        const auto& track = pm.getTrack (index);

        title.setText (track.displayName, juce::dontSendNotification);
        title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        addAndMakeVisible (title);

        juce::String meta = track.lengthString();
        if (track.analysis.valid && track.analysis.estimatedBpm > 0.0)
            meta += "   -   " + juce::String (juce::roundToInt (track.analysis.estimatedBpm)) + " BPM";
        meta += "   -   " + Track::stateLabel (track.state);
        if (pm.trackHasStems (index))
            meta += "   -   stems";
        subtitle.setText (meta, juce::dontSendNotification);
        subtitle.setFont (juce::FontOptions (12.0f));
        subtitle.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        addAndMakeVisible (subtitle);

        // --- Preferencia de colores ---
        sectionModeToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (P::textMid));
        sectionModeToggle.setToggleState (pm.isSectionColorMode (index), juce::dontSendNotification);
        sectionModeToggle.setTooltip ("Define una paleta distinta para cada parte (verso, coro, subida...). "
                                      "Al activarlo, los colores globales se ignoran.");
        sectionModeToggle.onClick = [this]
        {
            pm.setSectionColorMode (index, sectionModeToggle.getToggleState());
            updateColorTargetUI();
            notifyChanged();
        };
        addAndMakeVisible (sectionModeToggle);

        sectionPickerLabel.setText ("Parte:", juce::dontSendNotification);
        sectionPickerLabel.setFont (juce::FontOptions (12.0f));
        sectionPickerLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        addAndMakeVisible (sectionPickerLabel);

        sectionTypes = pm.sectionTypesPresent (index);
        for (int i = 0; i < (int) sectionTypes.size(); ++i)
            sectionPicker.addItem (TrackSection::typeName (sectionTypes[(size_t) i]), i + 1);
        currentSectionType = sectionTypes.empty() ? -1 : sectionTypes[0];
        sectionPicker.setSelectedId (1, juce::dontSendNotification);
        sectionPicker.onChange = [this]
        {
            const int idx = sectionPicker.getSelectedId() - 1;
            if (idx >= 0 && idx < (int) sectionTypes.size())
                currentSectionType = sectionTypes[(size_t) idx];
            loadColorsForTarget();
            resized();
            repaint();
        };
        addAndMakeVisible (sectionPicker);

        colorsInfo.setFont (juce::FontOptions (11.5f));
        colorsInfo.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        colorsInfo.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (colorsInfo);

        for (int i = 0; i < kMaxSlots; ++i)
        {
            auto& s = slots[(size_t) i];
            s.swatch.onClick = [this, i] { pickColour (i); };
            addAndMakeVisible (s.swatch);

            s.remove.setButtonText ("X");
            s.remove.setTooltip ("Quitar este color");
            s.remove.setColour (juce::TextButton::textColourOffId, juce::Colour (P::textMid));
            s.remove.onClick = [this, i] { removeColor (i); };
            addAndMakeVisible (s.remove);
        }

        addColorButton.setColour (juce::TextButton::textColourOffId, juce::Colour (P::accent));
        addColorButton.onClick = [this] { addColor(); };
        addAndMakeVisible (addColorButton);

        // --- Coreografia ---
        sourceLabel.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        sourceLabel.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (sourceLabel);

        sourceCombo.addItem ("Auto (IA)", 1);
        sourceCombo.addItem ("Manual (editada)", 2);
        sourceCombo.onChange = [this]
        {
            const bool manual = sourceCombo.getSelectedId() == 2;
            if (manual && ! pm.hasManualShow (index))
            {
                // No hay manual aun: la creamos horneando desde la IA.
                pm.bakeManualFromAuto (index);
            }
            pm.setChoreoMode (index, manual ? Track::ChoreoMode::Manual : Track::ChoreoMode::Auto);
            refreshChoreoButtons();
            notifyChanged();
        };
        addAndMakeVisible (sourceCombo);

        bakeButton.onClick = [this]
        {
            pm.bakeManualFromAuto (index);
            sourceCombo.setSelectedId (2, juce::dontSendNotification);
            refreshChoreoButtons();
            notifyChanged();
        };
        addAndMakeVisible (bakeButton);

        editManualButton.onClick = [this]
        {
            if (! pm.hasManualShow (index))
                pm.bakeManualFromAuto (index);
            if (onEditManual) onEditManual (index);
            closeWindow();
        };
        addAndMakeVisible (editManualButton);

        discardManualButton.onClick = [this]
        {
            pm.discardManual (index);
            sourceCombo.setSelectedId (1, juce::dontSendNotification);
            refreshChoreoButtons();
            notifyChanged();
        };
        addAndMakeVisible (discardManualButton);

        // --- Analisis ---
        retrainButton.setTooltip ("Vuelve a separar los stems y re-analiza el tema desde cero.");
        retrainButton.onClick = [this]
        {
            pm.retrainTrack (index);
            notifyChanged();
            closeWindow();
        };
        addAndMakeVisible (retrainButton);

        closeButton.onClick = [this] { closeWindow(); };
        addAndMakeVisible (closeButton);

        sourceCombo.setSelectedId (pm.isManualMode (index) ? 2 : 1, juce::dontSendNotification);
        refreshChoreoButtons();
        updateColorTargetUI();

        setSize (440, 600);
    }

    void paint (juce::Graphics& g) override
    {
        using P = LuxLookAndFeel::Palette;
        g.fillAll (juce::Colour (P::bg1));

        // Cabeceras de seccion.
        drawSectionHeader (g, "Preferencia de colores", colorsHeaderY);
        drawSectionHeader (g, "Coreografia",            choreoHeaderY);
        drawSectionHeader (g, "Analisis",               analysisHeaderY);

        if (colorCount == 0)
        {
            g.setColour (juce::Colour (P::textDim));
            g.setFont (juce::FontOptions (11.5f, juce::Font::italic));
            g.drawText ("Sin colores (identidad automatica del tema)",
                        emptyHintBounds, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);

        title.setBounds (area.removeFromTop (26));
        subtitle.setBounds (area.removeFromTop (18));
        area.removeFromTop (10);

        // --- Colores ---
        colorsHeaderY = area.getY();
        area.removeFromTop (22);
        {
            auto row = area.removeFromTop (26);
            sectionModeToggle.setBounds (row.removeFromLeft (180));
            if (sectionModeToggle.getToggleState())
            {
                row.removeFromLeft (10);
                sectionPickerLabel.setBounds (row.removeFromLeft (44));
                sectionPicker.setBounds (row.removeFromLeft (150));
            }
        }
        area.removeFromTop (4);
        colorsInfo.setBounds (area.removeFromTop (30));
        area.removeFromTop (6);

        // Lista de colores en flujo: cada chip = muestra + boton quitar. Al final, Agregar.
        {
            const int chipW = 46, chipH = 30, rmW = 18, gap = 8, rowH = 34;
            const int top = area.getY();
            const int left = area.getX();
            const int right = area.getRight();
            int x = left, y = top;

            emptyHintBounds = juce::Rectangle<int> (left, top, right - left, chipH);

            for (int i = 0; i < kMaxSlots; ++i)
            {
                auto& s = slots[(size_t) i];
                const bool vis = i < colorCount;
                s.swatch.setVisible (vis);
                s.remove.setVisible (vis);
                if (! vis) continue;

                const int chipTotal = chipW + 2 + rmW;
                if (x + chipTotal > right) { x = left; y += rowH; }
                s.swatch.setBounds (x, y, chipW, chipH);
                s.remove.setBounds (x + chipW + 2, y + (chipH - rmW) / 2, rmW, rmW);
                x += chipTotal + gap;
            }

            const bool canAdd = colorCount < kMaxSlots;
            addColorButton.setVisible (canAdd);
            if (canAdd)
            {
                const int addW = 160;
                if (colorCount > 0 && x + addW > right) { x = left; y += rowH; }
                addColorButton.setBounds (x, y, addW, chipH);
            }

            area.removeFromTop ((y + rowH) - top);
        }
        area.removeFromTop (12);

        // --- Coreografia ---
        choreoHeaderY = area.getY();
        area.removeFromTop (22);
        {
            auto row = area.removeFromTop (28);
            sourceLabel.setBounds (row.removeFromLeft (60));
            sourceCombo.setBounds (row.removeFromLeft (200));
        }
        area.removeFromTop (6);
        {
            auto row = area.removeFromTop (28);
            bakeButton.setBounds (row.removeFromLeft (175));
            row.removeFromLeft (8);
            editManualButton.setBounds (row.removeFromLeft (160));
        }
        area.removeFromTop (6);
        discardManualButton.setBounds (area.removeFromTop (28).removeFromLeft (175));
        area.removeFromTop (12);

        // --- Analisis ---
        analysisHeaderY = area.getY();
        area.removeFromTop (22);
        retrainButton.setBounds (area.removeFromTop (30).removeFromLeft (240));

        // --- Cerrar (abajo a la derecha) ---
        auto bottom = getLocalBounds().reduced (16).removeFromBottom (30);
        closeButton.setBounds (bottom.removeFromRight (110));
    }

private:
    // Muestra de color que se pinta sola (asi el color SIEMPRE se ve).
    struct Swatch : public juce::Component
    {
        juce::Colour colour { juce::Colours::grey };
        std::function<void()> onClick;

        Swatch() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (juce::Colours::white.withAlpha (isMouseOverOrDragging() ? 0.95f : 0.55f));
            g.drawRoundedRectangle (b, 5.0f, 1.6f);
        }
        void mouseUp    (const juce::MouseEvent&) override { if (onClick) onClick(); }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit  (const juce::MouseEvent&) override { repaint(); }
    };

    struct Slot
    {
        Swatch           swatch;
        juce::TextButton remove { "X" };
        juce::Colour     colour;
    };

    void drawSectionHeader (juce::Graphics& g, const juce::String& text, int y)
    {
        using P = LuxLookAndFeel::Palette;
        auto r = juce::Rectangle<int> (16, y, getWidth() - 32, 18);
        g.setColour (juce::Colour (P::accent));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (text.toUpperCase(), r, juce::Justification::centredLeft);
        g.setColour (juce::Colour (P::line));
        g.fillRect (16.0f, (float) (y + 18), (float) (getWidth() - 32), 1.0f);
    }

    void pickColour (int idx)
    {
        auto selector = std::make_unique<SwatchColourSelector>();
        selector->setName ("color");
        selector->setCurrentColour (slots[(size_t) idx].colour);
        selector->setSize (260, 340);

        activeSlot = idx;
        activeSelector = selector.get();
        activeSelector->addChangeListener (this);

        juce::CallOutBox::launchAsynchronously (
            std::move (selector), slots[(size_t) idx].swatch.getScreenBounds(), nullptr);

        applyColors();
    }

    void addColor()
    {
        if (colorCount >= kMaxSlots) return;
        const int i = colorCount;
        ++colorCount;                 // la ranura ya tiene un color por defecto
        applyColors();
        resized();
        repaint();
        pickColour (i);               // abre el selector para el color recien agregado
    }

    void removeColor (int idx)
    {
        if (idx < 0 || idx >= colorCount) return;
        for (int i = idx; i < colorCount - 1; ++i)
        {
            slots[(size_t) i].colour        = slots[(size_t) (i + 1)].colour;
            slots[(size_t) i].swatch.colour = slots[(size_t) i].colour;
        }
        --colorCount;
        applyColors();
        resized();
        repaint();
    }

    void changeListenerCallback (juce::ChangeBroadcaster* src) override
    {
        if (src == activeSelector && activeSlot >= 0 && activeSlot < kMaxSlots)
        {
            slots[(size_t) activeSlot].colour        = activeSelector->getCurrentColour();
            slots[(size_t) activeSlot].swatch.colour = slots[(size_t) activeSlot].colour;
            slots[(size_t) activeSlot].swatch.repaint();
            applyColors();
        }
    }

    void applyColors()
    {
        std::vector<juce::Colour> result;
        for (int i = 0; i < colorCount; ++i)
            result.push_back (slots[(size_t) i].colour);
        if (sectionModeToggle.getToggleState())
        {
            if (currentSectionType >= 0)
                pm.setSectionColors (index, currentSectionType, result);
        }
        else
        {
            pm.setPreferredColors (index, result);
        }
        repaint();
        notifyChanged();
    }

    /** Carga en las ranuras los colores del destino activo (global o seccion). */
    void loadColorsForTarget()
    {
        static const juce::Colour defaults[kMaxSlots] = {
            juce::Colour (0xffff2a2a), juce::Colour (0xff2a7bff), juce::Colour (0xff28d860),
            juce::Colour (0xffffb020), juce::Colour (0xffc83cff), juce::Colour (0xff20d8d8),
            juce::Colour (0xffff5ec4), juce::Colour (0xff9bff3c), juce::Colour (0xffff8a3c)
        };
        std::vector<juce::Colour> cur = sectionModeToggle.getToggleState()
            ? (currentSectionType >= 0 ? pm.getSectionColors (index, currentSectionType)
                                       : std::vector<juce::Colour>{})
            : pm.getPreferredColors (index);
        colorCount = juce::jlimit (0, kMaxSlots, (int) cur.size());
        for (int i = 0; i < kMaxSlots; ++i)
        {
            slots[(size_t) i].colour        = (i < (int) cur.size()) ? cur[(size_t) i] : defaults[i];
            slots[(size_t) i].swatch.colour = slots[(size_t) i].colour;
            slots[(size_t) i].swatch.repaint();
        }
    }

    /** Refresca visibilidad/textos al cambiar el modo de colores y recarga la paleta. */
    void updateColorTargetUI()
    {
        const bool sec = sectionModeToggle.getToggleState();
        sectionPicker.setVisible (sec);
        sectionPickerLabel.setVisible (sec);
        colorsInfo.setText (sec
            ? "Cada parte usa SU paleta (max 9). Las partes sin colores usan la identidad automatica del tema."
            : "Colores de toda la cancion. La coreografia usara SOLO esos (max 9). Sin ninguno = automatico.",
            juce::dontSendNotification);
        loadColorsForTarget();
        resized();
        repaint();
    }

    void refreshChoreoButtons()
    {
        const bool hasManual = pm.hasManualShow (index);
        discardManualButton.setEnabled (hasManual);
    }

    void notifyChanged() { if (onChanged) onChanged(); }

    void closeWindow()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    }

    PlaylistManager& pm;
    int index = -1;

    juce::Label title, subtitle, colorsInfo;
    juce::Label sourceLabel { {}, "Fuente:" };
    Slot        slots[kMaxSlots];
    int         colorCount = 0;
    juce::Rectangle<int> emptyHintBounds;
    juce::TextButton addColorButton      { "+  Agregar un color" };

    juce::ToggleButton sectionModeToggle { "Colores por seccion" };
    juce::Label        sectionPickerLabel;
    juce::ComboBox     sectionPicker;
    std::vector<int>   sectionTypes;
    int                currentSectionType = -1;

    juce::ComboBox   sourceCombo;
    juce::TextButton bakeButton          { "Hornear desde IA" };
    juce::TextButton editManualButton    { "Editar piano roll..." };
    juce::TextButton discardManualButton { "Descartar manual" };

    juce::TextButton retrainButton       { "Re-entrenar (separar stems)" };
    juce::TextButton closeButton         { "Cerrar" };

    int colorsHeaderY = 0, choreoHeaderY = 0, analysisHeaderY = 0;

    juce::ColourSelector* activeSelector = nullptr;
    int activeSlot = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongPropertiesPanel)
};
