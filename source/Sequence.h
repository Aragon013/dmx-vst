#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

/**
    Un keyframe de automatizacion para un canal DMX.
    El tiempo se expresa en BEATS (negras) para poder sincronizarse al BPM del DAW.
    El valor es 0..255 (rango DMX directo).
*/
struct Keyframe
{
    double timeBeats = 0.0;
    float  value     = 0.0f;   // 0..255
    bool   stepped   = false;  // true = salto (mantiene valor hasta el siguiente); false = rampa lineal
};

/** Ordena los keyframes por tiempo (ascendente). */
inline void sortKeyframes (std::vector<Keyframe>& kfs)
{
    std::sort (kfs.begin(), kfs.end(),
               [] (const Keyframe& a, const Keyframe& b) { return a.timeBeats < b.timeBeats; });
}

/**
    Valor interpolado de una lista de keyframes (asumida ordenada) en un instante dado.
    Devuelve -1.0f si no hay keyframes (sin automatizacion).
*/
inline float interpolateKeyframes (const std::vector<Keyframe>& kfs, double beats)
{
    if (kfs.empty())
        return -1.0f;

    if (beats <= kfs.front().timeBeats)
        return kfs.front().value;

    if (beats >= kfs.back().timeBeats)
        return kfs.back().value;

    // Busqueda binaria del primer keyframe con timeBeats >= beats (kfs ordenada).
    // Con 88 canales de barra de pixeles hay decenas de miles de keyframes; un
    // escaneo lineal por frame seria muy costoso.
    const auto it = std::lower_bound (kfs.begin(), kfs.end(), beats,
                                      [] (const Keyframe& k, double b) { return k.timeBeats < b; });

    const size_t i = (size_t) (it - kfs.begin());
    const auto& a = kfs[i - 1];
    const auto& b = kfs[i];

    if (a.stepped)
        return a.value;

    const double span = b.timeBeats - a.timeBeats;
    if (span <= 0.0)
        return b.value;

    const double t = (beats - a.timeBeats) / span;
    return (float) (a.value + (b.value - a.value) * t);
}

/**
    Forma de onda de un clip de efecto (un LFO musical sobre un canal DMX).
*/
enum class EffectType
{
    Sine, Triangle, SawUp, SawDown, Square, Random
};

/**
    Un clip de efecto: region temporal [start, start+length) en beats que genera
    un valor modulado (LFO) sobre un canal, "encima" de los keyframes.
    El valor oscila entre low y high (0..255) con la forma de onda elegida,
    repitiendo un ciclo completo cada periodBeats.
*/
struct EffectClip
{
    double     startBeats  = 0.0;
    double     lengthBeats = 4.0;
    EffectType type        = EffectType::Sine;
    double     periodBeats = 1.0;   // duracion de un ciclo completo de la onda
    float      low         = 0.0f;  // salida en el minimo de la onda (0..255)
    float      high        = 255.0f;// salida en el maximo de la onda
    double     phase       = 0.0;   // 0..1 desplazamiento de fase
};

/** Forma de onda normalizada 0..1 para una posicion de fase frac (0..1).
    periodIndex (ciclo entero actual) se usa para el sample&hold del modo Random. */
inline float effectWaveform (EffectType type, double frac, double periodIndex)
{
    frac = frac - std::floor (frac);   // 0..1
    switch (type)
    {
        case EffectType::Sine:     return 0.5f - 0.5f * (float) std::cos (frac * 2.0 * 3.14159265358979323846);
        case EffectType::Triangle: return 1.0f - (float) std::abs (2.0 * frac - 1.0);
        case EffectType::SawUp:    return (float) frac;
        case EffectType::SawDown:  return 1.0f - (float) frac;
        case EffectType::Square:   return frac < 0.5 ? 1.0f : 0.0f;
        case EffectType::Random:
        {
            // Sample & hold: un valor pseudo-aleatorio por ciclo.
            std::uint32_t h = (std::uint32_t) ((std::int64_t) std::floor (periodIndex) * 2654435761u + 1013904223u);
            h ^= h >> 13; h *= 2246822519u; h ^= h >> 16;
            return (float) (h & 0xffffffu) / (float) 0xffffffu;
        }
    }
    return 0.0f;
}

/** Evalua un clip en un instante (beats). Devuelve -1 si esta fuera de su rango. */
inline float evaluateEffectClip (const EffectClip& clip, double beats)
{
    if (beats < clip.startBeats || beats >= clip.startBeats + clip.lengthBeats)
        return -1.0f;
    if (clip.periodBeats <= 0.0)
        return clip.low;

    const double local = beats - clip.startBeats;
    const double pos    = local / clip.periodBeats + clip.phase;
    const double frac   = pos - std::floor (pos);
    const float  w      = effectWaveform (clip.type, frac, std::floor (pos));
    return clip.low + (clip.high - clip.low) * w;
}

/** Valor combinado de un canal: los clips de efecto activos SOBRESCRIBEN los
    keyframes (modelo mixto). Si no hay clip activo, usa la interpolacion de
    keyframes. Devuelve -1 si no hay ni clip activo ni keyframes. */
inline float evaluateChannel (const std::vector<Keyframe>& kfs,
                              const std::vector<EffectClip>& clips, double beats)
{
    float clipVal = -1.0f;
    for (const auto& c : clips)
    {
        const float v = evaluateEffectClip (c, beats);
        if (v >= 0.0f)
            clipVal = v;   // gana el ultimo clip activo
    }

    if (clipVal >= 0.0f)
        return clipVal;

    return interpolateKeyframes (kfs, beats);
}

