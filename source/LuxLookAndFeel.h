#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "KnobLookAndFeel.h"

/**
    LuxLookAndFeel — tema global "Estudio a Oscuras".

    Inspirado en suites comerciales (iZotope/Soundtoys): fondo casi negro, superficies
    sutilmente elevadas, tipografia clara y limpia, bordes finos neutros y color vivo
    reservado para lo que importa (arcos de knobs, lineas de automatizacion, medidores,
    indicadores activos). Hereda el knob premium de KnobLookAndFeel.

    Se instala una sola vez en el editor (setLookAndFeel) y cascada a todos los hijos.
*/
class LuxLookAndFeel : public KnobLookAndFeel
{
public:
    //==============================================================================
    // Paleta unica del producto (referenciable desde los paneles).
    struct Palette
    {
        static constexpr juce::uint32 bg0     = 0xff0a0c10;   // fondo mas profundo
        static constexpr juce::uint32 bg1     = 0xff10131a;   // panel base
        static constexpr juce::uint32 surface = 0xff181d27;   // superficie elevada / control
        static constexpr juce::uint32 control = 0xff1c222e;   // combos / botones
        static constexpr juce::uint32 controlHi= 0xff262d3b;  // hover
        static constexpr juce::uint32 line     = 0xff2a3140;  // bordes / separadores
        static constexpr juce::uint32 lineSoft = 0xff1e2430;  // separadores tenues
        static constexpr juce::uint32 textHi   = 0xffe9edf4;  // texto principal
        static constexpr juce::uint32 textMid  = 0xffaab2c2;  // texto secundario
        static constexpr juce::uint32 textDim  = 0xff6b7486;  // texto apagado
        static constexpr juce::uint32 accent   = 0xffffb020;  // ambar de marca
        static constexpr juce::uint32 accent2  = 0xff4fc3f7;  // cian (datos)
    };

    LuxLookAndFeel()
    {
        const auto accent  = juce::Colour (Palette::accent);
        const auto textHi  = juce::Colour (Palette::textHi);
        const auto textMid = juce::Colour (Palette::textMid);

        // Esquema base
        auto scheme = getCurrentColourScheme();
        scheme.setUIColour (ColourScheme::windowBackground,  juce::Colour (Palette::bg1));
        scheme.setUIColour (ColourScheme::widgetBackground,  juce::Colour (Palette::surface));
        scheme.setUIColour (ColourScheme::menuBackground,    juce::Colour (Palette::bg0));
        scheme.setUIColour (ColourScheme::outline,           juce::Colour (Palette::line));
        scheme.setUIColour (ColourScheme::defaultText,       textHi);
        scheme.setUIColour (ColourScheme::defaultFill,       accent);
        scheme.setUIColour (ColourScheme::highlightedText,   juce::Colours::black);
        scheme.setUIColour (ColourScheme::highlightedFill,   accent);
        scheme.setUIColour (ColourScheme::menuText,          textHi);
        setColourScheme (scheme);

        // Etiquetas
        setColour (juce::Label::textColourId,            textMid);

        // ComboBox
        setColour (juce::ComboBox::backgroundColourId,   juce::Colour (Palette::control));
        setColour (juce::ComboBox::textColourId,         textHi);
        setColour (juce::ComboBox::outlineColourId,      juce::Colour (Palette::line));
        setColour (juce::ComboBox::arrowColourId,        textMid);
        setColour (juce::ComboBox::focusedOutlineColourId, accent.withAlpha (0.7f));

        // PopupMenu
        setColour (juce::PopupMenu::backgroundColourId,         juce::Colour (Palette::bg0));
        setColour (juce::PopupMenu::textColourId,               textHi);
        setColour (juce::PopupMenu::headerTextColourId,         textMid);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId,    juce::Colours::white);

        // TextButton
        setColour (juce::TextButton::buttonColourId,     juce::Colour (Palette::control));
        setColour (juce::TextButton::buttonOnColourId,   accent.withAlpha (0.22f));
        setColour (juce::TextButton::textColourOffId,    textHi);
        setColour (juce::TextButton::textColourOnId,     accent.brighter (0.2f));

