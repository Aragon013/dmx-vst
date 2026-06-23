#pragma once

#include "../../source/FixtureModel.h"
#include "TrackAnalysis.h"
#include "DmxShow.h"
#include "MotionFigure.h"
#include <functional>
#include <vector>
#include <algorithm>
#include <map>
#include <cmath>

/**
    Genera un show DMX a partir del analisis offline (energia + transientes +
    BPM, y desde la Fase 4 tambien por STEM) y de un rig de equipos.

    - Si el analisis trae stems (drums/bass/vocals/other), se hace un mapeo POR
      INSTRUMENTO: bateria -> PAR/strobe, bajo -> barra LED, voces -> acentos en
      cabezas, melodia/otros -> movimiento de cabezas.
    - Si no hay stems (demucs no disponible), se usa el mapeo MONO de la Fase 5
      sobre la mezcla completa.

    Todo en beats (sincronizado al BPM). El resultado es de solo lectura para el
    motor de playback.
*/
namespace ChoreographyEngine
{
    struct RGB { juce::uint8 r, g, b; };

    /** Paleta de color para el ciclo por compas. */
    inline RGB paletteColour (int index)
    {
        static const RGB pal[] = {
            { 255,  30,  40 },  // rojo
            {  30, 120, 255 },  // azul
            {  40, 220,  90 },  // verde
            { 255, 150,  20 },  // ambar
            { 200,  40, 230 },  // magenta
            {  20, 220, 220 },  // cian
            { 255, 235,  60 },  // amarillo
            { 255,  60, 160 }   // rosa
        };
        const int n = (int) (sizeof (pal) / sizeof (pal[0]));
        return pal[((index % n) + n) % n];
    }

    //==============================================================================
    // IDENTIDAD DE COLOR POR CANCION (Fase 2): la paleta se deriva del contenido
    // tonal del tema (chroma / notas dominantes) para dar una identidad constante.
    //==============================================================================

    /** Hue (0..1) asociado a una clase de altura (0=C). Recorre el circulo de
        quintas para que notas musicalmente relacionadas tengan colores cercanos. */
    inline float noteHue (int pitchClass)
    {
        const int pc = ((pitchClass % 12) + 12) % 12;
        return std::fmod ((float) (pc * 7) / 12.0f, 1.0f);   // circulo de quintas
    }

    /** Construye una paleta estable (3-5 colores) a partir del chroma de la
        cancion. Las notas mas fuertes definen los colores -> identidad del tema. */
    inline std::vector<RGB> paletteFromChroma (const std::vector<float>& chroma)
    {
        std::vector<RGB> pal;
        if (chroma.size() < 12)
            return pal;

        // Ordena las clases de altura por energia.
        std::vector<std::pair<float, int>> ranked;
        for (int pc = 0; pc < 12; ++pc)
            ranked.push_back ({ chroma[(size_t) pc], pc });
        std::sort (ranked.begin(), ranked.end(),
                   [] (auto& a, auto& b) { return a.first > b.first; });

        if (ranked[0].first <= 1.0e-6f)
            return pal;   // chroma vacio -> usa paleta por defecto

        // Toma hasta 4 notas dominantes que superen un umbral relativo.
        const float thresh = ranked[0].first * 0.45f;
        int taken = 0;
        for (int i = 0; i < 12 && taken < 4; ++i)
        {
            if (i > 0 && ranked[(size_t) i].first < thresh)
                break;
            const int   pc  = ranked[(size_t) i].second;
            const float hue = noteHue (pc);
            // Brillo/saturacion ligeramente variados para riqueza visual.
            const float sat = 0.92f;
            const float val = (i == 0) ? 1.0f : (0.82f + 0.05f * (float) (taken % 3));
            const auto c = juce::Colour::fromHSV (hue, sat, val, 1.0f);
            pal.push_back ({ c.getRed(), c.getGreen(), c.getBlue() });
            ++taken;
        }

        // Garantiza al menos 3 colores para que haya variedad espacial.
        while (pal.size() < 3 && ! pal.empty())
        {
            const int   pc  = ranked[(size_t) (pal.size() % 12)].second;
            const auto c = juce::Colour::fromHSV (noteHue (pc + 3), 0.9f, 0.9f, 1.0f);
            pal.push_back ({ c.getRed(), c.getGreen(), c.getBlue() });
        }
        return pal;
    }

    /** Color de identidad: elige de la paleta del tema de forma ESTABLE (cambia
        por frases largas, no como pasarela). slot = posicion espacial o de equipo. */
    inline RGB identityColour (const std::vector<RGB>& pal, int slot, double beats, double colorBeats)
    {
        if (pal.empty())
            return paletteColour (slot);
        const int P = (int) pal.size();
        const int phrase = (int) std::floor (beats / juce::jmax (1.0, colorBeats * 4.0));
        int i = ((slot + phrase) % P + P) % P;
        return pal[(size_t) i];
    }

    /** AUTO-COLOR por ENERGIA: modula un color base (la identidad tonal del tema)
        segun la intensidad de la seccion. Secciones tranquilas -> color mas tenue y
        algo desaturado; secciones intensas -> saturacion y brillo plenos, con un
        empujon CALIDO en el climax. Asi el color evoluciona con la cancion sin
        perder la identidad de la paleta. */
    inline RGB energyColour (RGB base, float energy)
    {
        energy = juce::jlimit (0.0f, 1.0f, energy);
        const auto c = juce::Colour (base.r, base.g, base.b);

        const float sat = juce::jlimit (0.0f, 1.0f, c.getSaturation()  * juce::jmap (energy, 0.55f, 1.0f));
        const float val = juce::jlimit (0.0f, 1.0f, c.getBrightness() * juce::jmap (energy, 0.72f, 1.0f));

        // Empujon calido en el climax: acerca el tono al naranja (~0.05) hasta un 18%.
        float hue = c.getHue();
        if (energy > 0.8f)
        {
            const float warmAmt = juce::jlimit (0.0f, 0.18f, (energy - 0.8f) / 0.2f * 0.18f);
            float dh = 0.05f - hue;
            if (dh >  0.5f) dh -= 1.0f;
            if (dh < -0.5f) dh += 1.0f;
            hue = std::fmod (hue + dh * warmAmt + 1.0f, 1.0f);
        }

        const auto out = juce::Colour::fromHSV (hue, sat, val, 1.0f);
        return { out.getRed(), out.getGreen(), out.getBlue() };
    }

    //==============================================================================
    /** Estilo de show: define la DINAMICA de encendido/apagado y del color.
        Permite que el escenario no se limite a "subir/bajar" todas las luces igual. */
    enum class ColorStyle  { Cycle, Rainbow, Warm, Cool, Mono, Auto };
    enum class MotionStyle { Unison, Chase, Alternate, Wave, Pulse,
                             Bounce, Build, Sparkle, Bloom, Symmetry,
                             Strobe, Stack, Theater, Ripple, Random, Auto };

    /** Ruido determinista 0..1 (para destellos/saltos reproducibles sin estado). */
    inline float hashNoise (int a, int b)
    {
        juce::uint32 h = (juce::uint32) (a * 73856093) ^ (juce::uint32) (b * 19349663);
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        return (float) (h & 0xffffffu) / (float) 0xffffffu;
    }

    /** Elige el patron de movimiento segun la energia local (0..1) de la seccion.
        Secciones tranquilas -> ondas suaves; secciones intensas -> estrobo/pulso.
        Dentro de cada banda alterna entre patrones afines POR FRASE para que el
        Auto IA tenga variedad y no se vuelva monotono. */
    inline MotionStyle motionForEnergy (float e, int phrase = 0)
    {
        const bool alt = (phrase & 1) != 0;
        if (e < 0.18f) return alt ? MotionStyle::Symmetry  : MotionStyle::Wave;       // ambiente
        if (e < 0.32f) return alt ? MotionStyle::Bloom     : MotionStyle::Build;      // intro
        if (e < 0.46f) return alt ? MotionStyle::Theater   : MotionStyle::Alternate;  // estrofa
        if (e < 0.60f) return alt ? MotionStyle::Stack     : MotionStyle::Chase;      // desarrollo
        if (e < 0.74f) return alt ? MotionStyle::Bounce    : MotionStyle::Ripple;     // pre-drop
        if (e < 0.88f) return alt ? MotionStyle::Sparkle   : MotionStyle::Pulse;      // drop
        return MotionStyle::Strobe;                                                    // climax
    }

    /** Patron de movimiento POR TIPO de seccion: el CORO es el auge emotivo (pleno,
        al tempo, NUNCA estroboscopico), el DROP es caos, la intro/break son calmados.
        Fallback a la energia si no se conoce el tipo de seccion. */
    inline MotionStyle motionForSection (int secType, float e, int phrase)
    {
        const bool alt = (phrase & 1) != 0;
        switch (secType)
        {
            case TrackSection::Chorus: return alt ? MotionStyle::Bloom    : MotionStyle::Pulse;    // auge pleno al tempo
            case TrackSection::Drop:   return alt ? MotionStyle::Strobe   : MotionStyle::Sparkle;  // caos / climax
            case TrackSection::Build:  return alt ? MotionStyle::Build    : MotionStyle::Stack;     // crece
            case TrackSection::Verse:  return alt ? MotionStyle::Chase    : MotionStyle::Alternate; // groove
            case TrackSection::Break:  return alt ? MotionStyle::Wave     : MotionStyle::Symmetry;  // respiro
            case TrackSection::Intro:
            case TrackSection::Outro:  return alt ? MotionStyle::Symmetry : MotionStyle::Wave;      // ambiente
            default:                   return motionForEnergy (e, phrase);
        }
    }

    /** Figura de movimiento de cabezas/spiders POR TIPO de seccion: el coro va
        AMPLIO y melodico (circulos/ochos/espiral/pendulo), el drop energetico
        (caos/zigzag/abanico), la intro/break calmados. Fallback por energia. */
    inline motionfig::Figure figureForSection (int secType, float e, int phrase)
    {
        using F = motionfig::Figure;
        const int v = ((phrase % 4) + 4) % 4;
        switch (secType)
        {
            case TrackSection::Chorus: { const F o[] = { F::Circle, F::Eight,     F::Spiral,  F::Pendulum };   return o[v]; }
            case TrackSection::Drop:   { const F o[] = { F::Chaos,  F::Zigzag,    F::FanBeam, F::SnapPoints }; return o[v]; }
            case TrackSection::Build:  { const F o[] = { F::Spread, F::FanBeam,   F::Spiral,  F::Tide };       return o[v]; }
            case TrackSection::Break:  { const F o[] = { F::Static, F::SweepSync, F::Mirror,  F::Pendulum };   return o[v]; }
            case TrackSection::Verse:  { const F o[] = { F::SweepWave, F::Cross,  F::Wave2,   F::Tide };       return o[v]; }
            case TrackSection::Intro:
            case TrackSection::Outro:  { const F o[] = { F::SweepSync, F::Mirror, F::Spread,  F::Static };     return o[v]; }
            default:                   return motionfig::figureForEnergy (e, phrase);
        }
    }

    struct ShowStyle
    {
        juce::String name       { "Equilibrado" };
        ColorStyle   color      = ColorStyle::Cycle;
        MotionStyle  motion     = MotionStyle::Unison;
        double       colorBeats = 1.0;   // beats entre cambios de color
        double       moveSpeed  = 1.0;   // velocidad de movimiento de cabezas
        float        colorOffset = 0.0f; // brillo base anadido a canales RGB de barra pixel (0..1)
        float        whiteOffset = 0.0f; // brillo base anadido a canales de blanco de barra pixel (0..1)
        bool         fullAuto    = false; // modo IA total: el motor elige estilo+color+movimiento por seccion
        motionfig::Figure moveFigure = motionfig::Figure::Auto; // coreografia de movimiento (Auto = por energia)
    };

    /** Estilos predefinidos que el usuario puede elegir desde la UI. */
    inline std::vector<ShowStyle> stylePresets()
    {
        return {
            { "Equilibrado",   ColorStyle::Cycle,   MotionStyle::Unison,    1.0, 1.0 },
            { "Auto IA",       ColorStyle::Cycle,   MotionStyle::Auto,      1.0, 1.1 },
            { "Chase",         ColorStyle::Cycle,   MotionStyle::Chase,     1.0, 1.3 },
            { "Alterno",       ColorStyle::Warm,    MotionStyle::Alternate, 2.0, 1.0 },
            { "Onda",          ColorStyle::Cool,    MotionStyle::Wave,      2.0, 0.7 },
            { "Arcoiris",      ColorStyle::Rainbow, MotionStyle::Unison,    0.5, 1.0 },
            { "Pulso",         ColorStyle::Cycle,   MotionStyle::Pulse,     1.0, 1.6 },
            { "Rebote",        ColorStyle::Cool,    MotionStyle::Bounce,    1.0, 1.2 },
            { "Acumular",      ColorStyle::Warm,    MotionStyle::Build,     2.0, 0.9 },
            { "Destellos",     ColorStyle::Cycle,   MotionStyle::Sparkle,   0.5, 1.4 },
            { "Floracion",     ColorStyle::Cool,    MotionStyle::Bloom,     2.0, 0.8 },
            { "Espejo",        ColorStyle::Warm,    MotionStyle::Symmetry,  1.5, 1.0 },
            { "Estrobo",       ColorStyle::Cycle,   MotionStyle::Strobe,    1.0, 2.0 },
            { "Apilar",        ColorStyle::Cool,    MotionStyle::Stack,     2.0, 0.9 },
            { "Teatro",        ColorStyle::Warm,    MotionStyle::Theater,   1.0, 1.1 },
            { "Onda expansiva",ColorStyle::Cool,    MotionStyle::Ripple,    1.0, 1.0 },
            { "Aleatorio",     ColorStyle::Cycle,   MotionStyle::Random,    1.0, 1.2 },
            { "Mono Chase",    ColorStyle::Mono,    MotionStyle::Chase,     4.0, 1.0 },
            // Color reactivo a la energia (identidad tonal modulada por intensidad).
            { "Color energia", ColorStyle::Auto,    MotionStyle::Unison,    1.0, 1.0 },
            // Modo IA TOTAL: el motor escoge estilo/color/movimiento por seccion. fullAuto=true.
            { "Full Auto",     ColorStyle::Cycle,   MotionStyle::Auto,      1.0, 1.2, 0.0f, 0.0f, true }
        };
    }

