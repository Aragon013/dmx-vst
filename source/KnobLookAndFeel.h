#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Knob moderno y minimalista (estilo plugin comercial):

      - Geometria SIEMPRE circular (cuadrado centrado), nunca ovalada.
      - Anillo de pista fino y tenue que recorre todo el rango.
      - Arco de valor con glow sutil en el color de acento.
      - Cuerpo central plano y discreto (sin cromados ni gradientes de juguete),
        con un borde finisimo y un indicador limpio.

    Vectorial puro: grosores proporcionales al radio, ideal para HiDPI/Retina.
*/
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        // --- Cuadrado centrado: garantiza que TODO sea circular, nunca ovalado ---
        const float side = (float) juce::jmin (width, height);
        juce::Rectangle<float> square (0.0f, 0.0f, side, side);
        square.setCentre ((float) x + (float) width * 0.5f, (float) y + (float) height * 0.5f);
        square = square.reduced (side * 0.10f);

        const auto  centre = square.getCentre();
        const float radius = square.getWidth() * 0.5f;
        const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
        if (accent == juce::Colour())
            accent = juce::Colour (0xffffb020);

        const float dim = slider.isEnabled() ? 1.0f : 0.4f;

        const float ringRadius = radius * 0.86f;
        const float ringW      = juce::jmax (2.5f, radius * 0.11f);

        // --- Cuerpo central plano y discreto ---
        {
            const auto body = square.reduced (radius * 0.30f);
            g.setColour (juce::Colour (0xff181c24));
            g.fillEllipse (body);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawEllipse (body, 1.0f);
        }

        // --- Anillo de pista (fondo tenue, recorrido completo) ---
        {
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (juce::Colour (0xff262b36));
            g.strokePath (track, juce::PathStrokeType (ringW, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // --- Arco de valor con glow sutil ---
        if (sliderPos > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                                 rotaryStartAngle, angle, true);

            for (int i = 2; i >= 1; --i)   // glow en 2 pasadas suaves
            {
                g.setColour (accent.withAlpha (0.10f * (float) i * dim));
                g.strokePath (value, juce::PathStrokeType (ringW + (float) i * 3.0f,
                                                           juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            }

            g.setColour (accent.withAlpha (dim));   // nucleo del arco
            g.strokePath (value, juce::PathStrokeType (ringW, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // --- Indicador: linea fina + punto luminoso en la punta del arco ---
        {
            const float inner = radius * 0.34f;
            const float outer = radius * 0.62f;
            const float thk   = juce::jmax (2.0f, radius * 0.06f);
            const float cosA  = std::cos (angle - juce::MathConstants<float>::halfPi);
            const float sinA  = std::sin (angle - juce::MathConstants<float>::halfPi);

            g.setColour (juce::Colours::white.withAlpha (0.92f * dim));
            g.drawLine (centre.x + cosA * inner, centre.y + sinA * inner,
                        centre.x + cosA * outer, centre.y + sinA * outer, thk);

            const float dotR = juce::jmax (2.0f, radius * 0.075f);
            const float dx = centre.x + cosA * ringRadius;
            const float dy = centre.y + sinA * ringRadius;
            g.setColour (accent.withAlpha (0.35f * dim));
            g.fillEllipse (dx - dotR * 1.9f, dy - dotR * 1.9f, dotR * 3.8f, dotR * 3.8f);
            g.setColour (accent.withAlpha (dim));
            g.fillEllipse (dx - dotR, dy - dotR, dotR * 2.0f, dotR * 2.0f);
        }
    }
};
