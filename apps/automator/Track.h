#pragma once

#include <juce_core/juce_core.h>
#include "TrackAnalysis.h"
#include "DmxShow.h"

/**
    Un tema de la playlist del AI Automator.

    En la Fase 1 solo guardamos los datos basicos (archivo, nombre, duracion) y
    un estado. La Fase 2 anade un id estable, el resultado del pre-proceso
    (TrackAnalysis) y un mensaje de error opcional.
*/
struct Track
{
    /** Ciclo de vida de un tema dentro del pipeline de pre-proceso. */
    enum class State
    {
        Pending,    // recien anadido, aun sin procesar
        Ready,      // metadata leida; listo para reproducir
        Decoding,   // (futuro) decodificando a PCM en background
        Analyzing,  // analisis DSP / (futuro) separacion de stems
        Choreographed, // (futuro) ya tiene su DmxShow
        Error       // no se pudo leer
    };

    /** Fuente de la coreografia que se reproduce para este tema.
        Auto   = la generada por la IA (Track::show, volatil, se regenera).
        Manual = la editada en el piano roll (Track::manualShow, persistente). */
    enum class ChoreoMode { Auto, Manual };

    int          id            = 0;   // identificador estable (no cambia al reordenar)
    juce::File   file;
    juce::String displayName;
    double       lengthSeconds = 0.0;
    State        state         = State::Pending;
    juce::String errorMessage;

    TrackAnalysis analysis;           // resultado del pre-proceso (Fase 2+)
    DmxShow       show;               // coreografia DMX generada por IA (Fase 5+)

    ChoreoMode    choreoMode = ChoreoMode::Auto;  // que fuente se reproduce
    DmxShow       manualShow;         // coreografia editada a mano (piano roll, Fase 4)

    /** "mm:ss" para mostrar en la lista. */
    juce::String lengthString() const
    {
        if (lengthSeconds <= 0.0)
            return "--:--";

        const int total = (int) (lengthSeconds + 0.5);
        return juce::String (total / 60).paddedLeft ('0', 2)
             + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    static juce::String stateLabel (State s)
    {
        switch (s)
        {
            case State::Pending:       return "Pendiente";
            case State::Ready:         return "Listo";
            case State::Decoding:      return "Decodificando";
            case State::Analyzing:     return "Analizando";
            case State::Choreographed: return "Coreografiado";
            case State::Error:         return "Error";
        }
        return {};
    }
};
