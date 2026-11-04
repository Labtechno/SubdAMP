#include "gui/LevelMeter.h"

#include "gui/Theme.h"
#include "subdamp/dsp/Decibels.h"

#include <cmath>

namespace subdamp::gui
{

LevelMeter::LevelMeter()
{
    setOpaque (false);
}

void LevelMeter::pushPeak (float linearPeak, double elapsedSeconds)
{
    const auto peakDb = juce::jlimit (minDb, maxDb, dsp::gainToDecibels (linearPeak));
    const auto fallenDb = displayedDb - falloffDbPerSecond * static_cast<float> (elapsedSeconds);
    const auto newDb = juce::jmax (peakDb, fallenDb, minDb);

    clipHoldRemaining = linearPeak >= 1.0f ? clipHoldSeconds
                                           : juce::jmax (0.0, clipHoldRemaining - elapsedSeconds);

    // Pas de repaint tant que l'affichage ne bouge pas de façon visible.
    if (std::abs (newDb - displayedDb) > 0.05f || clipHoldRemaining > 0.0)
    {
        displayedDb = newDb;
        repaint();
    }
}

float LevelMeter::proportionForDecibels (float decibels) const noexcept
{
    return juce::jmap (decibels, minDb, maxDb, 0.0f, 1.0f);
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto corner = area.getWidth() * 0.3f;

    g.setColour (theme::meterBackground);
    g.fillRoundedRectangle (area, corner);

    auto clipArea = area.removeFromTop (area.getHeight() * 0.06f);
    area.removeFromTop (2.0f);

    // Zone de niveau, avec les seuils -18 dB (orange) et -6 dB (rouge) en couleur fixe.
    const auto levelHeight = area.getHeight() * proportionForDecibels (displayedDb);
    const auto level = area.withTop (area.getBottom() - levelHeight);

    juce::ColourGradient gradient (theme::meterLow, area.getBottomLeft(), theme::meterHigh, area.getTopLeft(), false);
    gradient.addColour (proportionForDecibels (-18.0f), theme::meterLow);
    gradient.addColour (proportionForDecibels (-6.0f), theme::meterMid);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (level, corner);

    // Repère 0 dBFS
    const auto zeroY = area.getBottom() - area.getHeight() * proportionForDecibels (0.0f);
    g.setColour (theme::textDim);
    g.drawHorizontalLine (juce::roundToInt (zeroY), area.getX(), area.getRight());

    g.setColour (clipHoldRemaining > 0.0 ? theme::meterHigh : theme::meterBackground.brighter (0.15f));
    g.fillRoundedRectangle (clipArea, corner);
}

} // namespace subdamp::gui
