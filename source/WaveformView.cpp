#include "WaveformView.h"

void WaveformView::paint (juce::Graphics& g)
{
    const juce::Colour bg      { 0xff0f1115 };
    const juce::Colour wave    { 0xff3a7bd5 };
    const juce::Colour amber   { 0xffffb020 };
    const juce::Colour grid    { 0xff262a31 };

    auto bounds = getLocalBounds().toFloat();
    g.setColour (bg);
    g.fillRoundedRectangle (bounds, 6.0f);

    if (! result.valid || result.peaks.empty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Carga un archivo de audio para analizarlo",
                    getLocalBounds(), juce::Justification::centred, false);
        return;
    }

    const float w   = bounds.getWidth();
    const float h   = bounds.getHeight();
    const float midY = bounds.getCentreY();

    // Linea central
    g.setColour (grid);
    g.drawHorizontalLine ((int) midY, bounds.getX(), bounds.getRight());

    // Forma de onda (envolvente espejada)
    g.setColour (wave);
    const auto numPoints = (int) result.peaks.size();
    for (int x = 0; x < (int) w; ++x)
    {
        const int idx = juce::jlimit (0, numPoints - 1,
                                      (int) ((float) x / w * (float) numPoints));
        const float amp = result.peaks[(size_t) idx];
        const float half = amp * (h * 0.48f);
        g.drawVerticalLine (x + (int) bounds.getX(), midY - half, midY + half);
    }

    // Marcadores de onset (triggers candidatos)
    if (result.lengthSeconds > 0.0)
    {
        g.setColour (amber.withAlpha (0.85f));
        for (const auto t : result.onsetTimes)
        {
            const float x = (float) (t / result.lengthSeconds) * w + bounds.getX();
            g.drawVerticalLine ((int) x, bounds.getY(), bounds.getBottom());
        }
    }

    // Cursor de reproduccion (verde) sobre la forma de onda.
    if (result.lengthSeconds > 0.0)
    {
        const double frac = juce::jlimit (0.0, 1.0, playheadSeconds / result.lengthSeconds);
        const float px = (float) (frac * w) + bounds.getX();
        g.setColour (juce::Colour (0xff3bd45f).withAlpha (0.35f));
        g.drawVerticalLine ((int) px, bounds.getY(), bounds.getBottom());  // glow
        g.setColour (juce::Colour (0xff3bd45f));
        g.fillRect (px - 0.5f, bounds.getY(), 1.5f, h);
    }

    // Info
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (result.fileName,
                getLocalBounds().reduced (8).removeFromTop (18),
                juce::Justification::topLeft, true);

    g.setColour (amber);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String (result.onsetTimes.size()) + " triggers  -  "
                    + juce::String (result.lengthSeconds, 1) + " s",
                getLocalBounds().reduced (8).removeFromBottom (18),
                juce::Justification::bottomRight, false);
}

double WaveformView::seekTimeForX (int x) const
{
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    if (w <= 0.0f || result.lengthSeconds <= 0.0)
        return 0.0;

    const double frac = juce::jlimit (0.0, 1.0, (double) (x - bounds.getX()) / w);
    return frac * result.lengthSeconds;
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (result.valid && onSeek)
        onSeek (seekTimeForX (e.x));
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (result.valid && onSeek)
        onSeek (seekTimeForX (e.x));
}
