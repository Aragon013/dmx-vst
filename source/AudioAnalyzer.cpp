#include "AudioAnalyzer.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

AudioAnalyzer::AudioAnalyzer()
{
    formatManager.registerBasicFormats();
}

AnalysisResult AudioAnalyzer::analyzeFile (const juce::File& file,
                                           const OnsetParams& params,
                                           int waveformPoints)
{
    AnalysisResult result;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return result;

    const int numSamples  = (int) reader->lengthInSamples;
    const int numChannels = (int) reader->numChannels;

    if (numSamples <= 0 || numChannels <= 0 || reader->sampleRate <= 0.0)
        return result;

    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    reader->read (&buffer, 0, numSamples, 0, true, true);

    // --- Suma a mono ---
    std::vector<float> mono ((size_t) numSamples, 0.0f);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* d = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            mono[(size_t) i] += d[i];
    }
    const float invCh = 1.0f / (float) numChannels;
    for (auto& s : mono)
        s *= invCh;

    result = analyzeMono (mono.data(), numSamples, reader->sampleRate, params, waveformPoints);
    result.fileName = file.getFileName();
    return result;
}

AnalysisResult AudioAnalyzer::analyzeMono (const float* mono, int numSamples, double sampleRate,
                                           const OnsetParams& params, int waveformPoints)
{
    AnalysisResult result;

    if (mono == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return result;

    result.sampleRate    = sampleRate;
    result.lengthSeconds = (double) numSamples / sampleRate;

    // --- Forma de onda submuestreada (envolvente de pico) ---
    waveformPoints = juce::jmax (1, waveformPoints);
    result.peaks.assign ((size_t) waveformPoints, 0.0f);
    const int samplesPerPoint = juce::jmax (1, numSamples / waveformPoints);
    for (int p = 0; p < waveformPoints; ++p)
    {
        const int start = p * samplesPerPoint;
        const int end   = juce::jmin (numSamples, start + samplesPerPoint);
        float peak = 0.0f;
        for (int i = start; i < end; ++i)
            peak = juce::jmax (peak, std::abs (mono[(size_t) i]));
        result.peaks[(size_t) p] = juce::jmin (1.0f, peak);
    }

    // --- Magnitudes espectrales por banda (log) y por frame ---
    const int fftOrder = 10;            // 1024 muestras por ventana
    const int fftSize  = 1 << fftOrder;
    const int hop      = fftSize / 2;   // solape 50%
    const int numBins  = fftSize / 2;
    const int numBands = 64;            // bandas log para el analisis por frecuencia

    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
                                                juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> fftBuffer ((size_t) fftSize * 2, 0.0f);

    result.numBands = numBands;
    result.bandMags.clear();
    result.frameTimes.clear();

    // Precalcula a que banda log pertenece cada bin.
    std::vector<int> binToBand ((size_t) numBins, 0);
    for (int bin = 0; bin < numBins; ++bin)
    {
        const float frac = (float) bin / (float) numBins;          // 0..1
        int band = (int) (std::log10 (1.0f + 9.0f * frac) * numBands); // log 0..numBands
        binToBand[(size_t) bin] = juce::jlimit (0, numBands - 1, band);
    }

    std::vector<float> bandMag ((size_t) numBands, 0.0f);

    for (int pos = 0; pos + fftSize <= numSamples; pos += hop)
    {
        std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
            fftBuffer[(size_t) i] = mono[(size_t) (pos + i)];

        window.multiplyWithWindowingTable (fftBuffer.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftBuffer.data());

        std::fill (bandMag.begin(), bandMag.end(), 0.0f);
        for (int bin = 1; bin < numBins; ++bin)
            bandMag[(size_t) binToBand[(size_t) bin]] += fftBuffer[(size_t) bin];

        result.bandMags.insert (result.bandMags.end(), bandMag.begin(), bandMag.end());
        result.frameTimes.push_back ((double) (pos + fftSize / 2) / sampleRate);
    }

    result.numFrames = (int) result.frameTimes.size();
    result.valid = true;

    // Deteccion de onsets (peak picking) con los parametros dados.
    recomputeOnsets (result, params);
    return result;
}

void AudioAnalyzer::recomputeOnsets (AnalysisResult& result, const OnsetParams& params)
{
    result.onsetTimes.clear();
    result.flux.clear();

    const int numBands  = result.numBands;
    const int numFrames = result.numFrames;
    if (numBands <= 0 || numFrames <= 1)
        return;

    const int loBand = juce::jlimit (0, numBands - 1, (int) (params.lowCut  * numBands));
    const int hiBand = juce::jlimit (loBand + 1, numBands, (int) (params.highCut * numBands));

    // --- Spectral flux por frame en la banda seleccionada ---
    std::vector<float>& flux = result.flux;
    flux.assign ((size_t) numFrames, 0.0f);

    for (int f = 1; f < numFrames; ++f)
    {
        const float* cur  = &result.bandMags[(size_t) f * numBands];
        const float* prev = &result.bandMags[(size_t) (f - 1) * numBands];
        float acc = 0.0f;
        for (int b = loBand; b < hiBand; ++b)
        {
            const float diff = cur[b] - prev[b];
            if (diff > 0.0f)
                acc += diff;
        }
        flux[(size_t) f] = acc * params.gain;
    }

    // --- Suavizado (media movil) ---
    const int sm = juce::jmax (0, params.smoothing);
    if (sm > 0)
    {
        std::vector<float> smoothed ((size_t) numFrames, 0.0f);
        for (int f = 0; f < numFrames; ++f)
        {
            const int a = juce::jmax (0, f - sm);
            const int b = juce::jmin (numFrames - 1, f + sm);
            float s = 0.0f;
            for (int k = a; k <= b; ++k)
                s += flux[(size_t) k];
            smoothed[(size_t) f] = s / (float) (b - a + 1);
        }
        flux.swap (smoothed);
    }

    // --- Normaliza a [0..1] ---
    float maxFlux = 0.0f;
    for (auto v : flux)
        maxFlux = juce::jmax (maxFlux, v);
    if (maxFlux > 0.0f)
        for (auto& v : flux)
            v /= maxFlux;

    // --- Peak picking: maximo local por encima de la media local + delta ---
    const int    neighbourhood = juce::jmax (1, params.neighbourhood);
    const float  delta         = params.delta;
    const float  threshold     = params.threshold;
    const double minSpacingSec = juce::jmax (0.0, params.minSpacingSec);
    double lastOnset = -1.0e9;

    for (int i = 1; i + 1 < numFrames; ++i)
    {
        const float v = flux[(size_t) i];

        if (v < flux[(size_t) (i - 1)] || v < flux[(size_t) (i + 1)])
            continue;                   // no es maximo local

        const int a = juce::jmax (0, i - neighbourhood);
        const int b = juce::jmin (numFrames - 1, i + neighbourhood);
        float mean = 0.0f;
        for (int k = a; k <= b; ++k)
            mean += flux[(size_t) k];
        mean /= (float) (b - a + 1);

        if (v >= mean + delta && v > threshold)
        {
            const double t = result.frameTimes[(size_t) i];
            if (t - lastOnset >= minSpacingSec)
            {
                result.onsetTimes.push_back (t);
                lastOnset = t;
            }
        }
    }
}
