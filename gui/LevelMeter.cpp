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
    // Échelle de témoins, comme sur une console : vert, ambre au-dessus de -18 dB, rouge au-dessus
    // de -6 dB. Les témoins éteints restent visibles, en verre teinté.
    auto area = getLocalBounds().toFloat();
    const auto unit = area.getWidth() / 12.0f;
    theme::drawRecess (g, area, unit, 0.0f);
    area = area.reduced (2.0f * unit);

    const auto clipArea = area.removeFromTop (area.getWidth());
    theme::drawLed (g, clipArea, theme::alert, clipHoldRemaining > 0.0 ? 1.0f : 0.0f, unit);
    area.removeFromTop (3.0f * unit);

    constexpr int segments = 30;
    const auto pitch = area.getHeight() / static_cast<float> (segments);

    for (int i = 0; i < segments; ++i)
    {
        const auto segmentDb = juce::jmap (static_cast<float> (i) + 0.5f, 0.0f, static_cast<float> (segments), minDb, maxDb);
        const auto colour = segmentDb > -6.0f ? theme::alert : (segmentDb > -18.0f ? theme::amber : theme::medical);
        const auto isLit = displayedDb >= segmentDb;
        const auto segment = juce::Rectangle<float> (area.getX(), area.getBottom() - pitch * static_cast<float> (i + 1),
                                                     area.getWidth(), pitch).reduced (0.0f, juce::jmax (0.6f, pitch * 0.18f));

        g.setColour (isLit ? colour : colour.withAlpha (0.1f));
        g.fillRect (segment);
    }

    // Repère 0 dBFS
    const auto zeroY = area.getBottom() - area.getHeight() * proportionForDecibels (0.0f);
    g.setColour (theme::text.withAlpha (0.7f));
    g.fillRect (area.getX() - 1.0f * unit, zeroY - 0.5f, area.getWidth() + 2.0f * unit, 1.0f);
}

} // namespace subdamp::gui