    /** Pool integrado del modo FULL AUTO: una progresion de estilos ordenada de
        CALMO a INTENSO. El motor lo recorre segun el nivel de energia de cada
        SECCION del tema (intro/verso/build/drop/...), cambiando a la vez el patron
        de movimiento Y el color para sacarle el maximo partido al lightshow sin que
        el usuario tenga que tocar nada. base aporta los offsets de brillo. */
    inline std::vector<ShowStyle> fullAutoPool (const ShowStyle& base)
    {
        std::vector<ShowStyle> pool = {
            //  nombre          color                motion                  cBeats moveSpd
            { "FA Ambiente",   ColorStyle::Cool,    MotionStyle::Wave,      4.0,  0.6f },
            { "FA Intro",      ColorStyle::Auto,    MotionStyle::Bloom,     4.0,  0.8f },
            { "FA Verso",      ColorStyle::Auto,    MotionStyle::Alternate, 2.0,  1.0f },
            { "FA Desarrollo", ColorStyle::Auto,    MotionStyle::Chase,     1.0,  1.2f },
            { "FA Pre-drop",   ColorStyle::Auto,    MotionStyle::Ripple,    1.0,  1.4f },
            { "FA Drop",       ColorStyle::Auto,    MotionStyle::Pulse,     1.0,  1.7f },
            { "FA Climax",     ColorStyle::Rainbow, MotionStyle::Strobe,    0.5,  2.0f }
        };
        for (auto& s : pool)
        {
            s.colorOffset = base.colorOffset;   // conserva los offsets de brillo de la barra
            s.whiteOffset = base.whiteOffset;
        }
        // El pool ya viene ordenado calmo->intenso (lo recorre activeStyleAt por energia).
        return pool;
    }

    /** Nombres (en espanol) de los patrones de movimiento, en el ORDEN del enum. */
    inline juce::StringArray motionStyleNames()
    {
        return { "Unisono", "Chase", "Alterno", "Onda", "Pulso", "Rebote", "Acumular",
                 "Destellos", "Floracion", "Espejo", "Estrobo", "Apilar", "Teatro",
                 "Onda expansiva", "Aleatorio", "Auto IA" };
    }

    /** Nombres (en espanol) de los estilos de color, en el ORDEN del enum. */
    inline juce::StringArray colorStyleNames()
    {
        return { "Ciclo (identidad)", "Arcoiris", "Calido", "Frio", "Mono", "Auto (energia)" };
    }

    /** Intensidad relativa (0..1) de un patron, para que el Auto IA asigne las
        coreografias de una cancion a bandas de energia (calmo -> intenso). */
    inline float motionIntensity (MotionStyle m)
    {
        switch (m)
        {
            case MotionStyle::Wave:     return 0.08f;
            case MotionStyle::Symmetry: return 0.14f;
            case MotionStyle::Bloom:    return 0.20f;
            case MotionStyle::Build:    return 0.26f;
            case MotionStyle::Unison:   return 0.32f;
            case MotionStyle::Alternate:return 0.40f;
            case MotionStyle::Theater:  return 0.46f;
            case MotionStyle::Stack:    return 0.52f;
            case MotionStyle::Chase:    return 0.58f;
            case MotionStyle::Bounce:   return 0.64f;
            case MotionStyle::Ripple:   return 0.70f;
            case MotionStyle::Random:   return 0.74f;
            case MotionStyle::Pulse:    return 0.80f;
            case MotionStyle::Sparkle:  return 0.86f;
            case MotionStyle::Strobe:   return 0.95f;
            case MotionStyle::Auto:     default: return 0.50f;
        }
    }

    //==============================================================================
    /** Estado de UN segmento dentro de un paso manual: encendido/apagado, color e
        intensidad. El color solo aplica si on==true. */
    struct SegState
    {
        bool        on        = false;
        juce::uint8 r         = 255;
        juce::uint8 g         = 255;
        juce::uint8 b         = 255;
        float       intensity = 1.0f;   // 0..1
        bool        whiteOn   = false;  // canal blanco del segmento (independiente del RGB)
        float       white     = 1.0f;   // 0..1
    };

    /** Un PASO (estado) de una secuencia manual: el estado de todos los segmentos
        + cuanto dura ese estado antes de pasar al siguiente. */
    struct SeqStep
    {
        std::vector<SegState> segments;
        double                duration = 1.0;   // en beats si useBeats, si no en segundos
    };

    /** Una SECUENCIA MANUAL dibujada a mano para una fixtura segmentada (p.ej. la
        barra LED de 22 secciones). Se reproduce en bucle; al aplicarla a una
        cancion, la intensidad global sigue la energia del audio. */
    struct ManualSequence
    {
        bool                 useBeats    = true;   // unidad de tiempo (beats vs segundos)
        int                  numSegments = 22;     // segmentos para los que se diseno
        std::vector<SeqStep> steps;

        double totalDuration() const
        {
            double d = 0.0;
            for (const auto& s : steps) d += juce::jmax (0.0, s.duration);
            return d;
        }
    };

    //==============================================================================
    /** Una COREOGRAFIA creada por el usuario: un look atado a una fixtura concreta
        (o a todas). Puede ser ALGORITMICA (ShowStyle: patron + color que reacciona
        al audio) o MANUAL (secuencia de estados dibujada a mano). Por cancion se
        elige un conjunto y el Auto IA reparte las algoritmicas por energia. */
    struct Choreography
    {
        juce::String   name      { "Nueva coreografia" };
        juce::String   targetKey;        // "" = todas las fixturas; si no, fixtureKey de un equipo
        bool           manual    = false;   // true => usa 'sequence' en vez de 'style'
        ShowStyle      style;            // patron + color + parametros (algoritmica)
        ManualSequence sequence;         // estados dibujados a mano (manual)
    };

    /** Serializa un ShowStyle a ValueTree. */
    inline juce::ValueTree showStyleToTree (const ShowStyle& s)
    {
        juce::ValueTree v ("Style");
        v.setProperty ("name",       s.name, nullptr);
        v.setProperty ("color",      (int) s.color, nullptr);
        v.setProperty ("motion",     (int) s.motion, nullptr);
        v.setProperty ("colorBeats", s.colorBeats, nullptr);
        v.setProperty ("moveSpeed",  s.moveSpeed, nullptr);
        return v;
    }

    inline ShowStyle showStyleFromTree (const juce::ValueTree& v)
    {
        ShowStyle s;
        s.name       = v.getProperty ("name", s.name).toString();
        s.color      = (ColorStyle)  (int) v.getProperty ("color",  (int) s.color);
        s.motion     = (MotionStyle) (int) v.getProperty ("motion", (int) s.motion);
        s.colorBeats = (double) v.getProperty ("colorBeats", s.colorBeats);
        s.moveSpeed  = (double) v.getProperty ("moveSpeed",  s.moveSpeed);
        return s;
    }

    inline juce::ValueTree choreoToTree (const Choreography& c)
    {
        juce::ValueTree v ("Choreo");
        v.setProperty ("name",   c.name, nullptr);
        v.setProperty ("target", c.targetKey, nullptr);
        v.setProperty ("manual", c.manual, nullptr);
        v.appendChild (showStyleToTree (c.style), nullptr);

        if (c.manual)
        {
            juce::ValueTree seq ("Seq");
            seq.setProperty ("useBeats",    c.sequence.useBeats, nullptr);
            seq.setProperty ("numSegments", c.sequence.numSegments, nullptr);
            for (const auto& st : c.sequence.steps)
            {
                juce::ValueTree sn ("Step");
                sn.setProperty ("duration", st.duration, nullptr);
                // Segmentos como cadena compacta "on,r,g,b,intensity,whiteOn,white;..." por rendimiento.
                juce::StringArray segs;
                for (const auto& sg : st.segments)
                    segs.add (juce::String (sg.on ? 1 : 0) + "," + juce::String ((int) sg.r) + ","
                              + juce::String ((int) sg.g) + "," + juce::String ((int) sg.b) + ","
                              + juce::String (sg.intensity, 3) + ","
                              + juce::String (sg.whiteOn ? 1 : 0) + "," + juce::String (sg.white, 3));
                sn.setProperty ("segs", segs.joinIntoString (";"), nullptr);
                seq.appendChild (sn, nullptr);
            }
            v.appendChild (seq, nullptr);
        }
        return v;
    }

    inline Choreography choreoFromTree (const juce::ValueTree& v)
    {
        Choreography c;
        c.name      = v.getProperty ("name", c.name).toString();
        c.targetKey = v.getProperty ("target", "").toString();
        c.manual    = (bool) v.getProperty ("manual", false);
        auto st = v.getChildWithName ("Style");
        if (st.isValid()) c.style = showStyleFromTree (st);
        c.style.name = c.name;

        auto seq = v.getChildWithName ("Seq");
        if (seq.isValid())
        {
            c.sequence.useBeats    = (bool) seq.getProperty ("useBeats", true);
            c.sequence.numSegments = (int)  seq.getProperty ("numSegments", 22);
            for (int i = 0; i < seq.getNumChildren(); ++i)
            {
                auto sn = seq.getChild (i);
                SeqStep step;
                step.duration = (double) sn.getProperty ("duration", 1.0);
                juce::StringArray segs;
                segs.addTokens (sn.getProperty ("segs", "").toString(), ";", "");
                for (const auto& s : segs)
                {
                    if (s.trim().isEmpty()) continue;
                    juce::StringArray p;
                    p.addTokens (s, ",", "");
                    SegState sg;
                    sg.on        = p.size() > 0 && p[0].getIntValue() != 0;
                    sg.r         = (juce::uint8) (p.size() > 1 ? juce::jlimit (0, 255, p[1].getIntValue()) : 255);
                    sg.g         = (juce::uint8) (p.size() > 2 ? juce::jlimit (0, 255, p[2].getIntValue()) : 255);
                    sg.b         = (juce::uint8) (p.size() > 3 ? juce::jlimit (0, 255, p[3].getIntValue()) : 255);
                    sg.intensity = p.size() > 4 ? p[4].getFloatValue() : 1.0f;
                    sg.whiteOn   = p.size() > 5 && p[5].getIntValue() != 0;
                    sg.white     = p.size() > 6 ? p[6].getFloatValue() : 1.0f;
                    step.segments.push_back (sg);
                }
                c.sequence.steps.push_back (std::move (step));
            }
        }
        return c;
    }

    /** Color de un equipo en un beat dado, segun el estilo. */
    inline RGB styleColour (const ShowStyle* s, int fixtureIndex, double beat, double colorBeats)
    {
        const int step = (int) std::floor (beat / juce::jmax (0.25, colorBeats));
        const ColorStyle mode = (s != nullptr) ? s->color : ColorStyle::Cycle;

        switch (mode)
        {
            case ColorStyle::Rainbow:
            {
                float hue = std::fmod ((float) (beat * 0.05) + fixtureIndex * 0.09f, 1.0f);
                if (hue < 0.0f) hue += 1.0f;
                const auto c = juce::Colour::fromHSV (hue, 0.95f, 1.0f, 1.0f);
                return { c.getRed(), c.getGreen(), c.getBlue() };
            }
            case ColorStyle::Warm:
            {
                static const RGB warm[] = { { 255, 40, 30 }, { 255, 120, 20 }, { 255, 60, 110 }, { 255, 200, 40 } };
                return warm[((step + fixtureIndex) % 4 + 4) % 4];
            }
            case ColorStyle::Cool:
            {
                static const RGB cool[] = { { 30, 120, 255 }, { 20, 220, 220 }, { 120, 40, 230 }, { 40, 220, 120 } };
                return cool[((step + fixtureIndex) % 4 + 4) % 4];
            }
            case ColorStyle::Mono:
                return paletteColour (fixtureIndex / 3);   // un color fijo por grupo de equipos
            case ColorStyle::Cycle:
            default:
                return paletteColour (step + fixtureIndex);
        }
    }

    /** Funcion de energia (0..1) interpolada en segundos, a partir de una envolvente. */
    using EnergyFn = std::function<float (double)>;

    inline EnergyFn energyFnFor (const std::vector<float>& energy, double lengthSeconds)
    {
        return [&energy, lengthSeconds] (double seconds) -> float
        {
            if (energy.empty() || lengthSeconds <= 0.0)
                return 0.0f;
            const double frac = juce::jlimit (0.0, 1.0, seconds / lengthSeconds);
            const double fi   = frac * (double) (energy.size() - 1);
            const int    i0   = (int) std::floor (fi);
            const int    i1   = juce::jmin (i0 + 1, (int) energy.size() - 1);
            const float  t    = (float) (fi - i0);
            return energy[(size_t) i0] * (1.0f - t) + energy[(size_t) i1] * t;
        };
    }

    /** Funcion de "nivel de seccion" (0..1) estable dentro de cada parte detectada.
        Devuelve el nivel medio de la seccion que contiene ese instante; sirve para
        que el estilo/movimiento cambie por seccion (intro calmado, drop intenso) en
        vez de fluctuar compas a compas. Vacio -> null (se usa la energia normal). */
    inline EnergyFn sectionFnFor (const std::vector<TrackSection>& sections)
    {
        if (sections.empty())
            return {};

        return [&sections] (double seconds) -> float
        {
            for (const auto& s : sections)
                if (seconds >= s.startSec && seconds < s.endSec)
                    return s.level;
            return sections.back().level;   // mas alla del final, mantiene la ultima
        };
    }

    /** Tipo de seccion (TrackSection::Type) que contiene un instante; -1 si no hay. */
    inline int sectionTypeAt (const std::vector<TrackSection>& sections, double seconds)
    {
        if (sections.empty())
            return -1;
        for (const auto& s : sections)
            if (seconds >= s.startSec && seconds < s.endSec)
                return s.type;
        return sections.back().type;
    }

    /** Paletas de color POR SECCION: tipo de seccion -> colores preferidos.
        Vacio = no hay modo por seccion (se usa la paleta global). */
    using SectionPaletteMap = std::map<int, std::vector<RGB>>;

    /** Construye una funcion paleta(segundos): devuelve los colores de la seccion
        en ese instante, o el fallback (identidad automatica) si esa seccion no
        tiene colores configurados. Vacia si no hay modo por seccion. */
    inline std::function<std::vector<RGB> (double)> makePaletteAt (
        const std::vector<TrackSection>& sections,
        const SectionPaletteMap& sectionPalettes,
        const std::vector<RGB>& fallback)
    {
        if (sectionPalettes.empty())
            return {};
        return [sections, sectionPalettes, fallback] (double sec) -> std::vector<RGB>
        {
            const int ty = sectionTypeAt (sections, sec);
            const auto it = sectionPalettes.find (ty);
            if (it != sectionPalettes.end() && ! it->second.empty())
                return it->second;
            return fallback;
        };
    }

    /** Filtra transientes para evitar acumular destellos demasiado juntos. */
    inline std::vector<double> spacedFlashes (const std::vector<double>& transients, double minGap = 0.18)
    {
        std::vector<double> out;
        double last = -10.0;
        for (double t : transients)
            if (t - last > minGap) { out.push_back (t); last = t; }
        return out;
    }

