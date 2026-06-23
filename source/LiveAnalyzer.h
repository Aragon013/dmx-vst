#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

/**
    Analisis de audio EN VIVO (tiempo real), pensado para correr dentro de
    processBlock sin coste apreciable.

    Calcula tres senales utiles para iluminacion:
      - level:    envolvente de amplitud full-range (volumen general)  [0..1]
      - bass:     envolvente de la banda grave (kick/bombo)            [0..1]
      - transient: pulso (1.0) cuando detecta un golpe en los graves; decae solo

    No usa FFT: usa RMS por muestra suavizado (attack/release) + un filtro de
    graves one-pole. La deteccion de transitorios compara la envolvente rapida
    de graves contra una media lenta (flux positivo) con histeresis.
*/
class LiveAnalyzer
{
public:
    LiveAnalyzer() = default;

    void prepare (double sampleRateIn)
    {
        sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;

        // Coeficientes de suavizado (en segundos -> factor por muestra).
        levelAttack  = coef (0.005);   // 5 ms
        levelRelease = coef (0.120);   // 120 ms
        bassAttack   = coef (0.004);
        bassRelease  = coef (0.180);
        slowRelease  = coef (0.350);   // media lenta para el umbral de transiente

        // Filtro paso-bajo one-pole para aislar graves (~150 Hz).
        const double fc = 150.0;
        lowpassCoef = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * fc / sampleRate);

        reset();
    }

    void reset()
    {
        levelEnv = bassEnv = slowBassEnv = 0.0f;
        lpState = 0.0f;
        transientHold = 0.0f;
        armed = true;
    }

    /** Procesa un bloque (solo lectura del audio). Llamar desde processBlock. */
    void process (const juce::AudioBuffer<float>& buffer)
    {
        const int numCh = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        if (numCh <= 0 || numSamples <= 0)
            return;

        for (int n = 0; n < numSamples; ++n)
        {
            // Mono sum
            float mono = 0.0f;
            for (int c = 0; c < numCh; ++c)
                mono += buffer.getReadPointer (c)[n];
            mono /= (float) numCh;

            const float rect = std::abs (mono);

            // Envolvente full-range (attack/release).
            levelEnv += (rect - levelEnv) * (rect > levelEnv ? levelAttack : levelRelease);

            // Graves: lowpass one-pole, luego envolvente.
            lpState = (1.0f - lowpassCoef) * mono + lowpassCoef * lpState;
            const float bassRect = std::abs (lpState);
            bassEnv += (bassRect - bassEnv) * (bassRect > bassEnv ? bassAttack : bassRelease);

            // Media lenta de graves (referencia para el transiente).
            slowBassEnv += (bassEnv - slowBassEnv) * slowRelease;

            // Deteccion de transiente: la envolvente de graves supera la media lenta
            // por un factor, con histeresis (rearme) para no disparar en rafaga.
            const float threshold = slowBassEnv * transientRatio + transientFloor;
            if (armed && bassEnv > threshold)
            {
                transientHold = 1.0f;
                ++transientCount;
                armed = false;
            }
            else if (bassEnv < slowBassEnv * 0.9f + 0.001f)
            {
                armed = true;
            }

            // Decaimiento del pulso de transiente.
            transientHold *= transientDecay;
        }

        // Publicar (atomico) para la UI / motor DMX.
        level.store (juce::jlimit (0.0f, 1.0f, levelEnv * makeupGain));
        bass.store  (juce::jlimit (0.0f, 1.0f, bassEnv  * makeupGain));
        transient.store (juce::jlimit (0.0f, 1.0f, transientHold));
    }

    // Lecturas (hilo de mensajes / DMX).
    float getLevel()     const noexcept { return level.load(); }
    float getBass()      const noexcept { return bass.load(); }
    float getTransient() const noexcept { return transient.load(); }
    int   getTransientCount() const noexcept { return transientCount.load(); }

    // Parametros ajustables.
    float makeupGain     = 3.0f;    // realce general (audio suele venir bajo)
    float transientRatio = 1.6f;    // cuanto debe superar a la media lenta
    float transientFloor = 0.01f;   // suelo absoluto para evitar disparos en silencio

private:
    float coef (double seconds) const
    {
        return 1.0f - (float) std::exp (-1.0 / (juce::jmax (1e-4, seconds) * sampleRate));
    }

    double sampleRate = 44100.0;

    float levelAttack = 0.0f, levelRelease = 0.0f;
    float bassAttack  = 0.0f, bassRelease  = 0.0f;
    float slowRelease = 0.0f;
    float lowpassCoef = 0.0f;

    float levelEnv = 0.0f, bassEnv = 0.0f, slowBassEnv = 0.0f;
    float lpState  = 0.0f;
    float transientHold = 0.0f;
    static constexpr float transientDecay = 0.90f;
    bool  armed = true;

    std::atomic<float> level     { 0.0f };
    std::atomic<float> bass      { 0.0f };
    std::atomic<float> transient { 0.0f };
    std::atomic<int>   transientCount { 0 };
};
