#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <vector>

/**
    Analisis por stem (Fase 4): envolvente de energia + transientes de una pista
    separada (drums/bass/vocals/other). Permite mapear las luces por instrumento.
*/
struct StemAnalysis
{
    juce::String        name;        // "drums" / "bass" / "vocals" / "other"
    std::vector<float>  energy;      // envolvente normalizada 0..1
    std::vector<double> transients;  // onsets (segundos)
};

/**
    Seccion estructural del tema detectada por energia (intro, subida, drop,
    verso, break, outro). Permite que el show cambie de estilo automaticamente
    segun la parte de la cancion.
*/
struct TrackSection
{
    // OJO: los valores se serializan en el sidecar (.lux). No reordenar; los
    // nuevos tipos van al FINAL para no invalidar analisis ya guardados.
    enum Type { Intro = 0, Build, Drop, Verse, Break, Outro, Chorus };

    double startSec = 0.0;
    double endSec   = 0.0;
    float  level    = 0.0f;   // energia media normalizada 0..1 (estable en la seccion)
    int    type     = Verse;  // Type

    static juce::String typeName (int t)
    {
        switch (t)
        {
            case Intro: return "Intro";
            case Build: return "Subida";
            case Drop:  return "Drop";
            case Verse: return "Verso";
            case Break: return "Break";
            case Outro: return "Outro";
            case Chorus: return "Chorus";
            default:    return "Parte";
        }
    }
};

/**
    Resultado del pre-proceso de un tema, cacheable en un archivo sidecar (.lux).

    Fase 3: el `OfflineAnalyzer` real (reusando `source/AudioAnalyzer`) rellena
    duracion, BPM estimado, una envolvente de energia gruesa y la lista de
    transientes (onsets) detectados.
    Fase 4: ademas, si hay separacion de stems disponible (HTDemucs), rellena
    `stems` con el analisis por instrumento.

    Se versiona con kSchemaVersion + la firma del archivo de origen (tamano +
    fecha de modificacion) para invalidar la cache si el audio cambia o si sube
    el esquema.
*/
struct TrackAnalysis
{
    static constexpr int kSchemaVersion = 7;

    bool   valid          = false;
    double lengthSeconds  = 0.0;
    double estimatedBpm    = 0.0;
    double beatOffset      = 0.0;    // segundos del primer beat real (fase de la rejilla)
    std::vector<float>  energy;      // envolvente de energia normalizada 0..1 (gruesa)
    std::vector<double> transients;  // instantes (segundos) de onsets detectados
    std::vector<StemAnalysis> stems; // analisis por stem (vacio si no hay separacion)
    std::vector<float>  chroma;      // 12 clases de altura (C..B) normalizadas 0..1 (identidad de color)
    std::vector<TrackSection> sections; // partes estructurales por energia (intro/drop/...)

    // Firma del archivo de origen para validar la cache.
    juce::int64 sourceSize         = 0;
    juce::int64 sourceModifiedMs   = 0;

    // Helpers de empaquetado base64 (reutilizados por stems).
    static juce::String packEnergy (const std::vector<float>& e)
    {
        juce::MemoryBlock mb (e.size());
        for (size_t i = 0; i < e.size(); ++i)
            mb[(int) i] = (char) juce::jlimit (0, 255, juce::roundToInt (e[i] * 255.0f));
        return mb.toBase64Encoding();
    }

    static std::vector<float> unpackEnergy (const juce::String& b64)
    {
        juce::MemoryBlock mb;
        mb.fromBase64Encoding (b64);
        std::vector<float> e (mb.getSize());
        for (size_t i = 0; i < mb.getSize(); ++i)
            e[i] = (float) (juce::uint8) mb[(int) i] / 255.0f;
        return e;
    }

    static juce::String packTransients (const std::vector<double>& t)
    {
        juce::MemoryBlock tb (t.size() * sizeof (float));
        for (size_t i = 0; i < t.size(); ++i)
        {
            const float f = (float) t[i];
            tb.copyFrom (&f, (int) (i * sizeof (float)), sizeof (float));
        }
        return tb.toBase64Encoding();
    }