    //==============================================================================
    /** Parametros de pintado de un equipo a partir de una fuente (mezcla o stem). */
    struct PaintContext
    {
        const EnergyFn*            energy  = nullptr;  // envolvente de la fuente
        const std::vector<double>* flashes = nullptr;  // transientes (espaciados)
        const std::vector<double>* accents = nullptr;  // acentos extra (p.ej. voces); puede ser null
        const EnergyFn*            sectionEnergy = nullptr;  // nivel estable por seccion (intro/drop/...); null = usa energy
        const std::vector<TrackSection>* sections = nullptr;  // secciones detectadas (intro/verso/coro/drop...); null = solo energia
        bool   isHead      = false;
        int    headIndex   = 0;
        int    headCount   = 1;     // numero total de cabezas (para escalonar las figuras de movimiento)
        int    colorOffset = 0;
        int    fixtureIndex = 0;    // posicion global del equipo (para escalonar color/fase)
        int    fixtureGroup = 0;    // grupo para patrones chase/alterno/onda
        int    groupCount   = 1;    // numero de grupos del patron
        int    orderIndex   = 0;    // posicion DENTRO de su clase (PAR/cabeza/barra) ordenada por direccion DMX
        int    orderCount   = 1;    // numero total de equipos de su clase (para patrones espaciales)
        const ShowStyle* style = nullptr;   // estilo activo (dinamica de patron/color)
        std::vector<RGB> palette;           // paleta de identidad del tema (vacia = ciclo por defecto)
        std::function<std::vector<RGB> (double)> paletteAt;  // paleta por seccion (segundos); vacia = usa 'palette'
        const std::vector<ShowStyle>* autoPool = nullptr;  // pool de coreografias del tema (Auto IA por energia)
        double lengthSeconds = 0.0;
        double beatsPerBar = 4.0;
        int    numBars     = 1;
        double bpm         = 120.0;
        double beatOffset  = 0.0;    // segundos del primer beat real (fase de la rejilla)
        std::function<double (double)> toBeats;
    };

    /** Estilo activo en un instante: si hay pool de coreografias del tema, el Auto
        IA elige una segun la energia local (calma -> intenso); si no, el estilo fijo. */
    inline const ShowStyle* activeStyleAt (const PaintContext& ctx, double beats)
    {
        if (ctx.autoPool == nullptr || ctx.autoPool->empty())
            return ctx.style;

        const double bar       = juce::jmax (1.0, ctx.beatsPerBar);
        const double barStart  = std::floor (beats / bar) * bar;
        const double secPerBeat = 60.0 / juce::jmax (1.0, ctx.bpm);
        const double sampleSec = barStart * secPerBeat + ctx.beatOffset;
        // Nivel estable por seccion si lo hay (cambio de estilo por parte), si no la energia del compas.
        const float  e = (ctx.sectionEnergy != nullptr) ? (*ctx.sectionEnergy) (sampleSec)
                       : (ctx.energy != nullptr) ? (*ctx.energy) (sampleSec) : 0.5f;
        const int    n = (int) ctx.autoPool->size();
        const int    idx = juce::jlimit (0, n - 1, (int) (juce::jlimit (0.0f, 0.9999f, e) * (float) n));
        return &(*ctx.autoPool)[(size_t) idx];
    }

    /** Rellena los canales de un equipo segun el contexto. */
    inline void paintFixture (Fixture& f, const PaintContext& ctx)
    {
        const float  dimFloor = 0.04f;   // suelo bajo: contraste fuerte (casi negro en lo flojo)
        const double dimStep  = 0.08;    // resolucion fina de la envolvente
        const double secPerBeat = 60.0 / juce::jmax (1.0, ctx.bpm);

        // Caida de un destello (en beats): rapida para que "parpadee" de verdad.
        const double flashDecaySec = 0.14;

        const double moveSpeed = (ctx.style != nullptr) ? juce::jmax (0.25, ctx.style->moveSpeed) : 1.0;

        // Patron de encendido/apagado (chase/alterno/onda/pulso) que modula la base.
        auto gate = [&] (double beats) -> float
        {
            // Estilo activo: pool de coreografias del tema (si lo hay) o el fijo.
            const ShowStyle* st = activeStyleAt (ctx, beats);
            if (st == nullptr)
                return 1.0f;

            const int g = juce::jmax (1, ctx.groupCount);
            const int b = (int) std::floor (beats);

            // Resuelve el modo Auto por seccion: mira la energia al inicio del compas.
            MotionStyle m = st->motion;
            if (m == MotionStyle::Auto)
            {
                const double bar = juce::jmax (1.0, ctx.beatsPerBar);
                const double barStart = std::floor (beats / bar) * bar;
                const double sampleSec = barStart * secPerBeat + ctx.beatOffset;
                const float  eBar  = (ctx.sectionEnergy != nullptr) ? (*ctx.sectionEnergy) (sampleSec)
                                   : (ctx.energy != nullptr) ? (*ctx.energy) (sampleSec) : 0.5f;
                const int    phrase = (int) std::floor (beats / (bar * 2.0));
                // Si conocemos el TIPO de seccion (coro/drop/verso...) lo usamos para
                // que el coro sea pleno y al tempo (no estrobo) y el drop sea caotico.
                const int    sty = (ctx.sections != nullptr) ? sectionTypeAt (*ctx.sections, sampleSec) : -1;
                m = (sty >= 0) ? motionForSection (sty, eBar, phrase) : motionForEnergy (eBar, phrase);
            }

            // Posicion normalizada del grupo (0..1) para patrones espaciales.
            const double gpos = (g > 1) ? (double) (ctx.fixtureGroup % g) / (g - 1) : 0.5;

            switch (m)
            {
                case MotionStyle::Chase:
                {
                    const int active = ((b % g) + g) % g;
                    return (active == (ctx.fixtureGroup % g)) ? 1.0f : 0.12f;
                }
                case MotionStyle::Alternate:
                    return (((b + ctx.fixtureGroup) % 2) == 0) ? 1.0f : 0.14f;
                case MotionStyle::Wave:
                {
                    const double ph = beats * 0.6 - ctx.fixtureGroup * 0.7;
                    return 0.22f + 0.78f * (0.5f + 0.5f * (float) std::sin (ph));
                }
                case MotionStyle::Pulse:
                {
                    const double frac = beats - std::floor (beats);
                    return 0.18f + 0.82f * (float) std::pow (1.0 - frac, 2.2);
                }
                case MotionStyle::Bounce:
                {
                    // Cabeza que va y vuelve (ping-pong) por los grupos.
                    const double tri  = std::abs (std::fmod (beats * 0.5 * moveSpeed, 2.0) - 1.0);
                    const double d    = gpos - tri;
                    return 0.12f + 0.88f * (float) std::exp (-(d * d) / (2.0 * 0.20 * 0.20));
                }
                case MotionStyle::Build:
                {
                    // Se van encendiendo grupos hasta llenar, luego reinicia.
                    const double level = std::fmod (beats * 0.25 * moveSpeed, 1.0);
                    return (gpos <= level + 0.001) ? 1.0f : 0.12f;
                }
                case MotionStyle::Sparkle:
                {
                    const float n = hashNoise (ctx.fixtureGroup, (int) std::floor (beats * 2.0 * moveSpeed));
                    return (n > 0.55f) ? 1.0f : 0.10f;
                }
                case MotionStyle::Bloom:
                {
                    // Florece del centro hacia fuera.
                    const double dist  = std::abs (gpos - 0.5) * 2.0;
                    const double front = std::fmod (beats * 0.5 * moveSpeed, 1.0);
                    return (dist <= front + 0.001) ? 1.0f : 0.12f;
                }
                case MotionStyle::Symmetry:
                {
                    // Onda simetrica desde el centro (espejo).
                    const double pm = std::abs (gpos - 0.5) * 2.0;
                    const double ph = beats * 0.9 * moveSpeed - pm * 3.0;
                    return 0.20f + 0.80f * (0.5f + 0.5f * (float) std::sin (ph));
                }
                case MotionStyle::Strobe:
                {
                    const double ph = std::fmod (beats * 4.0 * moveSpeed, 1.0);
                    return (ph < 0.5) ? 1.0f : 0.06f;
                }
                case MotionStyle::Stack:
                {
                    // Apila desde ambos extremos hacia el centro.
                    const double level = std::fmod (beats * 0.25 * moveSpeed, 1.0);
                    const double dist  = std::abs (gpos - 0.5) * 2.0;
                    return (dist >= 1.0 - level - 0.001) ? 1.0f : 0.12f;
                }
                case MotionStyle::Theater:
                {
                    // Persecucion con huecos (1 de cada 3 grupos).
                    const int phase = (((ctx.fixtureGroup + (int) std::floor (beats * moveSpeed)) % 3) + 3) % 3;
                    return (phase == 0) ? 1.0f : 0.12f;
                }
                case MotionStyle::Ripple:
                {
                    // Anillo que crece desde el centro cada beat.
                    const double frac = beats - std::floor (beats);
                    const double dist = std::abs (gpos - 0.5) * 2.0;
                    float ring = 1.0f - (float) (std::abs (dist - frac) / 0.30);
                    return 0.12f + 0.88f * juce::jlimit (0.0f, 1.0f, ring);
                }
                case MotionStyle::Random:
                {
                    const int active = (int) (hashNoise ((int) std::floor (beats * moveSpeed), 7) * g);
                    return ((active % g) == (ctx.fixtureGroup % g)) ? 1.0f : 0.12f;
                }
                case MotionStyle::Auto:
                case MotionStyle::Unison:
                default:
                    return 1.0f;
            }
        };

        // Movimiento de cabezas (Pan/Tilt) por FIGURAS de coreografia. Una unica
        // fuente de verdad (MotionFigure.h) compartida con el visualizador 2.5D. La
        // figura se elige por SECCION/energia (o se fuerza desde el estilo) y se
        // mantiene estable; la energia modula la amplitud; tiempo MUSICAL (beats) y
        // frecuencia constante -> sin tirones, los motores nunca corren de golpe.
        auto buildMovement = [&] (std::vector<Keyframe>& kf, bool isPan)
        {
            kf.clear();
            const double totalBeats = ctx.toBeats (ctx.lengthSeconds) + ctx.beatsPerBar;
            const double step    = 0.25;                              // resolucion en beats
            const int    index   = ctx.headIndex;
            const int    count   = juce::jmax (2, ctx.headCount);
            const motionfig::Figure forced = (ctx.style != nullptr) ? ctx.style->moveFigure
                                                                    : motionfig::Figure::Auto;

            auto energyAt = [&] (double beats) -> float
            {
                const double sec = beats * secPerBeat + ctx.beatOffset;
                if (ctx.energy != nullptr) return juce::jlimit (0.0f, 1.0f, (*ctx.energy) (sec));
                return 0.5f;
            };

            // Figura activa: si el estilo fuerza una, esa; si es Auto se construye
            // una LINEA DE TIEMPO de figuras estables que duran SECUENCIAS largas
            // (varios compases) para que completen sus ciclos y se vean melodicas, y
            // solo cambian al cambiar de SECCION (con histeresis) o tras un maximo;
            // NUNCA por beat ni por pico.
            const double moveSpd = (ctx.style != nullptr) ? juce::jmax (0.25, ctx.style->moveSpeed) : 1.0;
            const double bar     = juce::jmax (1.0, ctx.beatsPerBar);

            // Nivel ESTABLE de seccion: usa el nivel por seccion si lo hay; si no,
            // promedia la envolvente sobre el compas (no fluctua por beat ni pico).
            auto sectionLevelAt = [&] (double beats) -> float
            {
                if (ctx.sectionEnergy != nullptr)
                {
                    const double sampleSec = beats * secPerBeat + ctx.beatOffset;
                    return juce::jlimit (0.0f, 1.0f, (*ctx.sectionEnergy) (sampleSec));
                }
                float sum = 0.0f; int n = 0;
                for (double bb = beats; bb < beats + bar; bb += 0.5) { sum += energyAt (bb); ++n; }
                return n > 0 ? sum / (float) n : 0.5f;
            };

            // Banda de energia (coincide con los tramos de figureForEnergy) -> detecta
            // CAMBIOS DE SECCION, no de beat.
            auto energyBand = [] (float e) -> int
            {
                if (e < 0.15f) return 0;
                if (e < 0.30f) return 1;
                if (e < 0.48f) return 2;
                if (e < 0.66f) return 3;
                if (e < 0.82f) return 4;
                return 5;
            };

            // Tipo de seccion (coro/drop/verso...) en un instante; -1 si no se conoce.
            auto secTypeAt = [&] (double beats) -> int
            {
                if (ctx.sections == nullptr) return -1;
                return sectionTypeAt (*ctx.sections, beats * secPerBeat + ctx.beatOffset);
            };
            // Elige figura: si conocemos el tipo de seccion, por seccion (coro amplio,
            // drop caotico); si no, por banda de energia.
            auto pickFig = [&] (double beats, int variant) -> motionfig::Figure
            {
                const int sty = secTypeAt (beats);
                return (sty >= 0) ? figureForSection (sty, sectionLevelAt (beats), variant)
                                  : motionfig::figureForEnergy (sectionLevelAt (beats), variant);
            };

            struct Seg { double start; motionfig::Figure fig; };
            std::vector<Seg> segs;
            {
                const double minBeats = bar * 8.0;    // una figura dura >= 8 compases
                const double maxBeats = bar * 24.0;   // variedad como muy tarde a los 24
                int    variant  = 0;
                double segStart = 0.0;
                int    curBand  = energyBand (sectionLevelAt (0.0));
                int    curSec   = secTypeAt (0.0);
                motionfig::Figure curFig = (forced != motionfig::Figure::Auto)
                    ? forced : pickFig (0.0, variant);
                segs.push_back ({ 0.0, curFig });

                if (forced == motionfig::Figure::Auto)
                {
                    for (double b = bar; b <= totalBeats; b += bar)
                    {
                        const int    bnd     = energyBand (sectionLevelAt (b));
                        const int    sty     = secTypeAt (b);
                        const double held    = b - segStart;
                        // Cambio de figura al cambiar de TIPO de seccion (si lo hay) o de
                        // banda de energia, siempre con un minimo de duracion (histeresis).
                        const bool   changed = (sty >= 0) ? (sty != curSec) : (bnd != curBand);
                        const bool   section = changed && held >= minBeats;          // cambio de seccion
                        const bool   refresh = held >= maxBeats;                       // evita monotonia
                        if (section || refresh)
                        {
                            ++variant;
                            curBand  = bnd;
                            curSec   = sty;
                            segStart = b;
                            curFig   = pickFig (b, variant);
                            segs.push_back ({ b, curFig });
                        }
                    }
                }
            }

            // Muestra la figura activa con un EMPALME suave de 1 compas (cross-fade)
            // entre figuras consecutivas para que el cambio no de tirones.
            auto sampleAt = [&] (double b, float e) -> motionfig::Sample
            {
                size_t si = 0;
                while (si + 1 < segs.size() && segs[si + 1].start <= b) ++si;
                motionfig::Sample s = motionfig::eval (segs[si].fig, b, e, index, count, moveSpd);
                const double xf = bar;   // 1 compas de mezcla
                if (si > 0 && (b - segs[si].start) < xf)
                {
                    const float w = (float) ((b - segs[si].start) / xf);   // 0..1
                    const motionfig::Sample p = motionfig::eval (segs[si - 1].fig, b, e, index, count, moveSpd);
                    s.pan  = p.pan  + (s.pan  - p.pan)  * w;
                    s.tilt = p.tilt + (s.tilt - p.tilt) * w;
                }
                return s;
            };

            for (double b = 0.0; b <= totalBeats; b += step)
            {
                const float e = energyAt (b);
                const motionfig::Sample s = sampleAt (b, e);
                const float v = (isPan ? s.pan : s.tilt) * 255.0f;
                kf.push_back ({ b, juce::jlimit (0.0f, 255.0f, v), false });   // SIEMPRE rampa
            }

            sortKeyframes (kf);
        };

        for (auto& chan : f.channels)
        {
            std::vector<Keyframe>& kf = chan.keyframes;

            switch (chan.type)
            {
                case ChannelType::Dimmer:
                case ChannelType::White:
                {
                    // Envolvente base con contraste (mas oscuro en lo bajo) modulada por el patron.
                    auto contrast = [] (float e) { return std::pow (juce::jlimit (0.0f, 1.0f, e), 1.8f); };

                    for (double t = 0.0; t <= ctx.lengthSeconds; t += dimStep)
                    {
                        const double beats = ctx.toBeats (t);
                        const float  e = juce::jmax (dimFloor, contrast ((*ctx.energy) (t)) * gate (beats));
                        kf.push_back ({ beats, e * 255.0f, false });
                    }

                    // Destellos NITIDOS: mantiene la base, salta a tope y cae rapido.
                    auto addFlash = [&] (double t, float peak)
                    {
                        const float gk   = gate (ctx.toBeats (t));
                        const float base = juce::jmax (dimFloor, contrast ((*ctx.energy) (t)) * gk) * 255.0f;
                        kf.push_back ({ ctx.toBeats (t) - 0.0015,        base,      true });           // mantiene la base hasta el golpe
                        kf.push_back ({ ctx.toBeats (t),                 peak * gk, false });          // SNAP arriba
                        kf.push_back ({ ctx.toBeats (t + flashDecaySec), base,      false });          // caida rapida
                    };

                    if (ctx.flashes != nullptr)
                        for (double t : *ctx.flashes)
                            addFlash (t, 255.0f);
                    if (ctx.accents != nullptr)
                        for (double t : *ctx.accents)
                            addFlash (t, 235.0f);

                    sortKeyframes (kf);
                    break;
                }

                case ChannelType::Red:
                case ChannelType::Green:
                case ChannelType::Blue:
                {
                    // Color segun el estilo (ciclo/arcoiris/calido/frio/mono) + offset por equipo.
                    const double totalBeats = ctx.toBeats (ctx.lengthSeconds) + ctx.beatsPerBar;
                    const int    numBeats   = juce::jmax (1, (int) std::ceil (totalBeats));
                    const bool   hasPool    = (ctx.autoPool != nullptr && ! ctx.autoPool->empty());

                    const double baseCB     = (ctx.style != nullptr) ? juce::jmax (0.25, ctx.style->colorBeats) : 1.0;
                    const double colorBeats0 = ctx.isHead ? baseCB * 2.0 : baseCB;   // cabezas algo mas lento
                    const int    stepBeats  = hasPool ? 1 : juce::jmax (1, (int) std::round (colorBeats0));

                    // Energia (nivel de seccion si lo hay) en un beat dado, para el auto-color.
                    auto sampleE = [&] (double beats) -> float
                    {
                        const double sec = beats * secPerBeat + ctx.beatOffset;
                        if (ctx.sectionEnergy != nullptr) return (*ctx.sectionEnergy) (sec);
                        if (ctx.energy != nullptr)        return (*ctx.energy) (sec);
                        return 0.6f;
                    };

                    for (int beat = 0; beat < numBeats; beat += stepBeats)
                    {
                        // Estilo activo para este compas (pool del tema o estilo fijo).
                        const ShowStyle* st = activeStyleAt (ctx, (double) beat);
                        const double cb = ctx.isHead
                                            ? juce::jmax (0.25, (st != nullptr ? st->colorBeats : 1.0)) * 2.0
                                            : juce::jmax (0.25, (st != nullptr ? st->colorBeats : 1.0));
                        const bool smoothB = (st != nullptr && st->color == ColorStyle::Rainbow);
                        const bool isAuto  = (st != nullptr && st->color == ColorStyle::Auto);

                        // Paleta efectiva en este beat: por seccion si esta activo, si no la global.
                        std::vector<RGB> palBuf;
                        const std::vector<RGB>* palPtr = &ctx.palette;
                        if (ctx.paletteAt)
                        {
                            const double secAtBeat = (double) beat * secPerBeat + ctx.beatOffset;
                            palBuf = ctx.paletteAt (secAtBeat);
                            palPtr = &palBuf;
                        }
                        const std::vector<RGB>& pal = *palPtr;

                        const bool useIdentity = (! pal.empty())
                                               && (st == nullptr || st->color == ColorStyle::Cycle);

                        RGB col;
                        if (isAuto)
                        {
                            // Identidad tonal del tema modulada por la energia de la seccion.
                            const RGB baseCol = (! pal.empty())
                                ? identityColour (pal, ctx.fixtureIndex, (double) beat, cb)
                                : styleColour (st, ctx.colorOffset, (double) beat, cb);
                            col = energyColour (baseCol, sampleE ((double) beat));
                        }
                        else
                        {
                            col = useIdentity
                                ? identityColour (pal, ctx.fixtureIndex, (double) beat, cb)
                                : styleColour (st, ctx.colorOffset, (double) beat, cb);
                        }
                        const float v = (chan.type == ChannelType::Red)   ? col.r
                                      : (chan.type == ChannelType::Green) ? col.g
                                                                          : col.b;
                        kf.push_back ({ (double) beat, v, ! smoothB });   // step salvo arcoiris (rampa suave)
                    }
                    break;
                }

                case ChannelType::Strobe:
                case ChannelType::Shutter:
                {
                    // Strobe real en los golpes con energia media-alta (umbral mas bajo).
                    if (ctx.flashes != nullptr)
                    {
                        for (double t : *ctx.flashes)
                        {
                            if ((*ctx.energy) (t) < 0.40f)
                                continue;
                            kf.push_back ({ ctx.toBeats (t),        255.0f, true });
                            kf.push_back ({ ctx.toBeats (t + 0.05), 0.0f,   true });
                        }
                        sortKeyframes (kf);
                    }
                    break;
                }

                case ChannelType::Pan:
                {
                    buildMovement (kf, /*isPan*/ true);
                    break;
                }

                case ChannelType::Tilt:
                {
                    buildMovement (kf, /*isPan*/ false);
                    break;
                }

                default:
                    break;
            }
        }

        juce::ignoreUnused (secPerBeat);
    }

