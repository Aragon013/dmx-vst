#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PlaybackEngine.h"
#include "DmxShow.h"
#include "../../source/LuxLookAndFeel.h"

/**
    Previsualizacion en vivo del show DMX: por cada equipo del show activo dibuja
    sus canales como medidores verticales (0..255) coloreados por tipo, mas un
    "punto de color" que combina R/G/B*Dimmer para ver el color resultante.

    Lee el ultimo frame del PlaybackEngine bajo demanda (cuando el componente
    padre llama refreshFrom()). No tiene timer propio.
*/
class DmxPreview : public juce::Component
{
public:
    DmxPreview() = default;

    /** Fija el show a previsualizar (para conocer equipos/canales/direcciones). */
    void setShow (const DmxShow* s)
    {
        show = s;
        repaint();
    }

    /** Copia el ultimo frame del motor y repinta. Llamar desde el timer de UI. */
    void refreshFrom (const PlaybackEngine& engine)
    {
        const int nUni = juce::jmax (1, engine.getNumUniverses());
        frame.assign ((size_t) nUni, DmxShow::Universe {});
        for (int u = 0; u < nUni; ++u)
            engine.copyLatestUniverse (u, frame[(size_t) u]);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using P = LuxLookAndFeel::Palette;
        g.fillAll (juce::Colour (P::bg1));

        if (show == nullptr || ! show->valid || show->fixtures.empty())
        {
            g.setColour (juce::Colour (P::textDim));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("Sin show activo", getLocalBounds(), juce::Justification::centred, false);
            return;
        }

        auto area = getLocalBounds().reduced (8);

        const int cardW = 132;
        const int cardH = 120;
        const int gap   = 10;
        int x = area.getX();
        int y = area.getY();

        for (const auto& f : show->fixtures)
        {
            if (x + cardW > area.getRight())
            {
                x = area.getX();
                y += cardH + gap;
            }
            if (y + cardH > area.getBottom())
                break;

            drawFixture (g, f, juce::Rectangle<int> (x, y, cardW, cardH));
            x += cardW + gap;
        }
    }

private:
    juce::uint8 valueAt (int universe, int address1) const
    {
        if (universe < 0 || universe >= (int) frame.size())
            return 0;
        if (address1 < 1 || address1 > 512)
            return 0;
        return frame[(size_t) universe][(size_t) (address1 - 1)];
    }

    static juce::Colour colourForType (ChannelType t)
    {
        switch (t)
        {
            case ChannelType::Red:    return juce::Colour (0xffe23b3b);
            case ChannelType::Green:  return juce::Colour (0xff35c759);
            case ChannelType::Blue:   return juce::Colour (0xff3b82e2);
            case ChannelType::White:  return juce::Colour (0xffe8e8e8);
            case ChannelType::Amber:  return juce::Colour (0xffffb020);
            case ChannelType::UV:     return juce::Colour (0xff9b59ff);
            case ChannelType::Dimmer: return juce::Colour (0xffd0d0d0);
            case ChannelType::Pan:
            case ChannelType::Tilt:   return juce::Colour (0xff4fc3f7);
            default:                  return juce::Colour (0xff8a93a6);
        }
    }

    void drawFixture (juce::Graphics& g, const Fixture& f, juce::Rectangle<int> r)
    {
        using P = LuxLookAndFeel::Palette;

        g.setColour (juce::Colour (P::surface));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (juce::Colour (P::line));
        g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);

        auto inner = r.reduced (8);

        // Punto de color resultante (R/G/B escalado por Dimmer/255).
        int rr = 0, gg = 0, bb = 0, dim = 255; bool hasRgb = false, hasDim = false;
        for (int ci = 0; ci < f.channelCount(); ++ci)
        {
            const auto& c = f.channels[(size_t) ci];
            const int v = valueAt (f.universe, f.dmxAddressOf (ci));
            switch (c.type)
            {
                case ChannelType::Red:    rr = v; hasRgb = true; break;
                case ChannelType::Green:  gg = v; hasRgb = true; break;
                case ChannelType::Blue:   bb = v; hasRgb = true; break;
                case ChannelType::Dimmer: dim = v; hasDim = true; break;
                default: break;
            }
        }

        auto header = inner.removeFromTop (16);
        if (hasRgb || hasDim)
        {
            const float k = hasDim ? dim / 255.0f : 1.0f;
            juce::Colour swatch ((juce::uint8) juce::roundToInt (rr * k),
                                 (juce::uint8) juce::roundToInt (gg * k),
                                 (juce::uint8) juce::roundToInt (bb * k));
            auto dot = header.removeFromLeft (14).withSizeKeepingCentre (12, 12).toFloat();
            g.setColour (swatch);
            g.fillEllipse (dot);
            g.setColour (juce::Colour (P::line));
            g.drawEllipse (dot, 1.0f);
            header.removeFromLeft (4);
        }

        g.setColour (juce::Colour (P::textHi));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (f.name, header, juce::Justification::centredLeft, true);

        // Medidores verticales por canal.
        const int n = f.channelCount();
        if (n <= 0) return;

        auto meters = inner.reduced (0, 2);
        const float colW = meters.getWidth() / (float) n;
        const float barW = juce::jmax (3.0f, colW - 3.0f);

        for (int ci = 0; ci < n; ++ci)
        {
            const auto& c = f.channels[(size_t) ci];
            const int v = valueAt (f.universe, f.dmxAddressOf (ci));
            const float frac = v / 255.0f;

            juce::Rectangle<float> col (meters.getX() + ci * colW, (float) meters.getY(),
                                        barW, (float) meters.getHeight());

            g.setColour (juce::Colour (P::control));
            g.fillRoundedRectangle (col, 2.0f);

            auto fill = col.withTop (col.getBottom() - col.getHeight() * frac);
            g.setColour (colourForType (c.type).withAlpha (0.35f + 0.65f * frac));
            g.fillRoundedRectangle (fill, 2.0f);
        }
    }

    const DmxShow* show = nullptr;
    std::vector<DmxShow::Universe> frame { DmxShow::Universe {} };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DmxPreview)
};