    static std::vector<double> unpackTransients (const juce::String& b64)
    {
        juce::MemoryBlock tb;
        tb.fromBase64Encoding (b64);
        const size_t n = tb.getSize() / sizeof (float);
        std::vector<double> t (n);
        for (size_t i = 0; i < n; ++i)
        {
            float f = 0.0f;
            tb.copyTo (&f, (int) (i * sizeof (float)), sizeof (float));
            t[i] = (double) f;
        }
        return t;
    }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree v ("LuxAnalysis");
        v.setProperty ("schema",       kSchemaVersion,   nullptr);
        v.setProperty ("length",       lengthSeconds,    nullptr);
        v.setProperty ("bpm",          estimatedBpm,     nullptr);
        v.setProperty ("beatOffset",   beatOffset,       nullptr);
        v.setProperty ("sourceSize",   sourceSize,       nullptr);
        v.setProperty ("sourceMod",    sourceModifiedMs, nullptr);

        // Energia empaquetada como cadena de bytes (0..255) en base64.
        v.setProperty ("energy", packEnergy (energy), nullptr);

        // Transientes empaquetados como floats (segundos) en base64.
        v.setProperty ("transients", packTransients (transients), nullptr);

        // Chroma (12 clases de altura) empaquetado como bytes 0..255.
        v.setProperty ("chroma", packEnergy (chroma), nullptr);

        // Secciones estructurales: un nodo hijo "Section" por parte.
        for (const auto& s : sections)
        {
            juce::ValueTree sv ("Section");
            sv.setProperty ("start", s.startSec, nullptr);
            sv.setProperty ("end",   s.endSec,   nullptr);
            sv.setProperty ("level", s.level,    nullptr);
            sv.setProperty ("type",  s.type,     nullptr);
            v.appendChild (sv, nullptr);
        }

        // Stems (Fase 4): un nodo hijo "Stem" por instrumento.
        for (const auto& s : stems)
        {
            juce::ValueTree sv ("Stem");
            sv.setProperty ("name",       s.name,                    nullptr);
            sv.setProperty ("energy",     packEnergy (s.energy),     nullptr);
            sv.setProperty ("transients", packTransients (s.transients), nullptr);
            v.appendChild (sv, nullptr);
        }

        return v;
    }

    static TrackAnalysis fromValueTree (const juce::ValueTree& v)
    {
        TrackAnalysis a;
        if (! v.hasType ("LuxAnalysis"))
            return a;
        if ((int) v.getProperty ("schema", 0) != kSchemaVersion)
            return a;

        a.lengthSeconds    = (double)      v.getProperty ("length", 0.0);
        a.estimatedBpm     = (double)      v.getProperty ("bpm", 0.0);
        a.beatOffset       = (double)      v.getProperty ("beatOffset", 0.0);
        a.sourceSize       = (juce::int64) v.getProperty ("sourceSize", 0);
        a.sourceModifiedMs = (juce::int64) v.getProperty ("sourceMod", 0);

        a.energy     = unpackEnergy     (v.getProperty ("energy", "").toString());
        a.transients = unpackTransients (v.getProperty ("transients", "").toString());
        a.chroma     = unpackEnergy     (v.getProperty ("chroma", "").toString());

        for (int i = 0; i < v.getNumChildren(); ++i)
        {
            const juce::ValueTree sv = v.getChild (i);
            if (sv.hasType ("Stem"))
            {
                StemAnalysis s;
                s.name       = sv.getProperty ("name", "").toString();
                s.energy     = unpackEnergy     (sv.getProperty ("energy", "").toString());
                s.transients = unpackTransients (sv.getProperty ("transients", "").toString());
                a.stems.push_back (std::move (s));
            }
            else if (sv.hasType ("Section"))
            {
                TrackSection s;
                s.startSec = (double) sv.getProperty ("start", 0.0);
                s.endSec   = (double) sv.getProperty ("end",   0.0);
                s.level    = (float)  sv.getProperty ("level", 0.0);
                s.type     = (int)    sv.getProperty ("type",  TrackSection::Verse);
                a.sections.push_back (std::move (s));
            }
        }

        a.valid = true;
        return a;
    }
};