    //==============================================================================
    // Motor dedicado de FOCOS PAR: fiesta total. Patrones de encendido/apagado
    // DUROS (unos a negro mientras otros a tope con su color), persecuciones y
    // SUBIDONES con estrobos que aceleran con la energia y el tempo segun la
    // seccion/hype de la cancion. Pensado para luces par RGB(W)/dimmer simples.
    //==============================================================================
    inline void paintPar (Fixture& f, const PaintContext& ctx)
    {
        if (ctx.energy == nullptr)
            return;

        const double secPerBeat = 60.0 / juce::jmax (1.0, ctx.bpm);
        const double tEnd  = ctx.lengthSeconds + secPerBeat;
        const double dt    = 0.015;                    // fino: el estrobo necesita resolucion
        const double moveSpeed = (ctx.style != nullptr) ? juce::jmax (0.25, ctx.style->moveSpeed) : 1.0;

        // Posicion FISICA del foco dentro de la fila de PAR (ordenada por direccion
        // DMX en el rig). idx 0..n-1; pos 0..1. De aqui salen los patrones espaciales.
        const int    n   = juce::jmax (1, ctx.orderCount);
        const int    idx = juce::jlimit (0, n - 1, ctx.orderIndex);
        const double pos = (n > 1) ? (double) idx / (n - 1) : 0.5;

        const std::vector<double> emptyF;
        const std::vector<double>& flashes = (ctx.flashes != nullptr) ? *ctx.flashes : emptyF;

        // Nivel ESTABLE de seccion (intro/build/drop) -> decide el comportamiento.
        auto secLevel = [&] (double beats) -> float
        {
            const double sec = beats * secPerBeat + ctx.beatOffset;
            if (ctx.sectionEnergy != nullptr) return juce::jlimit (0.0f, 1.0f, (*ctx.sectionEnergy) (sec));
            if (ctx.energy != nullptr)        return juce::jlimit (0.0f, 1.0f, (*ctx.energy) (sec));
            return 0.5f;
        };
        // Energia INSTANTANEA -> modula el brillo y la velocidad del estrobo.
        auto enLevel = [&] (double beats) -> float
        {
            const double sec = beats * secPerBeat + ctx.beatOffset;
            return ctx.energy != nullptr ? juce::jlimit (0.0f, 1.0f, (*ctx.energy) (sec)) : 0.5f;
        };
        auto bandOf = [] (float e) -> int
        {
            if (e < 0.15f) return 0;
            if (e < 0.30f) return 1;
            if (e < 0.48f) return 2;
            if (e < 0.66f) return 3;
            if (e < 0.82f) return 4;
            return 5;
        };

        // Envolvente de golpe (transiente) para los grooves de baja/media energia.
        auto flashEnv = [&] (double t) -> float
        {
            const auto lo = std::lower_bound (flashes.begin(), flashes.end(), t - 0.25);
            float best = 0.0f;
            for (auto it = lo; it != flashes.end(); ++it)
            {
                if (*it > t) break;
                best = juce::jmax (best, (float) std::exp (-(t - *it) / 0.09));
            }
            return best;
        };

        // Catalogo de EFECTOS ESPACIALES de la fila de PAR. Cada uno usa la posicion
        // fisica (idx/pos) para crear barridos, escalados, estrobos secuenciados,
        // scanners, etc. Devuelven intensidad 0..1 ya modulada por la energia.
        enum class ParFx
        {
            Breathe, Wave, MirrorWave,
            SweepLR, SweepRL, AltOddEven,
            ChaseSingle, Scanner, ScaleUp, Theater, Marquee,
            ChasePair, RippleBeat, CenterOut, OutIn,
            SeqStrobe, BuildStrobe, FillFlush,
            Sparkle, RainPulse, ChasePairFast, FullStrobe,
            // Emotivos / al tempo (para coros y secciones melodicas):
            Anthem, Swell, Heartbeat, BeatPump, PingPong, Cascade
        };

        // Ancho de banda espacial: cubre ~1-2 focos para que el barrido se "vea".
        const double wp = juce::jmax (0.08, 1.0 / (double) n);
        const double gridGap = std::exp (-1.0 / (2.0 * 0.05 * 0.05));   // borde duro escalado

        auto evalFx = [&] (ParFx fx, double beats, double t, float ee) -> float
        {
            juce::ignoreUnused (t);
            const int    ib    = (int) std::floor (beats);
            const double frac  = beats - std::floor (beats);
            const float  bright = 0.35f + 0.65f * ee;          // brillo reactivo de un foco encendido
            const double spd   = moveSpeed;

            switch (fx)
            {
                case ParFx::Breathe:
                {
                    const float wob = 0.5f + 0.5f * (float) std::sin (beats * 0.5 - pos * 3.0);
                    return (0.10f + 0.30f * ee) * (0.5f + 0.5f * wob);
                }
                case ParFx::Wave:
                {
                    const float w = 0.5f + 0.5f * (float) std::sin (beats * 0.9 * spd - pos * 6.2831853);
                    return (0.15f + 0.85f * w) * bright;
                }
                case ParFx::MirrorWave:
                {
                    const double pm = std::abs (pos - 0.5) * 2.0;
                    const float  w  = 0.5f + 0.5f * (float) std::sin (beats * 0.9 * spd - pm * 6.0);
                    return (0.15f + 0.85f * w) * bright;
                }
                case ParFx::SweepLR:
                {
                    const double head = std::fmod (beats * 0.5 * spd, 1.0);
                    const double d  = std::abs (pos - head);
                    const double dd = std::min (d, 1.0 - d);                 // envuelve por los extremos
                    return (float) std::exp (-(dd * dd) / (2.0 * wp * wp)) * bright;
                }
                case ParFx::SweepRL:
                {
                    const double head = 1.0 - std::fmod (beats * 0.5 * spd, 1.0);
                    const double d  = std::abs (pos - head);
                    const double dd = std::min (d, 1.0 - d);
                    return (float) std::exp (-(dd * dd) / (2.0 * wp * wp)) * bright;
                }
                case ParFx::AltOddEven:
                {
                    const bool on = (((idx + ib) % 2) == 0);
                    return on ? bright : 0.0f;
                }
                case ParFx::ChaseSingle:
                {
                    const int active = (((int) std::floor (beats * spd)) % n + n) % n;
                    return (idx == active) ? bright : 0.0f;
                }
                case ParFx::Scanner:
                {
                    // Larson/KITT: cabeza que rebota por la fila + estela corta.
                    const double tri = std::abs (std::fmod (beats * 0.6 * spd, 2.0) - 1.0);
                    const double d   = pos - tri;
                    return (float) std::exp (-(d * d) / (2.0 * wp * wp)) * bright;
                }
                case ParFx::ScaleUp:
                {
                    // Se llena progresivamente de un extremo al otro y reinicia.
                    const double level = std::fmod (beats * 0.25 * spd, 1.0);
                    const float  on = (pos <= level) ? 1.0f
                                    : (float) std::exp (-((pos - level) * (pos - level)) / (2.0 * 0.05 * 0.05));
                    return juce::jmax (0.0f, on - (float) gridGap) * bright;
                }
                case ParFx::Theater:
                {
                    const int phase = (((idx + (int) std::floor (beats * spd)) % 3) + 3) % 3;
                    return (phase == 0) ? bright : 0.0f;
                }
                case ParFx::Marquee:
                {
                    const int phase = (((idx + (int) std::floor (beats * spd * 2.0)) % 3) + 3) % 3;
                    return (phase == 0) ? bright : 0.0f;
                }
                case ParFx::ChasePair:
                case ParFx::ChasePairFast:
                {
                    // Cometa con estela que recorre la fila (rapido en la variante Fast).
                    const double k = (fx == ParFx::ChasePairFast) ? 1.0 : 0.5;
                    const double headF = std::fmod (beats * k * spd, 1.0) * n;
                    double td = headF - idx;                                  // distancia detras de la cabeza
                    if (td < 0) td += n;                                      // envuelve
                    const float tail = (td < 3.0) ? (float) std::exp (-td / 1.1) : 0.0f;
                    return tail * bright;
                }
                case ParFx::RippleBeat:
                {
                    const double dist = std::abs (pos - 0.5) * 2.0;          // 0 centro .. 1 extremo
                    const float  ring = 1.0f - (float) (std::abs (dist - frac) / 0.25);
                    return juce::jlimit (0.0f, 1.0f, ring) * bright;
                }
                case ParFx::CenterOut:
                {
                    const double dist  = std::abs (pos - 0.5) * 2.0;
                    const double front = std::fmod (beats * 0.5 * spd, 1.0);
                    const float  on = (dist <= front) ? 1.0f
                                    : (float) std::exp (-((dist - front) * (dist - front)) / (2.0 * 0.05 * 0.05));
                    return juce::jmax (0.0f, on - (float) gridGap) * bright;
                }
                case ParFx::OutIn:
                {
                    const double dist  = std::abs (pos - 0.5) * 2.0;
                    const double level = std::fmod (beats * 0.5 * spd, 1.0);
                    const float  on = (dist >= 1.0 - level) ? 1.0f
                                    : (float) std::exp (-((1.0 - level - dist) * (1.0 - level - dist)) / (2.0 * 0.05 * 0.05));
                    return juce::jmax (0.0f, on - (float) gridGap) * bright;
                }
                case ParFx::SeqStrobe:
                {
                    // Estrobo SECUENCIADO: cada foco dispara con un desfase segun su
                    // posicion -> una ola de destellos que recorre la fila.
                    double rate = juce::jlimit (1.0, 6.0, std::floor (2.0 + ee * 4.0));
                    const double ph = beats * rate - pos;
                    const float on = (std::fmod (ph, 1.0) < 0.5) ? 1.0f : 0.0f;
                    return on * (0.4f + 0.6f * ee);
                }
                case ParFx::BuildStrobe:
                {
                    // Subidon: la fila se llena mientras un estrobo ACELERA con el hype.
                    const double level = std::fmod (beats * 0.5 * spd, 1.0);
                    const float  filled = (pos <= level) ? 1.0f : 0.0f;
                    double rate = juce::jlimit (1.0, 6.0, std::floor (1.0 + ee * 5.0));
                    const float  strobe = (std::fmod (beats * rate, 1.0) < 0.5) ? 1.0f : 0.25f;
                    return filled * strobe * (0.4f + 0.6f * ee);
                }
                case ParFx::FillFlush:
                {
                    // Llena, destella todo, vacia: ciclo dramatico para subidas.
                    const double cyc = std::fmod (beats * 0.25 * spd, 1.0);
                    float on = 0.0f;
                    if (cyc < 0.40)       on = (pos <= cyc / 0.40) ? 1.0f : 0.0f;        // llena
                    else if (cyc < 0.55)  on = 1.0f;                                     // flash total
                    else                  on = (pos >= (cyc - 0.55) / 0.45) ? 1.0f : 0.0f; // vacia
                    return on * bright;
                }
                case ParFx::Sparkle:
                {
                    const float ph = (float) (beats * 5.0 * spd) + hashNoise (idx, 0) * 6.2832f;
                    const float tw = std::pow (0.5f + 0.5f * std::sin (ph), 5.0f);
                    return tw * bright;
                }
                case ParFx::RainPulse:
                {
                    // En cada beat "cae" una gota en un foco al azar y decae.
                    const int   chosen = (int) (hashNoise (ib, 5) * n);
                    const int   chosen2 = (int) (hashNoise (ib, 9) * n);
                    const float env = (float) std::exp (-frac / 0.20);
                    return ((idx == (chosen % n)) || (idx == (chosen2 % n))) ? env * bright : 0.0f;
                }
                case ParFx::FullStrobe:
                {
                    double rate = juce::jlimit (2.0, 8.0, std::floor (3.0 + ee * 5.0));
                    const float on = (std::fmod (beats * rate, 1.0) < 0.5) ? 1.0f : 0.0f;
                    return on * (0.4f + 0.6f * ee);
                }
                case ParFx::Anthem:
                {
                    // CORO: toda la fila encendida con un latido SUAVE al ritmo. Sostenido
                    // y luminoso (auge emotivo), nunca estrobo. Pico limpio al inicio del beat.
                    const float pulse = 0.74f + 0.26f * (float) std::pow (juce::jmax (0.0, 1.0 - frac), 1.5);
                    return pulse * (0.62f + 0.38f * ee);
                }
                case ParFx::Swell:
                {
                    // Respiracion lenta de TODA la fila sincronizada al compas: crece y se
                    // abre como el coro de una cancion. Muy melodico.
                    const double barL = juce::jmax (1.0, ctx.beatsPerBar);
                    const float  s = 0.5f + 0.5f * (float) std::sin (beats / barL * 6.2831853 - 1.5707963);
                    return (0.46f + 0.54f * s) * (0.6f + 0.4f * ee);
                }
                case ParFx::Heartbeat:
                {
                    // Lub-dub: dos latidos juntos por compas en toda la fila (emocion),
                    // sobre una base sostenida tenue. Hace "sentir" la cancion.
                    const double barL = juce::jmax (1.0, ctx.beatsPerBar);
                    const double ph  = std::fmod (beats, barL) / barL;          // 0..1 en el compas
                    auto thump = [] (double x) { return std::exp (-(x * x) / (2.0 * 0.045 * 0.045)); };
                    float h = (float) juce::jmax (thump (ph), thump (ph - 0.16));
                    h = juce::jmax (h, 0.30f);
                    return h * (0.6f + 0.4f * ee);
                }
                case ParFx::BeatPump:
                {
                    // Toda la fila pulsa UNA vez por beat, limpio y al tempo (no estrobo
                    // rapido). Sensacion de bombeo musical para coros con pegada.
                    const float p = (float) std::pow (juce::jmax (0.0, 1.0 - frac / 0.6), 2.0);
                    return (0.28f + 0.72f * p) * (0.55f + 0.45f * ee);
                }
                case ParFx::PingPong:
                {
                    // Bloque luminoso ancho que rebota suave de extremo a extremo.
                    const double tri = std::abs (std::fmod (beats * 0.5 * spd, 2.0) - 1.0);
                    const double d   = pos - tri;
                    const double ww  = wp * 1.7;
                    return (float) std::exp (-(d * d) / (2.0 * ww * ww)) * bright;
                }
                case ParFx::Cascade:
                {
                    // Cascada continua: un frente recorre la fila dejando una estela suave.
                    const double level  = std::fmod (beats * 0.5 * spd, 1.0);
                    const double behind = level - pos;
                    if (behind < 0.0) return 0.12f * bright;
                    return (0.25f + 0.75f * (float) std::exp (-behind / 0.30)) * bright;
                }
                default:
                    return bright;
            }
        };

        // Selector de efecto por BANDA de energia (calma -> caos) con 3-5 variantes
        // por banda que rotan por FRASE (cada 2 compases) -> variedad enorme sin caos.
        auto pickFx = [] (int band, int phrase) -> ParFx
        {
            auto choose = [&] (std::initializer_list<ParFx> opts) -> ParFx
            {
                const int cnt = (int) opts.size();
                const int v = ((phrase % cnt) + cnt) % cnt;
                return *(opts.begin() + v);
            };
            switch (band)
            {
                case 0:  return choose ({ ParFx::Breathe, ParFx::Wave, ParFx::MirrorWave });
                case 1:  return choose ({ ParFx::SweepLR, ParFx::SweepRL, ParFx::Wave, ParFx::AltOddEven });
                case 2:  return choose ({ ParFx::ChaseSingle, ParFx::Scanner, ParFx::ScaleUp, ParFx::Theater, ParFx::SweepLR });
                case 3:  return choose ({ ParFx::ChasePair, ParFx::RippleBeat, ParFx::CenterOut, ParFx::OutIn, ParFx::Marquee });
                case 4:  return choose ({ ParFx::SeqStrobe, ParFx::BuildStrobe, ParFx::FillFlush, ParFx::Scanner, ParFx::ChasePair });
                default: return choose ({ ParFx::SeqStrobe, ParFx::Sparkle, ParFx::RainPulse, ParFx::ChasePairFast, ParFx::FullStrobe });
            }
        };

        // Tipo de seccion (coro/drop/verso...) en un instante; -1 si no se conoce.
        auto secType = [&] (double beats) -> int
        {
            if (ctx.sections == nullptr) return -1;
            return sectionTypeAt (*ctx.sections, beats * secPerBeat + ctx.beatOffset);
        };

        // Selector de efecto POR TIPO DE SECCION: aqui esta la clave del coro emotivo.
        //  - Coro: efectos PLENOS y al tempo (himno/respiracion/latido), nunca estrobo.
        //  - Drop: el caos estroboscopico de verdad.
        //  - Build: llenados que crecen. Verso: grooves. Intro/Break/Outro: calma.
        // Si no se conoce el tipo, cae al selector por banda de energia.
        auto pickFxSection = [&] (int sty, int band, int phrase) -> ParFx
        {
            auto choose = [&] (std::initializer_list<ParFx> opts) -> ParFx
            {
                const int cnt = (int) opts.size();
                const int v = ((phrase % cnt) + cnt) % cnt;
                return *(opts.begin() + v);
            };
            switch (sty)
            {
                case TrackSection::Chorus:
                    return choose ({ ParFx::Anthem, ParFx::Swell, ParFx::Heartbeat,
                                     ParFx::BeatPump, ParFx::Wave, ParFx::CenterOut });
                case TrackSection::Drop:
                    return choose ({ ParFx::SeqStrobe, ParFx::BuildStrobe, ParFx::FullStrobe,
                                     ParFx::ChasePairFast, ParFx::Sparkle, ParFx::RainPulse });
                case TrackSection::Build:
                    return choose ({ ParFx::ScaleUp, ParFx::BuildStrobe, ParFx::FillFlush,
                                     ParFx::CenterOut, ParFx::Cascade });
                case TrackSection::Verse:
                    return choose ({ ParFx::ChaseSingle, ParFx::Scanner, ParFx::Wave,
                                     ParFx::AltOddEven, ParFx::Theater, ParFx::PingPong });
                case TrackSection::Break:
                    return choose ({ ParFx::Breathe, ParFx::Swell, ParFx::MirrorWave, ParFx::Wave });
                case TrackSection::Intro:
                case TrackSection::Outro:
                    return choose ({ ParFx::Breathe, ParFx::Wave, ParFx::MirrorWave, ParFx::Swell });
                default:
                    return pickFx (band, phrase);   // sin secciones: por energia
            }
        };

        // Intensidad 0..1 del FOCO: elige el efecto por TIPO de seccion (con fallback a
        // energia) y lo evalua en la posicion fisica de este foco.
        auto parInten = [&] (double beats, double t) -> float
        {
            const float se = secLevel (beats);
            const float ee = enLevel (beats);
            const int   b  = bandOf (se);
            const double bar = juce::jmax (1.0, ctx.beatsPerBar);
            const int   phrase = (int) std::floor (beats / (bar * 2.0));
            const int   sty = secType (beats);

            float v = evalFx (pickFxSection (sty, b, phrase), beats, t, ee);

            // Solo en DROP (o, sin secciones, en bandas altas) los transientes pinchan
            // toda la fila. El coro se mantiene MUSICAL, sin pinchazos estroboscopicos.
            if (sty == TrackSection::Drop || (sty < 0 && b >= 4))
                v = juce::jmax (v, flashEnv (t) * (0.45f + 0.55f * ee));

            return juce::jlimit (0.0f, 1.0f, v);
        };

        // Color saturado (a tope) por beat segun el estilo activo. Reutiliza la
        // identidad/paleta del tema igual que el resto de equipos.
        auto colourAtBeat = [&] (double beat) -> RGB
        {
            const ShowStyle* st = activeStyleAt (ctx, beat);
            const double cb = (st != nullptr) ? juce::jmax (0.25, st->colorBeats) : 1.0;
            std::vector<RGB> palBuf;
            const std::vector<RGB>* palPtr = &ctx.palette;
            if (ctx.paletteAt)
            {
                palBuf = ctx.paletteAt (beat * secPerBeat + ctx.beatOffset);
                palPtr = &palBuf;
            }
            const std::vector<RGB>& pal = *palPtr;
            const ColorStyle cm = (st != nullptr) ? st->color : ColorStyle::Cycle;
            if (! pal.empty() && (cm == ColorStyle::Auto || cm == ColorStyle::Cycle))
                return identityColour (pal, ctx.fixtureIndex, beat, cb);
            return styleColour (st, ctx.colorOffset, beat, cb);
        };

        // El foco tiene dimmer? Entonces el color va a tope y el dimmer manda la
        // intensidad (apagados nitidos); si no, el RGB carga la intensidad.
        bool hasDim = false, hasRGB = false;
        for (const auto& c : f.channels)
        {
            if (c.type == ChannelType::Dimmer) hasDim = true;
            if (c.type == ChannelType::Red || c.type == ChannelType::Green || c.type == ChannelType::Blue) hasRGB = true;
        }

        // --- Bakeo con compresion por delta (igual que la barra de pixeles). ---
        const int nCh = f.channelCount();
        std::vector<bool>   started (nCh, false);
        std::vector<float>  pushedV (nCh, 0.0f);
        std::vector<double> pushedT (nCh, 0.0);
        std::vector<float>  lastV   (nCh, 0.0f);
        std::vector<double> lastT   (nCh, 0.0);
        auto feed = [&] (int ci, double beats, float v)
        {
            auto& kf = f.channels[(size_t) ci].keyframes;
            if (! started[ci])
            {
                kf.push_back ({ beats, v, false });
                started[ci] = true; pushedV[ci] = v; pushedT[ci] = beats;
                lastV[ci] = v; lastT[ci] = beats;
                return;
            }
            if (std::abs (v - pushedV[ci]) >= 3.0f)
            {
                if (lastT[ci] > pushedT[ci] + 1.0e-9 && std::abs (lastV[ci] - pushedV[ci]) < 3.0f)
                    kf.push_back ({ lastT[ci], lastV[ci], false });
                kf.push_back ({ beats, v, false });
                pushedV[ci] = v; pushedT[ci] = beats;
            }
            lastV[ci] = v; lastT[ci] = beats;
        };

        for (double t = 0.0; t <= tEnd; t += dt)
        {
            const double beats = ctx.toBeats (t);
            const float  inten = juce::jlimit (0.0f, 1.0f, parInten (beats, t));
            const RGB    col   = colourAtBeat (std::floor (beats));   // color estable por beat

            for (int ci = 0; ci < nCh; ++ci)
            {
                switch (f.channels[(size_t) ci].type)
                {
                    case ChannelType::Dimmer:
                        feed (ci, beats, inten * 255.0f);
                        break;
                    case ChannelType::Red:
                        feed (ci, beats, (float) col.r * (hasDim ? 1.0f : inten));
                        break;
                    case ChannelType::Green:
                        feed (ci, beats, (float) col.g * (hasDim ? 1.0f : inten));
                        break;
                    case ChannelType::Blue:
                        feed (ci, beats, (float) col.b * (hasDim ? 1.0f : inten));
                        break;
                    case ChannelType::White:
                        feed (ci, beats, hasRGB ? 0.0f : inten * 255.0f);   // blanco solo si no hay RGB
                        break;
                    case ChannelType::Amber:
                    case ChannelType::UV:
                        feed (ci, beats, hasRGB ? 0.0f : inten * 255.0f);   // ambar/uv como blanco si no hay RGB
                        break;
                    default:
                        // Estrobo, Macro/Color, velocidad de macro y demas: SIEMPRE a 0.
                        // El estrobo se hace por DIMMER (sincronizado al tempo); el canal
                        // de macro debe quedar apagado o anularia el control RGB.
                        feed (ci, beats, 0.0f);
                        break;
                }
            }
        }

        // Cola: cierra cada canal con su ultimo valor.
        for (int ci = 0; ci < nCh; ++ci)
            if (started[ci] && lastT[ci] > pushedT[ci] + 1.0e-9)
                f.channels[(size_t) ci].keyframes.push_back ({ lastT[ci], lastV[ci], false });
    }

