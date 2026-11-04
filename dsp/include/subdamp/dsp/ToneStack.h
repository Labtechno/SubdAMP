#pragma once

#include "subdamp/dsp/Biquad.h"
#include "subdamp/dsp/MonoEffect.h"

#include <array>

namespace subdamp::dsp
{

/**
    Égaliseur 3 bandes placé après la capture d'ampli (graves, médiums, aigus, ±12 dB).

    Les captures NAM figent l'égalisation de l'ampli au moment de la capture : ce correcteur
    permet de les ajuster sans recapturer. Fréquences choisies pour la guitare :
    plateau grave 120 Hz, cloche médium 750 Hz, plateau aigu 3 kHz.
*/
class ToneStack final : public MonoEffect
{
public:
    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<float> block) noexcept override;
    LatencyTier latencyTier() const noexcept override { return LatencyTier::live; }

    void setGains (float bassDb, float midDb, float trebleDb) noexcept;

private:
    void updateCoefficients() noexcept;

    double sampleRate = 48000.0;
    Biquad bass, mid, treble;
    float bassDb = 0.0f, midDb = 0.0f, trebleDb = 0.0f;
    bool needsUpdate = true;
};

/**
    Cabinet de secours utilisé tant qu'aucune réponse impulsionnelle (IR) n'est chargée :
    approximation d'un 4×12 par filtres (coupure des graves, bosse de résonance,
    creux bas-médium, présence, chute raide au-dessus de 5 kHz).
*/
class FallbackCabinet final : public MonoEffect
{
public:
    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<float> block) noexcept override;
    LatencyTier latencyTier() const noexcept override { return LatencyTier::live; }

private:
    std::array<Biquad, 7> stages;
};

} // namespace subdamp::dsp
