#pragma once

#include "subdamp/dsp/LinearSmoother.h"
#include "subdamp/dsp/MonoEffect.h"

#include <array>

namespace subdamp::dsp
{

/**
    Décaleur de fréquence (type Bode) : module FREQ SHIFT, latence quasi nulle, polyphonique.

    Contrairement à un pitch shifter, il ajoute le même nombre de Hz à toutes les composantes :
    les rapports harmoniques sont cassés, d'où les sons métalliques, cloches, ou « barber pole »
    avec la réinjection.

    Le signal analytique est obtenu par deux chaînes de passe-tout dont les sorties sont en
    quadrature (coefficients d'Olli Niemitalo, 4 cellules par chaîne, ±0,7° de 20 Hz à 20 kHz),
    puis multiplié par un oscillateur en quadrature (modulation à bande latérale unique).
*/
class FrequencyShifter final : public MonoEffect
{
public:
    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<float> block) noexcept override;
    LatencyTier latencyTier() const noexcept override { return LatencyTier::live; }

    /** Décalage en Hz : positif vers l'aigu, négatif vers le grave. */
    void setShiftHz (float hz) noexcept;
    /** Proportion de signal décalé (0 à 1). */
    void setMix (float mix) noexcept;
    /** Réinjection de la sortie décalée dans l'entrée (0 à 0,95). */
    void setFeedback (float feedback) noexcept;

private:
    /** Chaîne de 4 passe-tout en z⁻² : y[n] = a²·(x[n] + y[n−2]) − x[n−2]. */
    struct AllpassChain
    {
        std::array<float, 4> coefficients {};
        std::array<std::array<float, 4>, 4> state {}; // x[n−1], x[n−2], y[n−1], y[n−2] par cellule

        float process (float x) noexcept;
        void reset() noexcept { state = {}; }
    };

    void updateOscillatorStep() noexcept;

    double sampleRate = 48000.0;
    AllpassChain inPhase, quadrature;
    float delayedInPhase = 0.0f;

    double shiftHz = 0.0;
    double oscillatorCos = 1.0, oscillatorSin = 0.0;
    double stepCos = 1.0, stepSin = 0.0;

    LinearSmoother mix, feedback;
    float previousOutput = 0.0f;
};

} // namespace subdamp::dsp
