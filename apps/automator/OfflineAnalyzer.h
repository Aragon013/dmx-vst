#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "../../source/AudioAnalyzer.h"
#include "TrackAnalysis.h"
#include "SidecarStore.h"
#include "StemSeparator.h"
#include <functional>
#include <vector>
#include <algorithm>
#include <cmath>

/**
    Analizador OFFLINE (Fase 3: analisis DSP real).

    Reutiliza `source/AudioAnalyzer` (FFT por bandas + deteccion de onsets) para
    obtener:
      - una envolvente de energia gruesa (de la forma de onda),
      - la lista de transientes (onsets) detectados,
      - un BPM estimado a partir de los intervalos entre onsets.

    Es trabajo real y cacheable. En la Fase 4 se ampliara con separacion de
    stems (ONNX). NO toca la UI: corre en un hilo de background.
*/
namespace OfflineAnalyzer
{
    /** Numero de puntos de la envolvente de energia (resolucion gruesa). */
    static constexpr int kEnergyPoints   = 512;
    /** Resolucion de la forma de onda que pedimos al AudioAnalyzer. */
    static constexpr int kWaveformPoints = 4096;

    /** Estima el BPM por histograma de intervalos entre onsets, doblado a [70,170). */
    inline double estimateBpm (const std::vector<double>& onsets)
    {
        if (onsets.size() < 4)
            return 0.0;

        constexpr double loBpm = 70.0, hiBpm = 170.0;
        constexpr int    bins  = (int) (hiBpm - loBpm); // 1 BPM por bin
        std::vector<double> hist ((size_t) bins, 0.0);

        // Acumular sobre intervalos entre onsets cercanos (hasta 4 de separacion).
        for (size_t i = 1; i < onsets.size(); ++i)
        {
            for (size_t k = 1; k <= 4 && i >= k; ++k)
            {
                const double ioi = onsets[i] - onsets[i - k];
                if (ioi <= 0.001)
                    continue;

                double bpm = 60.0 / ioi;
                while (bpm < loBpm)  bpm *= 2.0;
                while (bpm >= hiBpm) bpm /= 2.0;
                if (bpm < loBpm || bpm >= hiBpm)
                    continue;

                const int bin = juce::jlimit (0, bins - 1, (int) (bpm - loBpm));
                hist[(size_t) bin] += 1.0;
            }
        }

        // Suavizar un poco el histograma (ventana +-1).
        std::vector<double> smooth ((size_t) bins, 0.0);
        for (int b = 0; b < bins; ++b)
        {
            double s = hist[(size_t) b];
            if (b > 0)        s += 0.5 * hist[(size_t) (b - 1)];
            if (b < bins - 1) s += 0.5 * hist[(size_t) (b + 1)];
            smooth[(size_t) b] = s;
        }

        int best = 0;
        for (int b = 1; b < bins; ++b)
            if (smooth[(size_t) b] > smooth[(size_t) best])
                best = b;

        if (smooth[(size_t) best] <= 0.0)
            return 0.0;

        return loBpm + best;
    }

    /** Estima la FASE de la rejilla de beats: el offset (segundos, en [0, secPerBeat))
        donde caen los beats reales. Se calcula como la media circular de las fases
        de los onsets respecto al periodo de beat, de modo que beat 0 coincida con
        el primer beat de la musica (y no con t=0, que suele ser silencio/intro). */
    inline double estimateBeatPhase (const std::vector<double>& onsets, double bpm)
    {
        if (onsets.size() < 4 || bpm <= 0.0)
            return 0.0;

        const double secPerBeat = 60.0 / bpm;
        const double twoPi = juce::MathConstants<double>::twoPi;

        double sx = 0.0, sy = 0.0;
        for (double o : onsets)
        {
            if (o < 0.0) continue;
            const double ph = std::fmod (o, secPerBeat) / secPerBeat * twoPi;
            sx += std::cos (ph);
            sy += std::sin (ph);
        }

        if (std::abs (sx) < 1.0e-9 && std::abs (sy) < 1.0e-9)
            return 0.0;

        double ang = std::atan2 (sy, sx);   // -pi..pi
        if (ang < 0.0) ang += twoPi;
        return ang / twoPi * secPerBeat;    // 0..secPerBeat
    }

