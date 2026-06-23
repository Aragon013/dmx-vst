#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

/**
    Interfaz desacoplada para la separacion de stems (Fase 4).

    La implementacion concreta (DemucsProcessProvider) invoca el modelo HTDemucs
    como proceso externo, pero el resto del pipeline (OfflineAnalyzer /
    ChoreographyEngine) solo conoce esta interfaz, de modo que en el futuro se
    podria cambiar a un backend ONNX nativo sin tocar nada mas.

    Convencion de stems (Demucs v4): 4 pistas -> drums, bass, vocals, other.
*/
struct StemSet
{
    bool       valid = false;
    juce::File drums, bass, vocals, other;

    struct Named { juce::String name; juce::File file; };

    /** Devuelve los 4 stems con su nombre canonico, en orden fijo. */
    std::vector<Named> all() const
    {
        return { { "drums",  drums },
                 { "bass",   bass },
                 { "vocals", vocals },
                 { "other",  other } };
    }
};

class StemSeparator
{
public:
    virtual ~StemSeparator() = default;

    /** True si el backend esta disponible (p.ej. Python + demucs instalados). */
    virtual bool isAvailable() const = 0;

    /** Nombre legible del backend, para la UI. */
    virtual juce::String getBackendName() const = 0;

    /**
        Separa `input` en 4 stems, cacheando el resultado bajo `cacheDir`.
        `shouldAbort` permite cancelar (se comprueba periodicamente).
        Devuelve un StemSet invalido si falla o se cancela.
    */
    virtual StemSet separate (const juce::File& input,
                              const juce::File& cacheDir,
                              const std::function<bool()>& shouldAbort) = 0;

    /** Borra la cache de stems de `input` para forzar una re-separacion. */
    virtual void clearCache (const juce::File& /*input*/, const juce::File& /*cacheDir*/) {}
};
