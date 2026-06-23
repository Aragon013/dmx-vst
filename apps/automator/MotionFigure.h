#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

/**
    Vocabulario COMPARTIDO de figuras de movimiento para fixtures con motor
    (cabezas moviles, spiders, barras motorizadas). Es la UNICA fuente de verdad
    del movimiento: lo usan tanto el motor de coreografia (ChoreographyEngine, que
    hornea keyframes Pan/Tilt) como el visualizador 2.5D (para los aparatos que NO
    tienen canales Pan/Tilt, como spiders/derbies).

    Principios anti-erratico:
      - El tiempo es MUSICAL (beats): motor y visualizador coinciden con la cancion.
      - La frecuencia de cada figura es CONSTANTE (la energia modula la AMPLITUD, no
        el multiplicador de tiempo) -> nunca hay saltos de fase.
      - El desfase entre fixtures es DETERMINISTA (por indice) -> sync/espejo/abanico
        limpios, no ruido aleatorio.
      - La figura se elige por SECCION/energia y se mantiene estable (no cambia cada
        beat), que es lo que daba sensacion erratica.
*/
namespace motionfig
{
    enum class Figure
    {
        Auto = 0,   // la IA elige segun la energia de la seccion
        Static,     // casi quietas (ambiente)
        SweepSync,  // todas a la vez, lado a lado (sincronia)
        SweepWave,  // barrido escalonado por indice (abanico viajero)
        Mirror,     // mitades en espejo (se abren/cierran)
        Cross,      // pares que se cruzan
        Circle,     // circulos (pan/tilt en cuadratura)
        Eight,      // ochos (lemniscata: tilt al doble de frecuencia)
        Bounce,     // ping-pong / rebote (triangular)
        Spread,     // abanico que abre desde el centro
        Pendulum,   // pendulo amplio lado a lado (rango casi total)
        Spiral,     // espiral que abre y cierra (circulo con radio respirando)
        Tide,       // cascada de tilt escalonada (olas verticales)
        Zigzag,     // zigzag brusco de pan con tilt alterno
        Chaos,      // movimiento energetico tipo headbang (rapido, full rango)
        FanBeam,    // abanico que barre arriba/abajo mientras abre
        Wave2,      // doble onda cruzada (pan e indice opuesto)
        SnapPoints  // salta entre posiciones y mantiene (look de "tek")
    };

    /** Nombres (en espanol, sin acentos) en el ORDEN del enum. Indice 0 = Auto IA. */
    inline juce::StringArray figureNames()
    {
        return { "Auto IA", "Estatico", "Barrido sync", "Barrido onda", "Espejo",
                 "Cruce", "Circulos", "Ochos", "Rebote", "Abanico",
                 "Pendulo", "Espiral", "Marea", "Zigzag", "Caos",
                 "Haz abanico", "Doble onda", "Saltos" };
    }

    /** Elige una figura concreta segun la energia local (0..1) de la seccion.
        Alterna entre figuras afines por FRASE para dar variedad sin volverse
        monotono. Devuelve siempre una figura concreta (nunca Auto). */
    inline Figure figureForEnergy (float e, int phrase = 0)
    {
        // 4 variantes por banda para mucha mas variedad en el Auto IA.
        const int v = ((phrase % 4) + 4) % 4;
        if (e < 0.15f)                                                          // ambiente
            return (v < 2) ? Figure::Static : Figure::SweepSync;
        if (e < 0.30f)                                                          // intro
        {
            const Figure opts[] = { Figure::SweepSync, Figure::Mirror, Figure::Spread, Figure::Pendulum };
            return opts[v];
        }
        if (e < 0.48f)                                                          // estrofa
        {
            const Figure opts[] = { Figure::SweepWave, Figure::Mirror, Figure::Tide, Figure::Wave2 };
            return opts[v];
        }
        if (e < 0.66f)                                                          // desarrollo
        {
            const Figure opts[] = { Figure::Cross, Figure::SweepWave, Figure::Circle, Figure::FanBeam };
            return opts[v];
        }
        if (e < 0.82f)                                                          // pre-drop
        {
            const Figure opts[] = { Figure::Spread, Figure::Cross, Figure::Spiral, Figure::Zigzag };
            return opts[v];
        }
        // drop / climax: lo mas energetico y amplio
        const Figure opts[] = { Figure::Eight, Figure::Circle, Figure::Chaos, Figure::SnapPoints };
        return opts[v];
    }