    /** Detecta las secciones estructurales del tema a partir de la envolvente de
        energia: suaviza, cuantiza en niveles relativos (bajo/medio/alto) y agrupa
        en partes contiguas, fusionando las demasiado cortas. Etiqueta intro/outro
        por posicion, subidas (build) ante un drop, y drop/verso/break por nivel. */
    inline std::vector<TrackSection> detectSections (const std::vector<float>& energy,
                                                     double lengthSeconds,
                                                     double bpm)
    {
        std::vector<TrackSection> sections;
        const int N = (int) energy.size();
        if (N < 8 || lengthSeconds <= 0.0)
            return sections;

        const double secPerPoint = lengthSeconds / (double) N;

        // 1) Suavizado con ventana de ~2 s para quedarnos con la estructura.
        const int win = juce::jlimit (1, N / 4, (int) std::round (2.0 / juce::jmax (1.0e-3, secPerPoint)));
        std::vector<float> smooth ((size_t) N, 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int a = juce::jmax (0, i - win), b = juce::jmin (N - 1, i + win);
            double s = 0.0; int n = 0;
            for (int j = a; j <= b; ++j) { s += energy[(size_t) j]; ++n; }
            smooth[(size_t) i] = (float) (s / juce::jmax (1, n));
        }

        // 2) Umbrales por percentiles (robustos frente a outliers).
        std::vector<float> sorted = smooth;
        std::sort (sorted.begin(), sorted.end());
        auto pct = [&] (double p) { return sorted[(size_t) juce::jlimit (0, N - 1, (int) std::round (p * (N - 1)))]; };
        const float lo = pct (0.25), mid = pct (0.55), hi = pct (0.82);

        auto levelOf = [&] (float v) -> int
        {
            if (v < (lo + mid) * 0.5f) return 0;   // bajo
            if (v < (mid + hi) * 0.5f) return 1;   // medio
            return 2;                              // alto
        };

        std::vector<int> lvl ((size_t) N, 0);
        for (int i = 0; i < N; ++i)
            lvl[(size_t) i] = levelOf (smooth[(size_t) i]);

        // 3) Runs contiguos del mismo nivel.
        struct Run { int level; int start; int end; };  // [start, end)
        std::vector<Run> runs;
        for (int i = 0; i < N; )
        {
            int j = i + 1;
            while (j < N && lvl[(size_t) j] == lvl[(size_t) i]) ++j;
            runs.push_back ({ lvl[(size_t) i], i, j });
            i = j;
        }

        // 4) Fusiona runs mas cortos que ~4 compases (o ~6 s) con el vecino mas parecido.
        const double minSec = (bpm > 0.0) ? juce::jmax (6.0, (60.0 / bpm) * 16.0) : 6.0;
        const int minPts = juce::jmax (2, (int) std::round (minSec / juce::jmax (1.0e-3, secPerPoint)));
        bool merged = true;
        while (merged && runs.size() > 1)
        {
            merged = false;
            for (size_t k = 0; k < runs.size(); ++k)
            {
                if (runs[k].end - runs[k].start >= minPts)
                    continue;

                // Absorbe en el vecino con nivel mas cercano (o el unico que haya).
                if (runs.size() == 1) break;
                size_t target;
                if (k == 0)                      target = 1;
                else if (k == runs.size() - 1)   target = k - 1;
                else                             target = (std::abs (runs[k - 1].level - runs[k].level)
                                                           <= std::abs (runs[k + 1].level - runs[k].level)) ? k - 1 : k + 1;

                runs[target].start = juce::jmin (runs[target].start, runs[k].start);
                runs[target].end   = juce::jmax (runs[target].end,   runs[k].end);
                runs.erase (runs.begin() + (long) k);
                merged = true;
                break;
            }
        }

        // 5) Construye secciones + nivel medio real de cada una.
        for (const auto& r : runs)
        {
            double sum = 0.0; int n = 0;
            for (int i = r.start; i < r.end; ++i) { sum += smooth[(size_t) i]; ++n; }
            TrackSection s;
            s.startSec = r.start * secPerPoint;
            s.endSec   = r.end   * secPerPoint;
            s.level    = juce::jlimit (0.0f, 1.0f, (float) (sum / juce::jmax (1, n)));
            s.type     = TrackSection::Verse;
            sections.push_back (s);
        }
        if (sections.empty())
            return sections;

        // 5b) Reescala los niveles para que la ESTRUCTURA abarque todo el rango
        //     (la parte mas floja -> ~0, el clímax -> 1). Asi el modo Full Auto
        //     aprovecha toda la progresion de estilos sin importar el volumen
        //     absoluto del tema. Solo si hay contraste real entre secciones.
        {
            float slo = 1.0f, shi = 0.0f;
            for (const auto& s : sections) { slo = juce::jmin (slo, s.level); shi = juce::jmax (shi, s.level); }
            if (shi - slo > 0.08f)
                for (auto& s : sections)
                    s.level = juce::jlimit (0.0f, 1.0f, juce::jmap (s.level, slo, shi, 0.05f, 1.0f));
        }

        // 6) Etiquetado: nivel + posicion + subidas ante un drop.
        const int last = (int) sections.size() - 1;
        for (int i = 0; i <= last; ++i)
        {
            const int rl = runs[(size_t) i].level;
            int type = (rl >= 2) ? TrackSection::Drop : (rl == 1 ? TrackSection::Verse : TrackSection::Break);

            if (i == 0 && rl <= 1)                 type = TrackSection::Intro;
            else if (i == last && rl <= 1)         type = TrackSection::Outro;

            // Subida: parte de nivel medio justo antes de un drop.
            if (i < last && rl == 1 && runs[(size_t) (i + 1)].level >= 2)
                type = TrackSection::Build;

            sections[(size_t) i].type = type;
        }

        // 7) La seccion de MAYOR energia se marca como Chorus (el climax del tema),
        //    salvo que sea una intro/outro/subida (esas conservan su rol).
        {
            int bestIdx = -1;
            float bestLvl = -1.0f;
            for (int i = 0; i <= last; ++i)
            {
                const int ty = sections[(size_t) i].type;
                if (ty == TrackSection::Intro || ty == TrackSection::Outro || ty == TrackSection::Build)
                    continue;
                if (sections[(size_t) i].level > bestLvl)
                {
                    bestLvl = sections[(size_t) i].level;
                    bestIdx = i;
                }
            }
            if (bestIdx >= 0)
                sections[(size_t) bestIdx].type = TrackSection::Chorus;
        }

        return sections;
    }

