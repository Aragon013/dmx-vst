#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

/**
    Parametros ajustables de la deteccion de onsets (triggers).
    Cambiarlos NO requiere recargar el archivo: solo re-ejecuta el peak picking
    sobre las magnitudes por banda ya calculadas.
*/
struct OnsetParams
{
    float  delta         = 0.06f;  // cuanto debe superar a la media local (sensibilidad)
    float  threshold     = 0.10f;  // flux minimo absoluto para considerar un pico
    int    neighbourhood = 8;      // frames a cada lado para la media local
    double minSpacingSec = 0.10;   // separacion minima entre triggers (anti dobles)
    float  lowCut        = 0.0f;   // [0..1] limite inferior de banda analizada
    float  highCut       = 1.0f;   // [0..1] limite superior de banda analizada
    int    smoothing     = 1;      // suavizado de la curva de flux (media movil, frames)
    float  gain          = 1.0f;   // ganancia aplicada a la curva de flux antes de normalizar
};

/**
    Resultado de analizar un archivo de audio (offline).

    - peaks: envolvente de amplitud submuestreada para dibujar la forma de onda.
    - onsetTimes: instantes (segundos) donde se detecto un cambio/transiente.
    - bandMags: magnitudes por banda y por frame (para re-detectar al cambiar params).
    - flux/fluxTimes: ultima curva de spectral flux calculada (para dibujar).
*/
struct AnalysisResult
{
    double sampleRate     = 0.0;
    double lengthSeconds  = 0.0;
    juce::String fileName;
    std::vector<float>  peaks;       // [0..1], envolvente para la waveform
    std::vector<double> onsetTimes;  // segundos

    int                 numBands  = 0;
    int                 numFrames = 0;
    std::vector<float>  bandMags;    // flat: frame * numBands + band
    std::vector<double> frameTimes;  // segundos de cada frame

    std::vector<float>  flux;        // ultima curva de flux normalizada [0..1]
    bool valid = false;
};

/**
    Analizador offline: carga un archivo de audio completo en memoria y calcula
    la forma de onda y las magnitudes espectrales por banda. Los onsets se
    detectan (y re-detectan) a partir de esas magnitudes segun los parametros.

    No es tiempo real; se ejecuta a peticion del usuario sobre un archivo.
*/
class AudioAnalyzer
{
public:
    AudioAnalyzer();

    /** Carga y analiza el archivo. waveformPoints = resolucion de la forma de onda. */
    AnalysisResult analyzeFile (const juce::File& file,
                                const OnsetParams& params,
                                int waveformPoints = 2000);

    /** Analiza una senal mono ya en memoria (misma logica que analyzeFile, sin
        leer de disco). Util para analizar audio capturado del DAW en vivo. */
    static AnalysisResult analyzeMono (const float* mono, int numSamples, double sampleRate,
                                       const OnsetParams& params, int waveformPoints = 2000);

    /** Re-detecta onsets a partir de las magnitudes por banda ya calculadas (rapido). */
    static void recomputeOnsets (AnalysisResult& result, const OnsetParams& params);

private:
    juce::AudioFormatManager formatManager;
};