    struct Sample { float pan = 0.5f, tilt = 0.5f; };   // ambos en 0..1

    /** Ruido determinista 0..1 (para los saltos de SnapPoints, reproducible). */
    inline float hashLike (int a, int b)
    {
        juce::uint32 h = (juce::uint32) (a * 73856093) ^ (juce::uint32) (b * 19349663);
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        return (float) (h & 0xffffffu) / (float) 0xffffffu;
    }

    /** Evalua una figura en un instante musical.
        @param fig     figura concreta (si llega Auto, se trata como SweepSync)
        @param beats   posicion musical en beats (tiempo monotono de la cancion)
        @param energy  hype 0..1 -> modula la AMPLITUD (calma = casi quieto)
        @param index   posicion del fixture dentro de su grupo de motores
        @param count   numero de fixtures del grupo
        @param speed   velocidad del estilo (estable por seccion; NO fluctua por frame)
        @returns pan/tilt normalizados 0..1 */
    inline Sample eval (Figure fig, double beats, float energy,
                        int index, int count, double speed)
    {
        const double twoPi = 2.0 * 3.14159265358979323846;
        const int    n     = juce::jmax (1, count);
        const float  gp    = (n > 1) ? (float) index / (float) (n - 1) : 0.5f;  // 0..1 en el grupo
        // Amplitud con SUELO ALTO: incluso en secciones medias los motores recorren
        // casi todo su rango (el usuario queria mas recorrido, no un 60%). La energia
        // solo termina de abrir hasta el 100%.
        const float  amp   = 0.62f + 0.38f * juce::jlimit (0.0f, 1.0f, energy);
        const bool   even  = (index & 1) == 0;
        // Velocidad base mas viva (los motores se sentian lentos). speed viene del estilo.
        const double sp    = juce::jmax (0.2, speed) * 1.5;

        // Oscilador base: la frecuencia (ciclos por beat) es CONSTANTE por figura.
        auto osc = [&] (double cyclesPerBeat, double phase01) -> float
        {
            return (float) std::sin ((beats * cyclesPerBeat * sp + phase01) * twoPi);
        };

        // Amplitudes de PAN/TILT casi a tope: pan llega a +-0.5 (rango completo) y
        // tilt mucho mas marcado que antes (los spiders necesitan recorrido vertical).
        const float PAN = 0.50f, TILT = 0.34f;

        float pan = 0.5f, tilt = 0.5f;

        switch (fig)
        {
            case Figure::Static:
                pan  = 0.5f + 0.05f * osc (0.06, gp * 0.1);   // deriva minima
                tilt = 0.5f + 0.06f * osc (0.05, 0.25);
                break;

            case Figure::SweepSync:
                pan  = 0.5f + PAN * amp * osc (0.1875, 0.0);
                tilt = 0.5f + 0.18f * amp * osc (0.1875, 0.25);
                break;

            case Figure::SweepWave:
                pan  = 0.5f + PAN * amp * osc (0.1875, gp * 0.6);
                tilt = 0.5f + 0.18f * amp * osc (0.1875, gp * 0.6 + 0.25);
                break;

            case Figure::Mirror:
            {
                const float dir = even ? 1.0f : -1.0f;
                pan  = 0.5f + dir * PAN * amp * osc (0.1875, 0.0);
                tilt = 0.5f + 0.22f * amp * osc (0.375, 0.0);
                break;
            }

            case Figure::Cross:
            {
                const float dir = even ? 1.0f : -1.0f;
                pan  = 0.5f + dir * PAN * amp * osc (0.125, gp * 0.2);
                tilt = 0.5f + 0.18f * amp * osc (0.25, 0.0);
                break;
            }

            case Figure::Circle:
                pan  = 0.5f + PAN * amp * osc (0.1666, gp * 0.5);
                tilt = 0.5f + TILT * amp * osc (0.1666, gp * 0.5 + 0.25);
                break;

            case Figure::Eight:
                pan  = 0.5f + PAN * amp * osc (0.1666, gp * 0.5);
                tilt = 0.5f + TILT * amp * osc (0.3333, gp * 0.5);
                break;

            case Figure::Bounce:
            {
                const double ph  = std::fmod (beats * 0.375 * sp + gp * 0.5, 1.0);
                const float  tri = (float) (1.0 - std::abs (ph * 2.0 - 1.0));  // 0..1..0
                pan  = 0.5f + (tri - 0.5f) * 2.0f * PAN * amp;
                tilt = 0.5f + 0.20f * amp * osc (0.75, 0.0);
                break;
            }

            case Figure::Spread:
            {
                const float side = (gp - 0.5f) * 2.0f;                 // -1..1
                const float open = 0.5f + 0.5f * osc (0.125, 0.0);     // respiracion 0..1
                pan  = 0.5f + side * PAN * amp * open;
                tilt = 0.5f + 0.20f * amp * osc (0.1875, 0.25);
                break;
            }

            case Figure::Pendulum:
            {
                // Pendulo amplio sincronizado: recorre casi todo el rango de pan.
                pan  = 0.5f + PAN * amp * osc (0.125, 0.0);
                tilt = 0.5f + 0.14f * amp * osc (0.25, 0.0);
                break;
            }

            case Figure::Spiral:
            {
                // Circulo cuyo radio respira (abre/cierra) -> espiral visual.
                const float rad = 0.45f + 0.55f * (0.5f + 0.5f * osc (0.0625, 0.0));
                pan  = 0.5f + PAN  * amp * rad * osc (0.25, gp * 0.5);
                tilt = 0.5f + TILT * amp * rad * osc (0.25, gp * 0.5 + 0.25);
                break;
            }

            case Figure::Tide:
            {
                // Cascada de tilt: cada fixture entra escalonado (olas verticales).
                tilt = 0.5f + TILT * amp * osc (0.25, gp * 0.9);
                pan  = 0.5f + 0.16f * amp * osc (0.125, gp * 0.4);
                break;
            }

            case Figure::Zigzag:
            {
                // Pan triangular brusco + tilt alterno arriba/abajo por fixture.
                const double ph  = std::fmod (beats * 0.375 * sp, 1.0);
                const float  tri = (float) (1.0 - std::abs (ph * 2.0 - 1.0));
                pan  = 0.5f + (tri - 0.5f) * 2.0f * PAN * amp;
                tilt = 0.5f + (even ? 1.0f : -1.0f) * TILT * amp * osc (0.5, 0.0);
                break;
            }

            case Figure::Chaos:
            {
                // Headbang energetico: pan y tilt rapidos con fases distintas por
                // fixture pero DETERMINISTAS (no ruido) para que no se vea sucio.
                pan  = 0.5f + PAN  * amp * osc (0.5,  gp * 1.3);
                tilt = 0.5f + TILT * amp * osc (0.75, gp * 0.7 + 0.2);
                break;
            }

            case Figure::FanBeam:
            {
                // Abanico que ademas barre arriba/abajo todos juntos.
                const float side = (gp - 0.5f) * 2.0f;
                pan  = 0.5f + side * PAN * amp;
                tilt = 0.5f + TILT * amp * osc (0.25, 0.0);
                break;
            }

            case Figure::Wave2:
            {
                // Doble onda: pan e indice opuesto se cruzan creando trenzado.
                pan  = 0.5f + PAN  * amp * osc (0.1875, gp * 1.0);
                tilt = 0.5f + 0.24f * amp * osc (0.375, (1.0f - gp) * 1.0);
                break;
            }

            case Figure::SnapPoints:
            {
                // Salta entre 4 posiciones fijas y mantiene (cada medio beat),
                // con un pequeno asentamiento (no es instantaneo del todo).
                const double seg = std::floor (beats * 2.0 * sp);
                const float  hx  = hashLike (index, (int) seg);
                const float  hy  = hashLike (index * 3 + 1, (int) seg);
                pan  = 0.5f + (hx - 0.5f) * 2.0f * PAN  * amp;
                tilt = 0.5f + (hy - 0.5f) * 2.0f * TILT * amp;
                break;
            }

            case Figure::Auto:
            default:
                pan  = 0.5f + PAN * amp * osc (0.1875, 0.0);
                tilt = 0.5f + 0.18f * amp * osc (0.1875, 0.25);
                break;
        }

        return { juce::jlimit (0.0f, 1.0f, pan), juce::jlimit (0.0f, 1.0f, tilt) };
    }

} // namespace motionfig