    /** Reduce la forma de onda (peaks) a una envolvente de N puntos normalizada. */
    inline std::vector<float> energyFromPeaks (const std::vector<float>& peaks, int points)
    {
        std::vector<float> energy ((size_t) points, 0.0f);
        if (peaks.empty())
            return energy;

        float peak = 1.0e-9f;
        for (int p = 0; p < points; ++p)
        {
            const size_t start = (size_t) ((juce::int64) p       * (juce::int64) peaks.size() / points);
            const size_t end   = (size_t) ((juce::int64) (p + 1) * (juce::int64) peaks.size() / points);
            double sum = 0.0;
            size_t n = 0;
            for (size_t i = start; i < end && i < peaks.size(); ++i) { sum += peaks[i]; ++n; }
            const float v = n > 0 ? (float) (sum / (double) n) : 0.0f;
            energy[(size_t) p] = v;
            peak = juce::jmax (peak, v);
        }
        for (auto& e : energy)
            e = juce::jlimit (0.0f, 1.0f, e / peak);
        return energy;
    }

    /** Calcula el CHROMA (12 clases de altura, C..B) promediado sobre toda la
        cancion a partir de las magnitudes por banda log del AudioAnalyzer.
        Da una "huella tonal" estable que define la identidad de color del tema. */
    inline std::vector<float> computeChroma (const AnalysisResult& r)
    {
        std::vector<float> chroma (12, 0.0f);
        if (r.numBands <= 0 || r.numFrames <= 0 || r.sampleRate <= 0.0 || r.bandMags.empty())
            return chroma;

        const int    numBands = r.numBands;
        const double nyquist  = r.sampleRate * 0.5;

        // Frecuencia central (Hz) y clase de altura de cada banda log.
        // El AudioAnalyzer mapea: band = log10(1 + 9*frac) * numBands, frac = bin/numBins.
        std::vector<int>   bandPitch  ((size_t) numBands, -1);
        std::vector<float> bandWeight ((size_t) numBands, 0.0f);
        for (int b = 0; b < numBands; ++b)
        {
            const double frac = (std::pow (10.0, ((double) b + 0.5) / numBands) - 1.0) / 9.0;
            const double freq = juce::jlimit (0.0, nyquist, frac * nyquist);
            if (freq < 55.0 || freq > 5000.0)   // rango util para tonalidad
                continue;
            // Nota MIDI -> clase de altura (0=C). 69 = A4 = 440 Hz.
            const double midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
            int pc = ((int) std::lround (midi)) % 12;
            if (pc < 0) pc += 12;
            bandPitch[(size_t) b]  = pc;
            bandWeight[(size_t) b] = 1.0f;
        }

        // Acumula magnitud por clase de altura sobre todos los frames.
        for (int f = 0; f < r.numFrames; ++f)
        {
            const float* mags = &r.bandMags[(size_t) f * (size_t) numBands];
            for (int b = 0; b < numBands; ++b)
            {
                const int pc = bandPitch[(size_t) b];
                if (pc >= 0)
                    chroma[(size_t) pc] += mags[b] * bandWeight[(size_t) b];
            }
        }

        // Normaliza al maximo.
        float mx = 1.0e-9f;
        for (float c : chroma) mx = juce::jmax (mx, c);
        for (auto& c : chroma) c = juce::jlimit (0.0f, 1.0f, c / mx);
        return chroma;
    }

