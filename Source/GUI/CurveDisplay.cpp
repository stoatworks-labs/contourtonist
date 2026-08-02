// SPDX-License-Identifier: MIT
#include "CurveDisplay.h"

#include <cmath>

namespace contourtonist::gui
{
namespace
{
constexpr double minFrequency = 20.0;
constexpr double maxFrequency = 20000.0;

const int gridFrequencies[] { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
} // namespace

CurveDisplay::CurveDisplay()
{
    setOpaque (false);
}

float CurveDisplay::frequencyToX (double frequencyHz) const
{
    const auto bounds = getLocalBounds().toFloat().reduced (34.0f, 12.0f);
    const double lo = std::log (minFrequency);
    const double hi = std::log (maxFrequency);
    const double t = (std::log (juce::jlimit (minFrequency, maxFrequency, frequencyHz)) - lo)
                   / (hi - lo);

    return bounds.getX() + (float) t * bounds.getWidth();
}

float CurveDisplay::gainToY (double gainDb) const
{
    const auto bounds = getLocalBounds().toFloat().reduced (34.0f, 12.0f);
    const double t = (gainDb + displayRangeDb) / (2.0 * displayRangeDb);

    return bounds.getBottom() - (float) juce::jlimit (0.0, 1.0, t) * bounds.getHeight();
}

void CurveDisplay::setCurve (const loudness::Contour& curve, bool extrapolated)
{
    // Only repaint when the curve has moved by something a human could see. The
    // controller updates thirty times a second and rate-limits to half a dB per second,
    // so most updates change the picture by a thousandth of a dB.
    bool changed = extrapolated != isExtrapolated;

    for (std::size_t i = 0; i < loudness::numTabulated && ! changed; ++i)
        changed = std::abs (curve.gainDb[i] - contour.gainDb[i]) > 0.02;

    contour = curve;
    isExtrapolated = extrapolated;

    // Keep the vertical scale on sensible round numbers, and only ever grow it during a
    // show — a scale that rescales itself downward makes the curve appear to jump when
    // it did not.
    const double needed = std::max (6.0, std::ceil (curve.maxAbsGainDb() / 3.0) * 3.0);

    if (needed > displayRangeDb)
    {
        displayRangeDb = needed;
        changed = true;
    }

    if (changed)
        repaint();
}

void CurveDisplay::setInactive (bool shouldBeInactive, const juce::String& reason)
{
    if (inactive == shouldBeInactive && inactiveReason == reason)
        return;

    inactive = shouldBeInactive;
    inactiveReason = reason;
    repaint();
}

void CurveDisplay::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    const auto plot = full.reduced (34.0f, 12.0f);

    g.setColour (juce::Colour (0xff14161a));
    g.fillRoundedRectangle (full, 4.0f);

    // --- Grid ------------------------------------------------------------------------
    g.setFont (juce::FontOptions (10.0f));

    for (const int f : gridFrequencies)
    {
        const float x = frequencyToX ((double) f);

        g.setColour (juce::Colour (0xff262a31));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());

        g.setColour (juce::Colour (0xff6b7280));
        const juce::String label = f >= 1000 ? juce::String (f / 1000) + "k" : juce::String (f);
        g.drawText (label, (int) x - 16, (int) plot.getBottom() + 1, 32, 11,
                    juce::Justification::centred);
    }

    const double step = displayRangeDb <= 6.0 ? 3.0 : (displayRangeDb <= 12.0 ? 3.0 : 6.0);

    for (double db = -displayRangeDb; db <= displayRangeDb + 0.001; db += step)
    {
        const float y = gainToY (db);
        const bool isZero = std::abs (db) < 0.001;

        g.setColour (isZero ? juce::Colour (0xff3d434d) : juce::Colour (0xff262a31));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());

        g.setColour (juce::Colour (0xff6b7280));
        g.drawText (juce::String (db > 0 ? "+" : "") + juce::String ((int) db),
                    2, (int) y - 6, 30, 12, juce::Justification::centredRight);
    }

    // --- The curve --------------------------------------------------------------------
    juce::Path path;
    bool started = false;

    for (double f = minFrequency; f <= maxFrequency; f *= 1.02)
    {
        const float x = frequencyToX (f);
        const float y = gainToY (contour.gainAt (f));

        if (! started) { path.startNewSubPath (x, y); started = true; }
        else            path.lineTo (x, y);
    }

    if (inactive)
    {
        g.setColour (juce::Colour (0xff3d434d));
        g.strokePath (path, juce::PathStrokeType (1.5f));

        g.setColour (juce::Colour (0xff8b929e));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (inactiveReason, plot.toNearestInt(), juce::Justification::centred);
        return;
    }

    // Fill between the curve and the zero line, so the direction of the correction
    // reads at a glance rather than needing the axis labels.
    juce::Path fill = path;
    fill.lineTo (frequencyToX (maxFrequency), gainToY (0.0));
    fill.lineTo (frequencyToX (minFrequency), gainToY (0.0));
    fill.closeSubPath();

    g.setColour (juce::Colour (isExtrapolated ? 0x22e0a03a : 0x2245b0e8));
    g.fillPath (fill);

    g.setColour (juce::Colour (isExtrapolated ? 0xffe0a03a : 0xff45b0e8));
    g.strokePath (path, juce::PathStrokeType (2.0f));

    if (isExtrapolated)
    {
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("extrapolated beyond ISO 226 range",
                    plot.toNearestInt().reduced (4), juce::Justification::topRight);
    }
}

} // namespace contourtonist::gui
