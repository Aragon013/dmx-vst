#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../../source/LuxLookAndFeel.h"
#include <vector>
#include <functional>

/**
    ColourSelector con una fila de MUESTRAS FIJAS (swatches) abajo: 7 colores
    rapidos de eleccion comun (incluido el rosa). JUCE las muestra solo si
    getNumSwatches() devuelve >0. Las muestras son editables (setSwatchColour) y
    se conservan mientras viva el selector. */
class SwatchColourSelector : public juce::ColourSelector
{
public:
    SwatchColourSelector()
        : juce::ColourSelector (juce::ColourSelector::showColourAtTop
                                | juce::ColourSelector::showSliders
                                | juce::ColourSelector::showColourspace)
    {
        swatches = {
            juce::Colour (0xffff2a2a),  // rojo
            juce::Colour (0xffff8a00),  // naranja
            juce::Colour (0xffffd000),  // amarillo
            juce::Colour (0xff28d860),  // verde
            juce::Colour (0xff2a7bff),  // azul
            juce::Colour (0xffc83cff),  // violeta
            juce::Colour (0xffff4fa6),  // rosa
            juce::Colour (0xff20d8d8),  // cian
            juce::Colour (0xffffffff)   // blanco
        };
    }

    int  getNumSwatches() const override                 { return (int) swatches.size(); }
    juce::Colour getSwatchColour (int index) const override
    {
        return juce::isPositiveAndBelow (index, (int) swatches.size())
                 ? swatches[(size_t) index] : juce::Colours::black;
    }
    void setSwatchColour (int index, const juce::Colour& c) override
    {
        if (juce::isPositiveAndBelow (index, (int) swatches.size()))
            swatches[(size_t) index] = c;
    }

private:
    std::vector<juce::Colour> swatches;
};

/**
    Panel modal para elegir los COLORES PREFERIDOS de una cancion.

    Cuando el usuario fija uno o mas colores, la coreografia de ese tema usara
    SOLO esos colores (en vez de la identidad automatica por chroma). Hasta 6
    ranuras; cada una se activa con su casilla y abre un selector de color.
*/
class PreferredColorsPanel : public juce::Component,
                             private juce::ChangeListener
{
public:
    static constexpr int kMaxSlots = 6;

    std::function<void (const std::vector<juce::Colour>&)> onApply;

    explicit PreferredColorsPanel (const std::vector<juce::Colour>& initial)
    {
        using P = LuxLookAndFeel::Palette;

        title.setText ("Colores preferidos de la cancion", juce::dontSendNotification);
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (P::textHi));
        addAndMakeVisible (title);

        info.setText ("Activa los colores que quieras. La coreografia usara SOLO esos. "
                      "Sin ninguno activo, se usa la identidad automatica del tema.",
                      juce::dontSendNotification);
        info.setFont (juce::FontOptions (12.0f));
        info.setColour (juce::Label::textColourId, juce::Colour (P::textMid));
        info.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (info);

        // Colores por defecto si una ranura no trae valor.
        static const juce::Colour defaults[kMaxSlots] = {
            juce::Colour (0xffff2a2a), juce::Colour (0xff2a7bff), juce::Colour (0xff28d860),
            juce::Colour (0xffffb020), juce::Colour (0xffc83cff), juce::Colour (0xff20d8d8)
        };

        for (int i = 0; i < kMaxSlots; ++i)
        {
            auto& s = slots[(size_t) i];
            s.colour = (i < (int) initial.size()) ? initial[(size_t) i] : defaults[i];
            s.enabled.setToggleState (i < (int) initial.size(), juce::dontSendNotification);
            s.enabled.setColour (juce::ToggleButton::textColourId, juce::Colour (P::textMid));
            addAndMakeVisible (s.enabled);

            s.swatch.setButtonText ("");
            s.swatch.onClick = [this, i] { pickColour (i); };
            addAndMakeVisible (s.swatch);
        }

        autoButton.onClick = [this]
        {
            for (auto& s : slots) s.enabled.setToggleState (false, juce::dontSendNotification);
            apply();
        };
        addAndMakeVisible (autoButton);

        applyButton.onClick = [this] { apply(); };
        addAndMakeVisible (applyButton);

        setSize (380, 360);
    }

    void paint (juce::Graphics& g) override
    {
        using P = LuxLookAndFeel::Palette;
        g.fillAll (juce::Colour (P::bg1));

        // Dibuja las muestras de color encima de cada boton.
        for (const auto& s : slots)
        {
            auto b = s.swatch.getBounds().toFloat().reduced (3.0f);
            g.setColour (s.enabled.getToggleState() ? s.colour : s.colour.withAlpha (0.25f));
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (juce::Colour (P::line));
            g.drawRoundedRectangle (b, 5.0f, 1.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        title.setBounds (area.removeFromTop (24));
        area.removeFromTop (4);
        info.setBounds (area.removeFromTop (40));
        area.removeFromTop (8);

        for (auto& s : slots)
        {
            auto row = area.removeFromTop (32);
            s.enabled.setBounds (row.removeFromLeft (90));
            s.swatch.setBounds (row.removeFromLeft (210).reduced (0, 2));
            area.removeFromTop (6);
        }

        area.removeFromTop (6);
        auto buttons = area.removeFromTop (32);
        autoButton.setBounds (buttons.removeFromLeft (150));
        applyButton.setBounds (buttons.removeFromRight (110));
    }

private:
    struct Slot
    {
        juce::ToggleButton enabled { "Color" };
        juce::TextButton   swatch;
        juce::Colour       colour;
    };

    void pickColour (int index)
    {
        auto selector = std::make_unique<SwatchColourSelector>();
        selector->setName ("color");
        selector->setCurrentColour (slots[(size_t) index].colour);
        selector->setSize (260, 320);

        activeSlot = index;
        activeSelector = selector.get();
        activeSelector->addChangeListener (this);

        auto& box = juce::CallOutBox::launchAsynchronously (
            std::move (selector), slots[(size_t) index].swatch.getScreenBounds(), nullptr);
        juce::ignoreUnused (box);

        slots[(size_t) index].enabled.setToggleState (true, juce::dontSendNotification);
    }

    void changeListenerCallback (juce::ChangeBroadcaster* src) override
    {
        if (src == activeSelector && activeSlot >= 0 && activeSlot < kMaxSlots)
        {
            slots[(size_t) activeSlot].colour = activeSelector->getCurrentColour();
            repaint();
        }
    }

    void apply()
    {
        std::vector<juce::Colour> result;
        for (const auto& s : slots)
            if (s.enabled.getToggleState())
                result.push_back (s.colour);
        if (onApply)
            onApply (result);
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    }

    juce::Label      title, info;
    Slot             slots[kMaxSlots];
    juce::TextButton autoButton  { "Auto (sin preferencia)" };
    juce::TextButton applyButton { "Aplicar" };
    juce::ColourSelector* activeSelector = nullptr;
    int                   activeSlot = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreferredColorsPanel)
};