    //==============================================================================
    // Motor de PIXELES para barras LED segmentadas (modo de muchos canales).
    //==============================================================================

    /** Disposicion de una barra de pixeles: lista de secciones RGB (indices de
        canal R,G,B) y de secciones de blanco (indice de canal). Se deduce de los
        tipos de canal del fixture, sin depender del nombre. */
    struct PixelLayout
    {
        std::vector<std::array<int, 3>> rgb;    // {R,G,B} (indices 0-based) por seccion
        std::vector<int>                white;  // canal de blanco por seccion
        bool isPixelBar() const { return rgb.size() >= 3 || white.size() >= 3; }
    };

    inline PixelLayout detectPixelLayout (const Fixture& f)
    {
        PixelLayout L;
        const int n = f.channelCount();
        int i = 0;
        while (i < n)
        {
            const auto t = f.channels[(size_t) i].type;
            if (t == ChannelType::Red && i + 2 < n
                && f.channels[(size_t) (i + 1)].type == ChannelType::Green
                && f.channels[(size_t) (i + 2)].type == ChannelType::Blue)
            {
                L.rgb.push_back ({ i, i + 1, i + 2 });
                i += 3;
            }
            else if (t == ChannelType::White)
            {
                L.white.push_back (i);
                i += 1;
            }
            else
            {
                i += 1;   // ignora maestro/strobe/otros en este modo
            }
        }
        return L;
    }

