#pragma once

#include "subdamp/dsp/MonoEffect.h"

namespace subdamp::dsp
{

/**
    Noise gate pour guitare, placé en entrée de chaîne.

    - Détection crête à attaque instantanée : le gate s'ouvre dès la première attaque de médiator.
    - Hystérésis de 6 dB et temps de maintien de 50 ms : pas de battement (« chatter »)
      quand une note qui s'éteint oscille autour du seuil.
    - Ouverture en 0,5 ms, fermeture réglable : la fin des notes n'est pas coupée net.
*/
class NoiseGate final : public MonoEffect
{
public:
    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<float> block) noexcept override;
    LatencyTier latencyTier() const noexcept override { return LatencyTier::live; }

    void setThresholdDecibels (float decibels) noexcept;
    void setReleaseMilliseconds (float milliseconds) noexcept;

    /** Vrai si le gate laisse passer le signal (pour un témoin dans l'interface). */
    bool isOpen() const noexcept { return open; }

private:
    float coefficientFor (double milliseconds) const noexcept;

    static constexpr float hysteresisDb = 6.0f;
    static constexpr double holdMs = 50.0;
    static constexpr double openMs = 0.5;
    static constexpr double detectorReleaseMs = 10.0;

    double sampleRate = 48000.0;
    float openThreshold = 0.001f;
    float closeThreshold = 0.0005f;
    float releaseMs = 100.0f;

    float detectorCoefficient = 0.0f;
    float openCoefficient = 0.0f;
    float closeCoefficient = 0.0f;
    int holdSamples = 0;

    float envelope = 0.0f;
    float gain = 0.0f;
    int holdRemaining = 0;
    bool open = false;
};

} // namespace subdamp::dsp
