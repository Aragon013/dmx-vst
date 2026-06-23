#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

/**
    Regla de automatizacion REACTIVA: conecta una senal del analisis en vivo
    (volumen, graves o transitorios) con una luz.

    Dos modos:
      - Canal: mapea la senal [0..1] al rango [outLow..outHigh] de un canal concreto
        (p.ej. Volumen -> Dimmer, Graves -> Dimmer del par del bombo).
      - Color: en cada transitorio detectado, cambia el color (cicla una paleta)
        escribiendo en los canales Red/Green/Blue del equipo (golpe -> color nuevo).
*/
struct ReactiveRule
{
    enum class Source { Level, Bass, Transient };

    int     fixtureIndex = 0;
    int     channelIndex = 0;       // usado si ! colorMode
    Source  source       = Source::Level;
    bool    colorMode    = false;   // true: cambia RGB del equipo en cada transitorio
    int     outLow       = 0;       // 0..255
    int     outHigh      = 255;     // 0..255
    bool    enabled      = true;
    juce::String busName;           // vacio = audio propio; si no, lee del bus (pista) con ese nombre

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree v ("Rule");
        v.setProperty ("fixture",  fixtureIndex,    nullptr);
        v.setProperty ("channel",  channelIndex,    nullptr);
        v.setProperty ("source",   (int) source,    nullptr);
        v.setProperty ("color",    colorMode,       nullptr);
        v.setProperty ("outLow",   outLow,          nullptr);
        v.setProperty ("outHigh",  outHigh,         nullptr);
        v.setProperty ("enabled",  enabled,         nullptr);
        v.setProperty ("bus",      busName,         nullptr);
        return v;
    }

    static ReactiveRule fromValueTree (const juce::ValueTree& v)
    {
        ReactiveRule r;
        r.fixtureIndex = (int)  v.getProperty ("fixture", 0);
        r.channelIndex = (int)  v.getProperty ("channel", 0);
        r.source       = (Source) (int) v.getProperty ("source", 0);
        r.colorMode    = (bool) v.getProperty ("color", false);
        r.outLow       = (int)  v.getProperty ("outLow", 0);
        r.outHigh      = (int)  v.getProperty ("outHigh", 255);
        r.enabled      = (bool) v.getProperty ("enabled", true);
        r.busName      = v.getProperty ("bus", "").toString();
        return r;
    }
};

inline juce::String reactiveSourceName (ReactiveRule::Source s)
{
    switch (s)
    {
        case ReactiveRule::Source::Level:     return "Volumen";
        case ReactiveRule::Source::Bass:      return "Graves";
        case ReactiveRule::Source::Transient: return "Transitorio";
        default:                              return "?";
    }
}

/** Paleta de colores RGB para el ciclo en transitorios. */
inline juce::Colour reactivePalette (int index)
{
    static const juce::Colour palette[] =
    {
        juce::Colour (0xffe23b3b),  // rojo
        juce::Colour (0xff3bd45f),  // verde
        juce::Colour (0xff4f8cff),  // azul
        juce::Colour (0xffffb020),  // ambar
        juce::Colour (0xff9b59ff),  // violeta
        juce::Colour (0xff00d0d0),  // cian
        juce::Colour (0xffff5fae),  // rosa
        juce::Colour (0xffffffff),  // blanco
    };
    const int n = (int) (sizeof (palette) / sizeof (palette[0]));
    return palette[((index % n) + n) % n];
}
