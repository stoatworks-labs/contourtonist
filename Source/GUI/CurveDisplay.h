// SPDX-License-Identifier: MIT
#pragma once

#include "../DSP/EqualLoudness.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace contourtonist::gui
{

/**
    Draws the compensation curve against a log-frequency, linear-decibel grid.

    Shows the target curve and, faintly behind it, the reference position — flat — so
    that "no compensation" is a visible state rather than an empty graph. The curve is
    drawn from the contour rather than from the fitted filter response on purpose: the
    two agree to a few tenths of a dB, and if they ever stop agreeing the number to look
    at is the reported fit error, not a line on a graph that is too thick to show it.
*/
class CurveDisplay : public juce::Component
{
public:
    CurveDisplay();

    void paint (juce::Graphics&) override;

    /** Update the curve being shown. Triggers a repaint only if something changed
        enough to be visible, so a static curve costs nothing. */
    void setCurve (const loudness::Contour& curve, bool extrapolated);

    /** Grey the display out and say why. */
    void setInactive (bool inactive, const juce::String& reason);

private:
    float frequencyToX (double frequencyHz) const;
    float gainToY (double gainDb) const;

    loudness::Contour contour = loudness::compensationCurve (100.0, 100.0);
    bool isExtrapolated = false;
    bool inactive = false;
    juce::String inactiveReason;

    double displayRangeDb = 12.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveDisplay)
};

} // namespace contourtonist::gui