    /** Genera la coreografia por SECCIONES de una barra de pixeles: barridos,
        olas, fades, persecuciones y pulsos espaciales, reactivos al stem. */
    inline void paintPixelBar (Fixture& f, const PaintContext& ctx, const PixelLayout& layout)
    {
        if (ctx.energy == nullptr)
            return;

        const int   nRGB   = (int) layout.rgb.size();
        const int   nW     = (int) layout.white.size();
        const double secPerBeat = 60.0 / juce::jmax (1.0, ctx.bpm);
        const double dt    = 0.04;                      // 25 keyframes/seg (se interpola a 40fps)
        const double tEnd  = ctx.lengthSeconds + secPerBeat;

        const MotionStyle baseMotion = (ctx.style != nullptr) ? ctx.style->motion : MotionStyle::Unison;

        // Destellos (transientes ya espaciados) de la fuente asignada, en segundos.
        const std::vector<double> emptyF;
        const std::vector<double>& flashes = (ctx.flashes != nullptr) ? *ctx.flashes : emptyF;

        // Contraste suave: la barra de pixeles debe verse VIVA, no casi negra.
        auto contrast = [] (float e) { return std::pow (juce::jlimit (0.0f, 1.0f, e), 1.35f); };

        // Offsets de usuario: brillo base anadido por separado a color (RGB) y a blanco.
        const float colorOffset = (ctx.style != nullptr) ? juce::jlimit (0.0f, 1.0f, ctx.style->colorOffset) : 0.0f;
        const float whiteOffset = (ctx.style != nullptr) ? juce::jlimit (0.0f, 1.0f, ctx.style->whiteOffset) : 0.0f;

        // Posicion normalizada 0..1 de una seccion.
        auto posOf = [] (int sec, int count) { return count > 1 ? (double) sec / (count - 1) : 0.5; };

        // Color de una seccion segun el estilo ACTIVO (pool del tema o estilo fijo).
        auto sectionColour = [&] (int sec, double beats, const ShowStyle* st) -> RGB
        {
            const ColorStyle cm = (st != nullptr) ? st->color : ColorStyle::Cycle;
            const double     cb = (st != nullptr) ? juce::jmax (0.25, st->colorBeats) : 1.0;
            const double pos = posOf (sec, nRGB);
            if (cm == ColorStyle::Rainbow)
            {
                float hue = (float) std::fmod (beats * 0.03 + pos * 1.0, 1.0);
                if (hue < 0.0f) hue += 1.0f;
                const auto c = juce::Colour::fromHSV (hue, 0.95f, 1.0f, 1.0f);
                return { c.getRed(), c.getGreen(), c.getBlue() };
            }
            // Auto-color por energia: identidad tonal modulada por la energia de la seccion.
            if (cm == ColorStyle::Auto)
            {
                RGB baseCol;
                if (! ctx.palette.empty())
                {
                    const int P    = (int) ctx.palette.size();
                    const int slot = (int) std::floor (pos * P);
                    baseCol = identityColour (ctx.palette, slot, beats, cb);
                }
                else
                {
                    baseCol = styleColour (st, ctx.colorOffset + sec, beats, cb);
                }
                const double sampleSec = beats * secPerBeat + ctx.beatOffset;
                const float  e = (ctx.sectionEnergy != nullptr) ? (*ctx.sectionEnergy) (sampleSec)
                               : (ctx.energy != nullptr) ? (*ctx.energy) (sampleSec) : 0.6f;
                return energyColour (baseCol, e);
            }
            // Identidad del tema: la paleta por chroma se reparte por la barra y
            // evoluciona por frases (no como pasarela). Solo en estilo por defecto.
            if (! ctx.palette.empty() && cm == ColorStyle::Cycle)
            {
                const int P    = (int) ctx.palette.size();
                const int slot = (int) std::floor (pos * P);
                return identityColour (ctx.palette, slot, beats, cb);
            }
            return styleColour (st, ctx.colorOffset + sec, beats, cb);
        };

        // Suma de "cometas" (barridos) lanzados por cada transiente, para Chase.
        const double sweepSec = secPerBeat * 2.0;       // cruza la barra en 2 beats
        const double cometW   = 0.13;                   // anchura del cometa (0..1)
        auto cometIntensity = [&] (double pos, double t, bool reverse) -> float
        {
            // Solo miramos los destellos cuya ventana de barrido cubre t.
            const auto lo = std::lower_bound (flashes.begin(), flashes.end(), t - sweepSec);
            float sum = 0.0f;
            for (auto it = lo; it != flashes.end(); ++it)
            {
                const double tk = *it;
                if (tk > t) break;
                const double prog = (t - tk) / sweepSec;           // 0..1
                if (prog < 0.0 || prog > 1.0) continue;
                const std::size_t idx = (std::size_t) (it - flashes.begin());
                bool dir = (idx & 1) != 0;                          // alterna direccion
                if (reverse) dir = ! dir;
                const double head = dir ? prog : 1.0 - prog;
                const double d = pos - head;
                sum += (float) std::exp (-(d * d) / (2.0 * cometW * cometW));
            }
            return juce::jmin (1.0f, sum);
        };

        // Onda expansiva radial: cada transiente lanza un anillo desde el centro
        // hacia los extremos (para el patron Ripple).
        auto rippleIntensity = [&] (double pos, double t) -> float
        {
            const double dist = std::abs (pos - 0.5) * 2.0;        // 0 centro .. 1 extremo
            const auto lo = std::lower_bound (flashes.begin(), flashes.end(), t - sweepSec);
            float sum = 0.0f;
            for (auto it = lo; it != flashes.end(); ++it)
            {
                const double tk = *it;
                if (tk > t) break;
                const double prog = (t - tk) / sweepSec;           // 0..1 = radio del anillo
                if (prog < 0.0 || prog > 1.0) continue;
                const double d = dist - prog;
                sum += (float) std::exp (-(d * d) / (2.0 * cometW * cometW));
            }
            return juce::jmin (1.0f, sum);
        };

        // Envolvente de acento blanco: pulso que decae tras cada transiente.
        auto whiteAccent = [&] (double t) -> float
        {
            const auto lo = std::lower_bound (flashes.begin(), flashes.end(), t - 0.5);
            float best = 0.0f;
            for (auto it = lo; it != flashes.end(); ++it)
            {
                if (*it > t) break;
                best = juce::jmax (best, (float) std::exp (-(t - *it) / 0.10));
            }
            return best;
        };

        // --- Escritura de keyframes con compresion por delta (evita millones de keys). ---
        const int nCh = f.channelCount();
        std::vector<bool>   started (nCh, false);
        std::vector<float>  pushedV (nCh, 0.0f);
        std::vector<double> pushedT (nCh, 0.0);
        std::vector<float>  lastV   (nCh, 0.0f);
        std::vector<double> lastT   (nCh, 0.0);

        auto feed = [&] (int ci, double beats, float v)
        {
            auto& kf = f.channels[(size_t) ci].keyframes;
            if (! started[ci])
            {
                kf.push_back ({ beats, v, false });
                started[ci] = true; pushedV[ci] = v; pushedT[ci] = beats;
                lastV[ci] = v; lastT[ci] = beats;
                return;
            }
            if (std::abs (v - pushedV[ci]) >= 3.0f)
            {
                // Ancla la zona plana previa para conservar bordes nitidos.
                if (lastT[ci] > pushedT[ci] + 1.0e-9 && std::abs (lastV[ci] - pushedV[ci]) < 3.0f)
                    kf.push_back ({ lastT[ci], lastV[ci], false });
                kf.push_back ({ beats, v, false });
                pushedV[ci] = v; pushedT[ci] = beats;
            }
            lastV[ci] = v; lastT[ci] = beats;
        };

        // Buffers por seccion (para suavizar espacialmente antes de escribir).
        std::vector<float> shape ((size_t) juce::jmax (1, nRGB), 0.0f);
        std::vector<float> rr ((size_t) juce::jmax (1, nRGB), 0.0f);
        std::vector<float> gb ((size_t) juce::jmax (1, nRGB), 0.0f);
        std::vector<float> bb ((size_t) juce::jmax (1, nRGB), 0.0f);
        std::vector<float> ww ((size_t) juce::jmax (1, nW),   0.0f);

        // Suavizado espacial 1-2-1 (varias pasadas) para transiciones GRADUALES entre segmentos.
        auto smooth = [] (std::vector<float>& v, int passes)
        {
            const int n = (int) v.size();
            if (n < 3) return;
            std::vector<float> tmp (v.size());
            for (int pass = 0; pass < passes; ++pass)
            {
                for (int i = 0; i < n; ++i)
                {
                    const float l = v[(size_t) juce::jmax (0, i - 1)];
                    const float c = v[(size_t) i];
                    const float r = v[(size_t) juce::jmin (n - 1, i + 1)];
                    tmp[(size_t) i] = (l + 2.0f * c + r) * 0.25f;
                }
                v.swap (tmp);
            }
        };

        for (double t = 0.0; t <= tEnd; t += dt)
        {
            const double beats = ctx.toBeats (t);
            const float  base  = contrast ((*ctx.energy) (t));   // energia 0..1

            // Estilo activo del compas: pool de coreografias del tema (Auto IA por
            // energia) o el estilo fijo. De el salen el patron, color y velocidad.
            const ShowStyle* act = activeStyleAt (ctx, beats);
            const double moveSpeed = (act != nullptr) ? juce::jmax (0.25, act->moveSpeed) : 1.0;

            // Resuelve el patron Auto por compas (energia al inicio del compas).
            MotionStyle motion = (act != nullptr) ? act->motion : baseMotion;
            if (motion == MotionStyle::Auto)
            {
                const double bar = juce::jmax (1.0, ctx.beatsPerBar);
                const double barStart = std::floor (beats / bar) * bar;
                const int    phrase   = (int) std::floor (beats / (bar * 2.0));
                motion = motionForEnergy ((*ctx.energy) (barStart * secPerBeat + ctx.beatOffset), phrase);
            }

            // Patrones NITIDOS (de bloque): juegan entre segmentos a NEGRO y a
            // tope con su color, sin difuminar. Los patrones "suaves" (ondas,
            // anillos, centelleo) conservan el gradiente.
            const bool crisp = (motion == MotionStyle::Chase    || motion == MotionStyle::Alternate
                             || motion == MotionStyle::Build    || motion == MotionStyle::Bloom
                             || motion == MotionStyle::Stack    || motion == MotionStyle::Theater
                             || motion == MotionStyle::Strobe   || motion == MotionStyle::Random);

            // 1) Forma de intensidad por seccion (0..1), segun el patron.
            //    La ENERGIA del stem (base) manda el brillo; el patron solo decide
            //    DONDE brilla. Asi la barra reacciona al instrumento, no solo al tempo.
            for (int s = 0; s < nRGB; ++s)
            {
                const double p = posOf (s, nRGB);
                float inten = base;

                switch (motion)
                {
                    case MotionStyle::Chase:
                    {
                        // Banda brillante que recorre la barra + cometas en los golpes.
                        const double head = std::fmod (beats * 0.5 * moveSpeed, 1.0);
                        const double d  = std::abs (p - head);
                        const double dd = std::min (d, 1.0 - d);                 // envuelve por los extremos
                        const float band  = (float) std::exp (-(dd * dd) / (2.0 * 0.16 * 0.16));
                        const float comet = cometIntensity (p, t, false);
                        const float where = juce::jlimit (0.0f, 1.0f, band * 0.75f + comet * 0.7f);
                        inten = base * (0.12f + 0.88f * where);
                        break;
                    }
                    case MotionStyle::Alternate:
                    {
                        const int parity = (((int) std::floor (beats) + s) & 1);
                        inten = base * (parity == 0 ? 1.0f : 0.22f);
                        break;
                    }
                    case MotionStyle::Wave:
                    {
                        const double ph = beats * 0.9 * moveSpeed - p * 3.0;
                        const float  w  = 0.5f + 0.5f * (float) std::sin (ph * 2.0 * 3.14159265358979);
                        inten = base * (0.25f + 0.75f * w);
                        break;
                    }
                    case MotionStyle::Pulse:
                    {
                        const double frac = beats - std::floor (beats);          // 0..1 dentro del beat
                        const double dist = std::abs (p - 0.5) * 2.0;            // 0 centro, 1 extremos
                        const double front = frac;                               // anillo que crece
                        float ring = 1.0f - (float) (std::abs (dist - front) / 0.28);
                        ring = juce::jlimit (0.0f, 1.0f, ring);
                        inten = base * (0.12f + 0.88f * ring);
                        break;
                    }
                    case MotionStyle::Bounce:
                    {
                        // Cabeza que recorre la barra y rebota (ping-pong).
                        const double tri  = std::abs (std::fmod (beats * 0.5 * moveSpeed, 2.0) - 1.0);
                        const double d    = p - tri;
                        const float  band = (float) std::exp (-(d * d) / (2.0 * 0.14 * 0.14));
                        const float  comet = cometIntensity (p, t, false);
                        inten = base * (0.12f + 0.88f * juce::jlimit (0.0f, 1.0f, band + comet * 0.5f));
                        break;
                    }
                    case MotionStyle::Build:
                    {
                        // Se llena progresivamente de un extremo al otro y reinicia.
                        const double level = std::fmod (beats * 0.25 * moveSpeed, 1.0);
                        const float  on = (p <= level) ? 1.0f
                                        : (float) std::exp (-((p - level) * (p - level)) / (2.0 * 0.05 * 0.05));
                        inten = base * (0.12f + 0.88f * on);
                        break;
                    }
                    case MotionStyle::Sparkle:
                    {
                        // Centelleo: cada seccion parpadea con su propia fase.
                        const float ph = (float) (beats * 6.0 * moveSpeed) + hashNoise (s, 0) * 6.2832f;
                        const float tw = std::pow (0.5f + 0.5f * std::sin (ph), 5.0f);
                        inten = base * (0.08f + 0.92f * tw);
                        break;
                    }
                    case MotionStyle::Bloom:
                    {
                        // Florece del centro hacia los extremos.
                        const double dist  = std::abs (p - 0.5) * 2.0;
                        const double front = std::fmod (beats * 0.5 * moveSpeed, 1.0);
                        const float  on = (dist <= front) ? 1.0f
                                        : (float) std::exp (-((dist - front) * (dist - front)) / (2.0 * 0.05 * 0.05));
                        inten = base * (0.12f + 0.88f * on);
                        break;
                    }
                    case MotionStyle::Symmetry:
                    {
                        // Onda en espejo desde el centro (dos mitades simetricas).
                        const double pm = std::abs (p - 0.5) * 2.0;
                        const double ph = beats * 0.9 * moveSpeed - pm * 3.0;
                        const float  w  = 0.5f + 0.5f * (float) std::sin (ph * 2.0 * 3.14159265358979);
                        inten = base * (0.2f + 0.8f * w);
                        break;
                    }
                    case MotionStyle::Strobe:
                    {
                        // Parpadeo global rapido (gateado por la energia del stem).
                        const double ph = std::fmod (beats * 4.0 * moveSpeed, 1.0);
                        inten = base * (ph < 0.5 ? 1.0f : 0.06f);
                        break;
                    }
                    case MotionStyle::Stack:
                    {
                        // Apila desde ambos extremos hacia el centro.
                        const double level = std::fmod (beats * 0.25 * moveSpeed, 1.0);
                        const double dist  = std::abs (p - 0.5) * 2.0;
                        const float  on = (dist >= 1.0 - level) ? 1.0f
                                        : (float) std::exp (-((1.0 - level - dist) * (1.0 - level - dist)) / (2.0 * 0.05 * 0.05));
                        inten = base * (0.12f + 0.88f * on);
                        break;
                    }
                    case MotionStyle::Theater:
                    {
                        // Persecucion con huecos (1 de cada 3 secciones encendida).
                        const int idx   = (int) std::floor (p * juce::jmax (1, nRGB));
                        const int phase = (((idx + (int) std::floor (beats * moveSpeed)) % 3) + 3) % 3;
                        inten = base * (phase == 0 ? 1.0f : 0.12f);
                        break;
                    }
                    case MotionStyle::Ripple:
                    {
                        // Anillos concentricos que salen del centro en cada golpe.
                        const float r = rippleIntensity (p, t);
                        inten = base * (0.12f + 0.88f * r);
                        break;
                    }
                    case MotionStyle::Random:
                    {
                        // Salta a una seccion aleatoria distinta en cada beat.
                        const int nz = juce::jmax (1, nRGB);
                        const int active = (int) (hashNoise ((int) std::floor (beats * moveSpeed), 3) * nz);
                        inten = base * (((active % nz) == s) ? 1.0f : 0.12f);
                        break;
                    }
                    case MotionStyle::Unison:
                    default:
                        inten = base;
                        break;
                }

                shape[(size_t) s] = juce::jlimit (0.0f, 1.0f, inten);
            }

            // En patrones de bloque, ENDURECE: por debajo del umbral apaga del
            // todo (segmento negro) y por encima sube a color pleno. Asi la barra
            // "juega" entre segmentos apagados y a tope, no medias tintas.
            if (crisp)
                for (int s = 0; s < nRGB; ++s)
                    shape[(size_t) s] = (shape[(size_t) s] < 0.30f) ? 0.0f
                                                                    : juce::jmax (0.62f, shape[(size_t) s]);

            // 2) Suaviza la forma entre secciones (solo en patrones suaves; los de
            //    bloque se quedan con bordes nitidos entre segmentos).
            smooth (shape, crisp ? 0 : 2);

            // 3) Componer color por seccion * intensidad (+ offset de color como suelo).
            for (int s = 0; s < nRGB; ++s)
            {
                const float inten = crisp ? shape[(size_t) s]
                                          : juce::jmax (colorOffset, shape[(size_t) s]);
                const RGB   col   = sectionColour (s, beats, act);
                rr[(size_t) s] = col.r * inten;
                gb[(size_t) s] = col.g * inten;
                bb[(size_t) s] = col.b * inten;
            }

            // 4) Suaviza el color final entre secciones (mezcla de bordes -> gradiente).
            //    En patrones de bloque NO se mezcla: cada segmento mantiene su color puro.
            smooth (rr, crisp ? 0 : 1);
            smooth (gb, crisp ? 0 : 1);
            smooth (bb, crisp ? 0 : 1);

            for (int s = 0; s < nRGB; ++s)
            {
                const auto& sec = layout.rgb[(size_t) s];
                feed (sec[0], beats, rr[(size_t) s]);
                feed (sec[1], beats, gb[(size_t) s]);
                feed (sec[2], beats, bb[(size_t) s]);
            }

            // 5) Secciones de blanco (acento/contrapunto) con su propio offset y suavizado.
            if (nW > 0)
            {
                const float acc = whiteAccent (t);
                for (int s = 0; s < nW; ++s)
                {
                    const double p = posOf (s, nW);
                    float w = 0.0f;
                    if (motion == MotionStyle::Chase)
                        w = cometIntensity (p, t, true) * (0.5f + 0.5f * base);   // contra-barrido blanco
                    else
                        w = acc * (0.5f + 0.5f * base);                           // destello blanco al golpe
                    ww[(size_t) s] = juce::jmax (whiteOffset, w);
                }
                smooth (ww, 1);
                for (int s = 0; s < nW; ++s)
                    feed (layout.white[(size_t) s], beats, juce::jlimit (0.0f, 255.0f, ww[(size_t) s] * 255.0f));
            }
        }

        // Cierra cada canal con su ultimo valor para que la cola sea correcta.
        for (int ci = 0; ci < nCh; ++ci)
            if (started[ci] && lastT[ci] > pushedT[ci] + 1.0e-9)
                f.channels[(size_t) ci].keyframes.push_back ({ lastT[ci], lastV[ci], false });
    }