        // ToggleButton
        setColour (juce::ToggleButton::textColourId,     textMid);
        setColour (juce::ToggleButton::tickColourId,     accent);
        setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (Palette::line));

        // TextEditor
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (Palette::bg0));
        setColour (juce::TextEditor::textColourId,       textHi);
        setColour (juce::TextEditor::outlineColourId,    juce::Colour (Palette::line));
        setColour (juce::TextEditor::focusedOutlineColourId, accent.withAlpha (0.7f));
        setColour (juce::TextEditor::highlightColourId,  accent.withAlpha (0.30f));
        setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        setColour (juce::CaretComponent::caretColourId,  accent);

        // ListBox
        setColour (juce::ListBox::backgroundColourId,    juce::Colour (Palette::bg1));
        setColour (juce::ListBox::outlineColourId,       juce::Colour (Palette::line));

        // ScrollBar
        setColour (juce::ScrollBar::thumbColourId,       juce::Colour (Palette::line).brighter (0.25f));
        setColour (juce::ScrollBar::trackColourId,       juce::Colour (Palette::bg0));

        // Slider (texto y caja)
        setColour (juce::Slider::textBoxTextColourId,    textHi);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::trackColourId,          juce::Colour (Palette::line));
        setColour (juce::Slider::backgroundColourId,     juce::Colour (Palette::bg0));
        setColour (juce::Slider::thumbColourId,          accent);

        // TabbedComponent
        setColour (juce::TabbedComponent::backgroundColourId, juce::Colour (Palette::bg1));
        setColour (juce::TabbedComponent::outlineColourId,    juce::Colours::transparentBlack);
        setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
    }

    //==============================================================================
    // Tipografias coherentes
    juce::Font getLabelFont (juce::Label& l) override
    {
        auto f = l.getFont();
        return f;
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (juce::FontOptions (13.5f));
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font (juce::FontOptions (13.5f));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions ((float) juce::jmin (15.0f, buttonHeight * 0.55f), juce::Font::bold));
    }

    //==============================================================================
    // ComboBox: superficie con borde fino y chevron limpio
    void drawComboBox (juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                       int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
        const float radius = 6.0f;

        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, radius);

        const bool focused = box.hasKeyboardFocus (true);
        g.setColour (focused ? box.findColour (juce::ComboBox::focusedOutlineColourId)
                             : box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, radius, focused ? 1.4f : 1.0f);

        // Chevron
        const float cx = (float) width - 16.0f;
        const float cy = (float) height * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
        chevron.lineTo (cx,        cy + 3.0f);
        chevron.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (10, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setColour (juce::Label::textColourId, box.findColour (juce::ComboBox::textColourId));
    }

    //==============================================================================
    // PopupMenu redondeado
    void drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                             const juce::PopupMenu::Options&) override
    {
        const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat();
        g.setColour (findColour (juce::PopupMenu::backgroundColourId));
        g.fillRoundedRectangle (bounds.reduced (0.5f), 8.0f);
        g.setColour (juce::Colour (Palette::line));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);
    }

    //==============================================================================
    // Botones: superficie plana con borde fino; estado on/hover con realce sutil
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const float radius = 6.0f;

        auto base = backgroundColour;
        if (shouldDrawButtonAsDown)
            base = base.brighter (0.10f);
        else if (shouldDrawButtonAsHighlighted)
            base = base.brighter (0.06f);

        // Gradiente vertical muy sutil para dar volumen
        juce::ColourGradient grad (base.brighter (0.05f), bounds.getCentreX(), bounds.getY(),
                                   base.darker (0.12f),    bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bounds, radius);

        const bool on = button.getToggleState();
        g.setColour (on ? juce::Colour (Palette::accent).withAlpha (0.85f)
                        : juce::Colour (Palette::line));
        g.drawRoundedRectangle (bounds, radius, on ? 1.4f : 1.0f);

        if (shouldDrawButtonAsHighlighted && ! on)
        {
            g.setColour (juce::Colour (Palette::accent).withAlpha (0.25f));
            g.drawRoundedRectangle (bounds, radius, 1.0f);
        }
    }

    //==============================================================================
    // ToggleButton con tick redondeado luminoso
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted, bool /*down*/) override
    {
        const float boxSize = juce::jmin (18.0f, button.getHeight() - 2.0f);
        juce::Rectangle<float> box (4.0f, (button.getHeight() - boxSize) * 0.5f, boxSize, boxSize);

        const bool on = button.getToggleState();

        g.setColour (juce::Colour (Palette::bg0));
        g.fillRoundedRectangle (box, 4.0f);

        g.setColour (on ? juce::Colour (Palette::accent)
                        : (shouldDrawButtonAsHighlighted ? juce::Colour (Palette::accent).withAlpha (0.5f)
                                                         : juce::Colour (Palette::line)));
        g.drawRoundedRectangle (box, 4.0f, on ? 1.6f : 1.2f);

        if (on)
        {
            // Glow + tick
            g.setColour (juce::Colour (Palette::accent).withAlpha (0.30f));
            g.fillRoundedRectangle (box.reduced (-1.0f), 5.0f);

            auto tick = box.reduced (boxSize * 0.28f);
            juce::Path p;
            p.startNewSubPath (tick.getX(),                 tick.getCentreY() + 1.0f);
            p.lineTo          (tick.getCentreX() - 1.0f,    tick.getBottom());
            p.lineTo          (tick.getRight(),             tick.getY());
            g.setColour (juce::Colour (Palette::accent).brighter (0.4f));
            g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }

        g.setColour (button.findColour (juce::ToggleButton::textColourId));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (button.getButtonText(),
                    button.getLocalBounds().withTrimmedLeft ((int) boxSize + 12),
                    juce::Justification::centredLeft, true);
    }

    //==============================================================================
    // Slider lineal moderno (track fino + thumb luminoso). IncDec usa los botones.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearVertical
            || style == juce::Slider::LinearBar)
        {
            const bool horiz = (style != juce::Slider::LinearVertical);
            auto track = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);

            const float thickness = 5.0f;
            juce::Rectangle<float> bar = horiz
                ? juce::Rectangle<float> (track.getX(), track.getCentreY() - thickness * 0.5f, track.getWidth(), thickness)
                : juce::Rectangle<float> (track.getCentreX() - thickness * 0.5f, track.getY(), thickness, track.getHeight());

            g.setColour (slider.findColour (juce::Slider::backgroundColourId));
            g.fillRoundedRectangle (bar, thickness * 0.5f);

            // Parte rellena hasta el thumb
            auto accent = juce::Colour (Palette::accent);
            juce::Rectangle<float> filled = bar;
            if (horiz) filled = filled.withRight (sliderPos);
            else       filled = filled.withTop (sliderPos);
            g.setColour (accent.withAlpha (0.85f));
            g.fillRoundedRectangle (filled, thickness * 0.5f);

            // Thumb
            const float r = 7.0f;
            const float cx = horiz ? sliderPos : track.getCentreX();
            const float cy = horiz ? track.getCentreY() : sliderPos;
            g.setColour (accent.withAlpha (0.30f));
            g.fillEllipse (cx - r - 1.0f, cy - r - 1.0f, (r + 1.0f) * 2.0f, (r + 1.0f) * 2.0f);
            g.setColour (juce::Colour (Palette::textHi));
            g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
            g.setColour (accent);
            g.fillEllipse (cx - r * 0.45f, cy - r * 0.45f, r * 0.9f, r * 0.9f);
            return;
        }

        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                          minSliderPos, maxSliderPos, style, slider);
    }

    //==============================================================================
    // Scrollbar minimalista
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                        bool isVertical, int thumbStart, int thumbSize,
                        bool isMouseOver, bool /*isMouseDown*/) override
    {
        juce::Rectangle<int> thumb = isVertical
            ? juce::Rectangle<int> (x + width / 2 - 3, thumbStart, 6, thumbSize)
            : juce::Rectangle<int> (thumbStart, y + height / 2 - 3, thumbSize, 6);

        auto col = bar.findColour (juce::ScrollBar::thumbColourId);
        g.setColour (isMouseOver ? col.brighter (0.3f) : col);
        g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
    }

    //==============================================================================
    // Pestanas estilo suite: texto, e indicador (subrayado) ambar para la activa
    void drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                        bool isMouseOver, bool /*isMouseDown*/) override
    {
        auto area = button.getLocalBounds();
        const bool active = button.getToggleState();

        if (active)
        {
            g.setColour (juce::Colour (Palette::surface));
            g.fillRect (area);
        }
        else if (isMouseOver)
        {
            g.setColour (juce::Colour (Palette::control).withAlpha (0.5f));
            g.fillRect (area);
        }

        // Indicador inferior ambar (solo activa) — color vivo reservado
        if (active)
        {
            auto bar = area.removeFromBottom (3);
            g.setColour (juce::Colour (Palette::accent));
            g.fillRect (bar);
            g.setColour (juce::Colour (Palette::accent).withAlpha (0.25f));
            g.fillRect (bar.withY (bar.getY() - 2).withHeight (2));
        }

        g.setColour (active ? juce::Colour (Palette::textHi)
                            : (isMouseOver ? juce::Colour (Palette::textMid)
                                           : juce::Colour (Palette::textDim)));
        g.setFont (juce::FontOptions (14.0f, active ? juce::Font::bold : juce::Font::plain));
        g.drawText (button.getButtonText(), button.getLocalBounds(),
                    juce::Justification::centred, false);
    }

    void drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g) override
    {
        g.setColour (juce::Colour (Palette::bg1));
        g.fillRect (bar.getLocalBounds());
    }

    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar& bar, juce::Graphics& g,
                                       int w, int h) override
    {
        // Linea base tenue bajo toda la barra de pestanas
        juce::ignoreUnused (w);
        g.setColour (juce::Colour (Palette::line));
        g.fillRect (0, h - 1, bar.getWidth(), 1);
    }

    //==============================================================================
    // TextEditor redondeado
    void fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                   juce::TextEditor& editor) override
    {
        g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f), 6.0f);
    }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                juce::TextEditor& editor) override
    {
        const bool focused = editor.hasKeyboardFocus (true) && editor.isEnabled();
        g.setColour (focused ? editor.findColour (juce::TextEditor::focusedOutlineColourId)
                             : editor.findColour (juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle (juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f),
                                6.0f, focused ? 1.4f : 1.0f);
    }
};
