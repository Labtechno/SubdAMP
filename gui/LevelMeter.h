#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace subdamp::gui
{

/**
    Vumètre crête vertical, échelle en dB.

    Reçoit une crête par rafraîchissement de l'interface (pushPeak) et applique une
    retombée progressive, comme un vumètre matériel. Purement côté interface : ne touche
    jamais le thread audio.
*/
class LevelMeter final : public juce::Component
{
public:
    LevelMeter();

    /** À appeler à chaque rafraîchissement avec la crête mesurée depuis le précédent. */
    void pushPeak (float linearPeak, double elapsedSeconds);

    void paint (juce::Graphics& g) override;

private:
    float proportionForDecibels (float decibels) const noexcept;

    static constexpr float minDb = -60.0f;
    static constexpr float maxDb = 6.0f;
    static constexpr float falloffDbPerSecond = 24.0f;
    static constexpr double clipHoldSeconds = 1.5;

    float displayedDb = minDb;
    double clipHoldRemaining = 0.0;
};

} // namespace subdamp::gui
