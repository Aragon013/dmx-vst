#pragma once

#include "../../source/FixtureModel.h"
#include <array>
#include <cstdint>
#include <vector>

/**
    Un "show" DMX generado para un tema: el conjunto de equipos (rig) con su
    automatizacion (keyframes + clips de efecto) ya rellenada por el
    ChoreographyEngine, mas el tempo y la duracion.

    Es de SOLO LECTURA durante la reproduccion: el motor de playback (Fase 6) lo
    muestrea frame a frame sin bloqueos. La automatizacion vive en beats para
    sincronizar con el BPM; renderFrame convierte segundos -> beats.
*/
struct DmxShow
{
    bool   valid          = false;
    double bpm            = 120.0;
    double beatOffset     = 0.0;    // segundos del primer beat real (fase de la rejilla)
    double lengthSeconds  = 0.0;
    int    numUniverses   = 1;
    std::vector<Fixture> fixtures;

    using Universe = std::array<juce::uint8, 512>;

    /** Numero total de keyframes (info/depuracion). */
    int countKeyframes() const
    {
        int n = 0;
        for (const auto& f : fixtures)
            for (const auto& c : f.channels)
                n += (int) c.keyframes.size();
        return n;
    }

    /** Asegura que out tiene un universo por cada universo usado y lo deja a cero. */
    void prepareBuffer (std::vector<Universe>& out) const
    {
        out.assign ((size_t) juce::jmax (1, numUniverses), Universe {});
    }

    /** Evalua todos los canales en un instante (segundos) y escribe los valores DMX. */
    void renderFrame (double seconds, std::vector<Universe>& out) const
    {
        for (auto& u : out)
            u.fill (0);

        const double beats = (seconds - beatOffset) * bpm / 60.0;

        for (const auto& f : fixtures)
        {
            if (f.universe < 0 || f.universe >= (int) out.size())
                continue;

            auto& uni = out[(size_t) f.universe];
            for (int ci = 0; ci < f.channelCount(); ++ci)
            {
                const auto& chan = f.channels[(size_t) ci];
                float v = evaluateChannel (chan.keyframes, chan.clips, beats);
                if (v < 0.0f)
                    v = (float) chan.defaultValue;

                const int addr = f.dmxAddressOf (ci);  // 1..512
                if (addr >= 1 && addr <= 512)
                    uni[(size_t) (addr - 1)] = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (v));
            }
        }
    }

    //==============================================================================
    // Serializacion (para persistir el show manual del piano roll en la sesion / .lux).

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree v ("DmxShow");
        v.setProperty ("valid",     valid,         nullptr);
        v.setProperty ("bpm",       bpm,           nullptr);
        v.setProperty ("beatOffset",beatOffset,    nullptr);
        v.setProperty ("length",    lengthSeconds, nullptr);
        v.setProperty ("universes", numUniverses,  nullptr);
        for (const auto& f : fixtures)
            v.appendChild (f.toValueTree(), nullptr);
        return v;
    }

    static DmxShow fromValueTree (const juce::ValueTree& v)
    {
        DmxShow s;
        if (! v.isValid() || ! v.hasType ("DmxShow"))
            return s;

        s.valid         = (bool)   v.getProperty ("valid", false);
        s.bpm           = (double) v.getProperty ("bpm", 120.0);
        s.beatOffset    = (double) v.getProperty ("beatOffset", 0.0);
        s.lengthSeconds = (double) v.getProperty ("length", 0.0);
        s.numUniverses  = (int)    v.getProperty ("universes", 1);
        for (int i = 0; i < v.getNumChildren(); ++i)
            if (v.getChild (i).hasType ("Fixture"))
                s.fixtures.push_back (Fixture::fromValueTree (v.getChild (i)));
        return s;
    }
};