    //==============================================================================
    /** Reproduce una SECUENCIA MANUAL (estados dibujados a mano) sobre una fixtura
        segmentada, en bucle a lo largo del tema. La intensidad global de cada
        estado sigue la energia del audio (ctx.energy). */
    inline void paintManualSequence (Fixture& f, const ManualSequence& seq, const PaintContext& ctx)
    {
        if (ctx.energy == nullptr || seq.steps.empty())
            return;

        const auto layout = detectPixelLayout (f);
        const int  nRGB   = (int) layout.rgb.size();
        const int  nW     = (int) layout.white.size();
        const int  nCh    = f.channelCount();
        if (nRGB == 0 && nW == 0)
            return;

        const double secPerBeat = 60.0 / juce::jmax (1.0, ctx.bpm);

        // Duracion de cada paso convertida a BEATS (unidad interna del timeline).
        std::vector<double> stepBeats (seq.steps.size(), 0.0);
        double cycleBeats = 0.0;
        for (size_t i = 0; i < seq.steps.size(); ++i)
        {
            double d = juce::jmax (0.0, seq.steps[i].duration);
            if (! seq.useBeats) d /= secPerBeat;      // segundos -> beats
            stepBeats[i] = juce::jmax (1.0e-3, d);
            cycleBeats  += stepBeats[i];
        }
        if (cycleBeats <= 0.0)
            return;

        // Indice del paso activo para una posicion (en beats) dentro del ciclo.
        auto stepAt = [&] (double beats) -> int
        {
            double m = std::fmod (beats, cycleBeats);
            if (m < 0.0) m += cycleBeats;
            double acc = 0.0;
            for (size_t i = 0; i < stepBeats.size(); ++i)
            {
                acc += stepBeats[i];
                if (m < acc) return (int) i;
            }
            return (int) stepBeats.size() - 1;
        };

        const double dt    = secPerBeat / 8.0;        // ~8 muestras por beat
        const double tEnd  = ctx.lengthSeconds + secPerBeat;

        std::vector<bool>   started ((size_t) nCh, false);
        std::vector<float>  pushedV ((size_t) nCh, 0.0f), lastV ((size_t) nCh, 0.0f);
        std::vector<double> pushedT ((size_t) nCh, 0.0), lastT ((size_t) nCh, 0.0);

        auto feed = [&] (int ci, double beats, float v)
        {
            auto& kf = f.channels[(size_t) ci].keyframes;
            if (! started[ci])
            {
                kf.push_back ({ beats, v, false });
                started[ci] = true; pushedV[ci] = v; pushedT[ci] = beats;
                lastV[ci] = v; lastT[ci] = beats;
                return;
            }
            if (std::abs (v - pushedV[ci]) >= 3.0f)
            {
                if (lastT[ci] > pushedT[ci] + 1.0e-9 && std::abs (lastV[ci] - pushedV[ci]) < 3.0f)
                    kf.push_back ({ lastT[ci], lastV[ci], false });
                kf.push_back ({ beats, v, false });
                pushedV[ci] = v; pushedT[ci] = beats;
            }
            lastV[ci] = v; lastT[ci] = beats;
        };

        for (double t = 0.0; t <= tEnd; t += dt)
        {
            const double beats   = ctx.toBeats (t);
            const float  energy  = juce::jlimit (0.0f, 1.0f, (*ctx.energy) (t));
            const auto&  step    = seq.steps[(size_t) stepAt (beats)];
            const int    nSeg    = (int) step.segments.size();

            // Secciones RGB.
            for (int s = 0; s < nRGB; ++s)
            {
                SegState sg;
                if (s < nSeg) sg = step.segments[(size_t) s];
                const float lvl = sg.on ? juce::jlimit (0.0f, 1.0f, sg.intensity) * energy : 0.0f;
                feed (layout.rgb[(size_t) s][0], beats, juce::jlimit (0.0f, 255.0f, sg.r * lvl));
                feed (layout.rgb[(size_t) s][1], beats, juce::jlimit (0.0f, 255.0f, sg.g * lvl));
                feed (layout.rgb[(size_t) s][2], beats, juce::jlimit (0.0f, 255.0f, sg.b * lvl));
            }

            // Secciones de blanco: el segmento tiene su propio canal blanco
            // independiente (whiteOn/white). Por compatibilidad, si el segmento no
            // marca blanco pero su color RGB es blanquecino, tambien lo enciende.
            for (int s = 0; s < nW; ++s)
            {
                SegState sg;
                if (s < nSeg) sg = step.segments[(size_t) s];
                float w = 0.0f;
                if (sg.whiteOn)
                {
                    w = juce::jlimit (0.0f, 1.0f, sg.white) * energy;
                }
                else if (sg.on)
                {
                    const int mx = juce::jmax (sg.r, juce::jmax (sg.g, sg.b));
                    const int mn = juce::jmin (sg.r, juce::jmin (sg.g, sg.b));
                    const float sat = mx > 0 ? (float) (mx - mn) / (float) mx : 0.0f;
                    if (sat < 0.25f)   // blanquecino -> enciende el pixel blanco
                        w = juce::jlimit (0.0f, 1.0f, sg.intensity) * energy * (mx / 255.0f);
                }
                feed (layout.white[(size_t) s], beats, juce::jlimit (0.0f, 255.0f, w * 255.0f));
            }
        }

        for (int ci = 0; ci < nCh; ++ci)
            if (started[ci] && lastT[ci] > pushedT[ci] + 1.0e-9)
                f.channels[(size_t) ci].keyframes.push_back ({ lastT[ci], lastV[ci], false });
    }

    //==============================================================================
    /** Clasificacion de un equipo segun su nombre/modelo. */
    inline bool isBar  (const Fixture& f) { return f.name.containsIgnoreCase ("barra") || f.name.containsIgnoreCase ("bar"); }
    inline bool isHead (const Fixture& f) { return f.name.containsIgnoreCase ("cabeza") || f.model.containsIgnoreCase ("moving")
                                                || f.name.containsIgnoreCase ("spider") || f.model.containsIgnoreCase ("spider"); }

    /** Foco PAR generico: tiene dimmer o RGB(W) y NO es cabeza movil ni barra. Usa
        el motor de fiesta dedicado (paintPar). */
    inline bool isPar (const Fixture& f)
    {
        if (isHead (f) || isBar (f)) return false;
        for (const auto& c : f.channels)
            if (c.type == ChannelType::Dimmer || c.type == ChannelType::Red
                || c.type == ChannelType::Green || c.type == ChannelType::Blue
                || c.type == ChannelType::White)
                return true;
        return false;
    }

    /** Asigna a cada PAR un indice de ORDEN FISICO segun su direccion DMX (universo,
        luego direccion de arranque). parPos[indiceGlobal] = posicion en la fila;
        parCount = total de PAR. De aqui salen barridos/escalados/estrobos secuenciados. */
    inline void buildParOrder (const std::vector<Fixture>& fixtures,
                               std::vector<int>& parPos, int& parCount)
    {
        parPos.assign (fixtures.size(), 0);
        std::vector<int> order;
        for (int i = 0; i < (int) fixtures.size(); ++i)
            if (isPar (fixtures[(size_t) i])) order.push_back (i);
        std::sort (order.begin(), order.end(), [&] (int a, int b)
        {
            const auto& fa = fixtures[(size_t) a];
            const auto& fb = fixtures[(size_t) b];
            if (fa.universe != fb.universe) return fa.universe < fb.universe;
            return fa.startAddress < fb.startAddress;
        });
        for (int k = 0; k < (int) order.size(); ++k)
            parPos[(size_t) order[k]] = k;
        parCount = (int) order.size();
    }

    /** Igual que buildParOrder pero para CABEZAS/spiders: ordena por (universo,
        direccion DMX) y asigna a cada cabeza su posicion fisica en el escenario.
        Asi, con varias cabezas iguales, los abanicos/espejos/barridos salen en el
        orden real del rig (izquierda a derecha) y se ven coordinados. */
    inline void buildHeadOrder (const std::vector<Fixture>& fixtures,
                                std::vector<int>& headPos, int& headCount)
    {
        headPos.assign (fixtures.size(), 0);
        std::vector<int> order;
        for (int i = 0; i < (int) fixtures.size(); ++i)
            if (isHead (fixtures[(size_t) i])) order.push_back (i);
        std::sort (order.begin(), order.end(), [&] (int a, int b)
        {
            const auto& fa = fixtures[(size_t) a];
            const auto& fb = fixtures[(size_t) b];
            if (fa.universe != fb.universe) return fa.universe < fb.universe;
            return fa.startAddress < fb.startAddress;
        });
        for (int k = 0; k < (int) order.size(); ++k)
            headPos[(size_t) order[k]] = k;
        headCount = (int) order.size();
    }

    /** Clave estable de un equipo (debe coincidir con la del visualizador/asignacion). */
    inline juce::String fixtureKey (const Fixture& f)
    {
        return f.name + "@" + juce::String (f.universe) + ":" + juce::String (f.startAddress);
    }

    /** Nombre base de un equipo, sin el sufijo numerico que se anade al duplicar en
        el rig ("Barra LED 2" -> "Barra LED"). Sirve para emparejar una coreografia
        MANUAL guardada para un tipo de equipo con cualquier copia suya en el rig. */
    inline juce::String fixtureBaseName (const juce::String& name)
    {
        auto s = name.trim();
        const int sp = s.lastIndexOfChar (' ');
        if (sp > 0 && s.substring (sp + 1).containsOnly ("0123456789"))
            return s.substring (0, sp).trim();
        return s;
    }

