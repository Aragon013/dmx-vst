#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioAnalyzer.h"

/**
    Dibuja la forma de onda del audio analizado y lineas verticales en cada
    onset detectado (candidatos a trigger de luces).
*/
class WaveformView : public juce::Component
{
public:
    void setResult (AnalysisResult newResult)
    {
        result = std::move (newResult);
        repaint();
    }

    const AnalysisResult& getResult() const noexcept { return result; }

    /** Posicion del cursor de reproduccion, en segundos. */
    void setPlayheadSeconds (double seconds)
    {
        if (! juce::approximatelyEqual (seconds, playheadSeconds))
        {
            playheadSeconds = seconds;
            repaint();
        }
    }

    /** Callback al hacer clic en la forma de onda: entrega el tiempo (segundos). */
    std::function<void (double)> onSeek;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    double seekTimeForX (int x) const;

    AnalysisResult result;
    double playheadSeconds = 0.0;
};