    /** Analiza un WAV de stem y devuelve su envolvente + transientes. */
    inline bool analyseStem (const juce::File& stemFile,
                             std::vector<float>& energyOut,
                             std::vector<double>& transientsOut)
    {
        if (! stemFile.existsAsFile())
            return false;

        AudioAnalyzer analyzer;
        OnsetParams params;
        AnalysisResult r = analyzer.analyzeFile (stemFile, params, kWaveformPoints);
        if (! r.valid)
            return false;

        energyOut     = energyFromPeaks (r.peaks, kEnergyPoints);
        transientsOut = r.onsetTimes;
        return true;
    }

    inline TrackAnalysis analyse (const juce::File& file,
                                  juce::AudioFormatManager& /*formatManager*/,
                                  const std::function<bool()>& shouldAbort,
                                  StemSeparator* stemSeparator = nullptr,
                                  const juce::File& stemCacheDir = {})
    {
        TrackAnalysis a;

        if (shouldAbort && shouldAbort())
            return a;

        AudioAnalyzer analyzer;
        OnsetParams params;  // defaults razonables
        AnalysisResult r = analyzer.analyzeFile (file, params, kWaveformPoints);

        if (! r.valid)
            return a;

        if (shouldAbort && shouldAbort())
            return {};

        a.lengthSeconds    = r.lengthSeconds;
        a.sourceSize       = file.getSize();
        a.sourceModifiedMs = file.getLastModificationTime().toMilliseconds();
        a.energy           = energyFromPeaks (r.peaks, kEnergyPoints);
        a.transients       = r.onsetTimes;
        a.estimatedBpm     = estimateBpm (r.onsetTimes);
        a.beatOffset       = estimateBeatPhase (r.onsetTimes, a.estimatedBpm);
        a.chroma           = computeChroma (r);
        a.sections         = detectSections (a.energy, a.lengthSeconds, a.estimatedBpm);
        a.valid            = true;

        // Fase 4: separacion de stems (HTDemucs) + analisis por instrumento.
        // Es opcional: si falla o no esta disponible, conservamos el analisis
        // de la mezcla completa (camino mono).
        if (stemSeparator != nullptr && stemSeparator->isAvailable()
            && stemCacheDir != juce::File())
        {
            if (shouldAbort && shouldAbort())
                return a;

            const StemSet stems = stemSeparator->separate (file, stemCacheDir, shouldAbort);
            if (stems.valid)
            {
                for (const auto& named : stems.all())
                {
                    if (shouldAbort && shouldAbort())
                        break;

                    StemAnalysis sa;
                    sa.name = named.name;
                    if (analyseStem (named.file, sa.energy, sa.transients))
                        a.stems.push_back (std::move (sa));
                }
            }
        }

        return a;
    }
}