    /** Devuelve el stem asignado a un equipo ("drums"/"bass"/"vocals"/"other") o "" = auto. */
    using StemAssignFn = std::function<juce::String (const Fixture&)>;

    /** Busca un stem por nombre; devuelve nullptr si no existe. */
    inline const StemAnalysis* findStem (const TrackAnalysis& a, const juce::String& name)
    {
        for (const auto& s : a.stems)
            if (s.name.equalsIgnoreCase (name))
                return &s;
        return nullptr;
    }

    //==============================================================================
    inline DmxShow makeShow (const TrackAnalysis& a, const std::vector<Fixture>& rig)
    {
        DmxShow show;
        show.bpm           = a.estimatedBpm > 0.0 ? a.estimatedBpm : 120.0;
        show.beatOffset    = a.beatOffset;
        show.lengthSeconds = a.lengthSeconds;
        show.fixtures      = rig;

        int maxUni = 0;
        for (const auto& f : show.fixtures)
            maxUni = juce::jmax (maxUni, f.universe);
        show.numUniverses = maxUni + 1;
        return show;
    }

    /** Construye el pool de ShowStyles que aplican a una fixtura, ordenado de
        calma a intenso para que el Auto IA lo reparta por bandas de energia. El
        estilo base aporta los offsets de brillo de la barra de pixeles. */
    inline std::vector<ShowStyle> buildFixturePool (const std::vector<Choreography>* songPool,
                                                    const Fixture& f, const ShowStyle& base)
    {
        std::vector<ShowStyle> pool;
        if (songPool != nullptr && ! songPool->empty())
        {
            const juce::String key = fixtureKey (f);
            for (const auto& c : *songPool)
            {
                if (c.manual)
                    continue;   // las manuales se aplican aparte (paintManualSequence)
                if (! c.targetKey.isEmpty() && c.targetKey != key)
                    continue;
                ShowStyle s = c.style;
                s.colorOffset = base.colorOffset;   // conserva los offsets de brillo de la barra
                s.whiteOffset = base.whiteOffset;
                pool.push_back (s);
            }
            std::sort (pool.begin(), pool.end(), [] (const ShowStyle& x, const ShowStyle& y)
                       { return motionIntensity (x.motion) < motionIntensity (y.motion); });
        }

        // Full Auto: si no hay coreografias del usuario para esta fixtura, usa el
        // pool integrado calmo->intenso para que el motor reparta por seccion.
        if (pool.empty() && base.fullAuto)
            pool = fullAutoPool (base);

        return pool;
    }

    /** Busca una coreografia MANUAL del tema que aplique a una fixtura (target
        vacio = cualquiera). El emparejamiento es por NOMBRE BASE del equipo, de modo
        que una coreografia guardada para un tipo de equipo se aplica a cualquier copia
        suya del rig sin importar direccion/universo. Devuelve nullptr si no hay. */
    inline const Choreography* findManualFor (const std::vector<Choreography>* songPool, const Fixture& f)
    {
        if (songPool == nullptr)
            return nullptr;
        const juce::String base = fixtureBaseName (f.name);
        const juce::String key  = fixtureKey (f);
        for (const auto& c : *songPool)
            if (c.manual && (c.targetKey.isEmpty()
                             || c.targetKey == key                                       // asignacion exacta por fixtura
                             || fixtureBaseName (c.targetKey).equalsIgnoreCase (base)))
                return &c;
        return nullptr;
    }

    /** Mapeo MONO (Fase 5): toda la mezcla controla todos los equipos. */
    inline DmxShow generateMono (const TrackAnalysis& a, const std::vector<Fixture>& rig,
                                 const ShowStyle& style = {},
                                 const std::vector<RGB>& preferred = {},
                                 const std::vector<Choreography>* songPool = nullptr,
                                 const SectionPaletteMap& sectionPalettes = {})
    {        DmxShow show = makeShow (a, rig);

        const double secPerBeat = 60.0 / show.bpm;
        const double beatOffset = show.beatOffset;
        const auto toBeats = [secPerBeat, beatOffset] (double sec) { return (sec - beatOffset) / secPerBeat; };
        const double beatsPerBar = 4.0;
        const int    numBars = juce::jmax (1, (int) std::ceil (toBeats (a.lengthSeconds) / beatsPerBar));

        const EnergyFn energy = energyFnFor (a.energy, a.lengthSeconds);
        const std::vector<double> flashes = spacedFlashes (a.transients);

        // Nivel estable por seccion (intro/build/drop/...) para cambiar estilo por parte.
        const EnergyFn sectionEnergy = sectionFnFor (a.sections);

        // Paleta de identidad del tema (colores preferidos del usuario si los hay).
        const std::vector<RGB> autoPalette = paletteFromChroma (a.chroma);
        const std::vector<RGB> palette = preferred.empty() ? autoPalette : preferred;
        auto paletteAt = makePaletteAt (a.sections, sectionPalettes, autoPalette);

        int headIndex = 0;
        int fixtureIndex = 0;
        int totalHeads = 0;
        for (const auto& f : show.fixtures) if (isHead (f)) ++totalHeads;
        std::vector<int> parPos; int parCount = 0;
        buildParOrder (show.fixtures, parPos, parCount);
        std::vector<int> headPos; int headCnt = 0;
        buildHeadOrder (show.fixtures, headPos, headCnt);
        for (auto& f : show.fixtures)
        {
            const bool head = isHead (f);

            // Pool de coreografias de la cancion que aplican a ESTA fixtura,
            // ordenado de calma a intenso (el Auto IA lo reparte por energia).
            std::vector<ShowStyle> fixturePool = buildFixturePool (songPool, f, style);

            PaintContext ctx;
            ctx.energy        = &energy;
            ctx.sectionEnergy = sectionEnergy ? &sectionEnergy : nullptr;
            ctx.sections      = a.sections.empty() ? nullptr : &a.sections;
            ctx.flashes       = &flashes;
            ctx.accents       = nullptr;
            ctx.isHead        = head;
            ctx.headIndex     = headPos[(size_t) fixtureIndex];   // posicion fisica (orden DMX)
            ctx.headCount     = juce::jmax (1, headCnt);
            ctx.colorOffset   = fixtureIndex;   // cada equipo arranca en un color distinto
            ctx.fixtureIndex  = fixtureIndex;
            ctx.fixtureGroup  = fixtureIndex;
            ctx.groupCount    = 4;
            ctx.orderIndex    = parPos[(size_t) fixtureIndex];
            ctx.orderCount    = juce::jmax (1, parCount);
            ctx.style         = &style;
            ctx.palette       = palette;
            ctx.paletteAt     = paletteAt;
            ctx.autoPool      = fixturePool.empty() ? nullptr : &fixturePool;
            ctx.lengthSeconds = a.lengthSeconds;
            ctx.beatsPerBar   = beatsPerBar;
            ctx.numBars       = numBars;
            ctx.bpm           = show.bpm;
            ctx.beatOffset    = show.beatOffset;
            ctx.toBeats       = toBeats;

            // Coreografia MANUAL del tema para esta fixtura: anula la algoritmica.
            if (const auto* man = findManualFor (songPool, f))
            {
                paintManualSequence (f, man->sequence, ctx);
                if (head) ++headIndex;
                ++fixtureIndex;
                continue;
            }

            const auto layout = detectPixelLayout (f);
            if      (layout.isPixelBar()) paintPixelBar (f, ctx, layout);
            else if (isPar (f))           paintPar (f, ctx);
            else                          paintFixture (f, ctx);
            if (head) ++headIndex;
            ++fixtureIndex;
        }

        show.valid = true;
        return show;
    }

    /** Mapeo POR STEM (Fase 4): cada grupo de equipos sigue a su instrumento. */
    inline DmxShow generateFromStems (const TrackAnalysis& a, const std::vector<Fixture>& rig,
                                      const StemAssignFn& assign = {}, const ShowStyle& style = {},
                                      const std::vector<RGB>& preferred = {},
                                      const std::vector<Choreography>* songPool = nullptr,
                                      const SectionPaletteMap& sectionPalettes = {})
    {
        DmxShow show = makeShow (a, rig);

        const double secPerBeat = 60.0 / show.bpm;
        const double beatOffset = show.beatOffset;
        const auto toBeats = [secPerBeat, beatOffset] (double sec) { return (sec - beatOffset) / secPerBeat; };
        const double beatsPerBar = 4.0;
        const int    numBars = juce::jmax (1, (int) std::ceil (toBeats (a.lengthSeconds) / beatsPerBar));

        // Paleta de identidad del tema (colores preferidos del usuario si los hay).
        const std::vector<RGB> autoPalette = paletteFromChroma (a.chroma);
        const std::vector<RGB> palette = preferred.empty() ? autoPalette : preferred;
        auto paletteAt = makePaletteAt (a.sections, sectionPalettes, autoPalette);

        // Stems disponibles (con fallback a la mezcla si falta alguno).
        const StemAnalysis* drums  = findStem (a, "drums");
        const StemAnalysis* bass   = findStem (a, "bass");
        const StemAnalysis* vocals = findStem (a, "vocals");
        const StemAnalysis* other  = findStem (a, "other");

        const EnergyFn mixEnergy    = energyFnFor (a.energy, a.lengthSeconds);
        const EnergyFn drumsEnergy  = drums  ? energyFnFor (drums->energy,  a.lengthSeconds) : mixEnergy;
        const EnergyFn bassEnergy   = bass   ? energyFnFor (bass->energy,   a.lengthSeconds) : mixEnergy;
        const EnergyFn vocalsEnergy = vocals ? energyFnFor (vocals->energy, a.lengthSeconds) : mixEnergy;
        const EnergyFn otherEnergy  = other  ? energyFnFor (other->energy,  a.lengthSeconds) : mixEnergy;

        // Nivel estable por seccion (intro/build/drop/...) para cambiar estilo por parte.
        const EnergyFn sectionEnergy = sectionFnFor (a.sections);

        const std::vector<double> drumsFlashes = spacedFlashes (drums  ? drums->transients  : a.transients);
        const std::vector<double> bassFlashes  = spacedFlashes (bass   ? bass->transients   : a.transients, 0.25);
        const std::vector<double> vocalAccents = spacedFlashes (vocals ? vocals->transients : std::vector<double>{}, 0.30);
        const std::vector<double> otherFlashes = spacedFlashes (other  ? other->transients  : a.transients, 0.22);

        // Selecciona envolvente/destellos por nombre de stem ("" = no aplica).
        auto energyForStem = [&] (const juce::String& s) -> const EnergyFn*
        {
            if (s == "drums")  return &drumsEnergy;
            if (s == "bass")   return &bassEnergy;
            if (s == "vocals") return &vocalsEnergy;
            if (s == "other")  return &otherEnergy;
            return nullptr;
        };
        auto flashesForStem = [&] (const juce::String& s) -> const std::vector<double>*
        {
            if (s == "drums")  return &drumsFlashes;
            if (s == "bass")   return &bassFlashes;
            if (s == "vocals") return &vocalAccents;
            if (s == "other")  return &otherFlashes;
            return nullptr;
        };

        int headIndex = 0;
        int fixtureIndex = 0;
        int totalHeads = 0;
        for (const auto& f : show.fixtures) if (isHead (f)) ++totalHeads;
        std::vector<int> parPos; int parCount = 0;
        buildParOrder (show.fixtures, parPos, parCount);
        std::vector<int> headPos; int headCnt = 0;
        buildHeadOrder (show.fixtures, headPos, headCnt);
        for (auto& f : show.fixtures)
        {
            std::vector<ShowStyle> fixturePool = buildFixturePool (songPool, f, style);

            PaintContext ctx;
            ctx.lengthSeconds = a.lengthSeconds;
            ctx.beatsPerBar   = beatsPerBar;
            ctx.numBars       = numBars;
            ctx.bpm           = show.bpm;
            ctx.beatOffset    = show.beatOffset;
            ctx.fixtureIndex  = fixtureIndex;
            ctx.fixtureGroup  = fixtureIndex;
            ctx.groupCount    = 4;
            ctx.orderIndex    = parPos[(size_t) fixtureIndex];
            ctx.orderCount    = juce::jmax (1, parCount);
            ctx.headCount     = juce::jmax (1, headCnt);
            ctx.style         = &style;
            ctx.palette       = palette;
            ctx.paletteAt     = paletteAt;
            ctx.autoPool      = fixturePool.empty() ? nullptr : &fixturePool;
            ctx.toBeats       = toBeats;
            ctx.sectionEnergy = sectionEnergy ? &sectionEnergy : nullptr;
            ctx.sections      = a.sections.empty() ? nullptr : &a.sections;

            // El ROL fisico (movimiento, color de arranque) sigue dependiendo del tipo.
            if (isHead (f))     { ctx.isHead = true; ctx.headIndex = headPos[(size_t) fixtureIndex]; ctx.colorOffset = headPos[(size_t) fixtureIndex] + 2; }
            else if (isBar (f)) { ctx.colorOffset = 1; }
            else                { ctx.colorOffset = fixtureIndex; }

            // Stem asignado por el usuario; si esta vacio, mapeo automatico por tipo.
            const juce::String stem = assign ? assign (f) : juce::String();
            const EnergyFn* manualE = energyForStem (stem);

            if (manualE != nullptr)
            {
                ctx.energy  = manualE;
                ctx.flashes = flashesForStem (stem);
                // Si la fuente es la voz, sus transientes ya van como destellos.
            }
            else if (isBar (f))
            {
                ctx.energy  = &bassEnergy;
                ctx.flashes = &bassFlashes;
            }
            else if (isHead (f))
            {
                ctx.energy  = &otherEnergy;
                ctx.flashes = &otherFlashes;
                ctx.accents = vocalAccents.empty() ? nullptr : &vocalAccents;
            }
            else
            {
                ctx.energy  = &drumsEnergy;
                ctx.flashes = &drumsFlashes;
            }

            // Coreografia MANUAL del tema para esta fixtura: anula la algoritmica.
            if (const auto* man = findManualFor (songPool, f))
            {
                paintManualSequence (f, man->sequence, ctx);
                if (isHead (f)) ++headIndex;
                ++fixtureIndex;
                continue;
            }

            const auto layout = detectPixelLayout (f);
            if      (layout.isPixelBar()) paintPixelBar (f, ctx, layout);
            else if (isPar (f))           paintPar (f, ctx);
            else                          paintFixture (f, ctx);
            if (isHead (f)) ++headIndex;
            ++fixtureIndex;
        }

        show.valid = true;
        return show;
    }

    inline DmxShow generate (const TrackAnalysis& a, const std::vector<Fixture>& rig,
                             const StemAssignFn& assign = {}, const ShowStyle& style = {},
                             const std::vector<RGB>& preferred = {},
                             const std::vector<Choreography>* songPool = nullptr,
                             const SectionPaletteMap& sectionPalettes = {})
    {
        if (! a.valid || a.lengthSeconds <= 0.0)
            return {};

        if (! a.stems.empty())
            return generateFromStems (a, rig, assign, style, preferred, songPool, sectionPalettes);

        return generateMono (a, rig, style, preferred, songPool, sectionPalettes);
    }
}
