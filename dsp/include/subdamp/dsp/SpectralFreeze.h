#pragma once

#include "subdamp/dsp/Fft.h"
#include "subdamp/dsp/StereoTexture.h"

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace subdamp::dsp
{

/**
    FREEZE : gel spectral. Au déclenchement, le spectre des ~170 dernières millisecondes est
    capturé ; il est ensuite rejoué indéfiniment en nappe, pendant que tu continues à jouer.

    Resynthèse : à chaque pas (1/4 de fenêtre), le spectre capturé reçoit des phases
    aléatoires, est ramené dans le temps par FFT inverse, fenêtré (Hann) et superposé aux
    trames précédentes. Les grandes fenêtres donnent une nappe lisse et tonale ; des tirages
    aléatoires différents à gauche et à droite la rendent large.

    Fondu d'entrée et de sortie réglables : la nappe peut gonfler lentement, puis s'éteindre
    quand on libère le gel.
*/
class SpectralFreeze final : public StereoTexture
{
public:
    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept override;

    /** Vrai : fige (capture au passage à vrai), faux : libère en fondu. */
    void setHold (bool shouldHold) noexcept { hold = shouldHold; }
    void setAttackMilliseconds (float ms) noexcept;
    void setReleaseMilliseconds (float ms) noexcept;
    /** Coupure du filtre de tonalité appliqué à la nappe (Hz). */
    void setToneHz (float hz) noexcept { toneHz = hz; }

    bool isFrozen() const noexcept { return hasCapture && gain > 0.0f; }

private:
    void capture() noexcept;
    void synthesiseFrame() noexcept;
    float nextRandomPhase (std::uint32_t& state) noexcept;

    double sampleRate = 48000.0;
    int size = 8192, hop = 2048;
    Fft fft;

    std::vector<float> window;
    std::vector<float> history;      // dernières `size` entrées (anneau)
    std::size_t historyIndex = 0;

    std::vector<float> magnitudes;   // spectre capturé (size / 2 + 1 cases)
    std::vector<std::complex<float>> frame;

    // Sortie : superposition de trames, une par canal
    std::array<std::vector<float>, 2> overlap;
    std::size_t overlapIndex = 0;
    int samplesUntilFrame = 0;
    std::array<std::uint32_t, 2> randomState { 0x12345678u, 0x9abcdef1u };

    bool hold = false, wasHolding = false, hasCapture = false;
    float gain = 0.0f, attackStep = 0.001f, releaseStep = 0.0001f;
    float toneHz = 8000.0f;
    float normalisation = 1.0f;
};

} // namespace subdamp::dsp
