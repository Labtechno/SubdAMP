#include "subdamp/dsp/SpectralFreeze.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace subdamp::dsp
{

void SpectralFreeze::prepare (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    // ~170 ms de fenêtre : 8192 points à 44,1 / 48 kHz, 16384 à 88,2 / 96 kHz.
    size = static_cast<int> (std::bit_ceil (static_cast<unsigned> (sampleRate * 0.12)));
    hop = size / 4;
    fft.prepare (size);

    const auto n = static_cast<std::size_t> (size);
    window.resize (n);

    for (std::size_t i = 0; i < n; ++i)
        window[i] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (n)));

    history.assign (n, 0.0f);
    magnitudes.assign (n / 2 + 1, 0.0f);
    frame.assign (n, {});

    for (auto& channel : overlap)
        channel.assign (n, 0.0f);

    // Hann d'analyse (gain cohérent 1/2) puis Hann de synthèse avec 4 trames superposées
    // (somme des carrés = 1,5) ; les phases aléatoires additionnent les trames en puissance.
    normalisation = 2.0f / std::sqrt (1.5f);

    setAttackMilliseconds (400.0f);
    setReleaseMilliseconds (2000.0f);
    reset();
}

void SpectralFreeze::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    historyIndex = 0;

    for (auto& channel : overlap)
        std::fill (channel.begin(), channel.end(), 0.0f);

    overlapIndex = 0;
    samplesUntilFrame = 0;
    hasCapture = false;
    wasHolding = false;
    gain = 0.0f;
}

void SpectralFreeze::setAttackMilliseconds (float ms) noexcept
{
    attackStep = static_cast<float> (1000.0 / (std::max (1.0f, ms) * sampleRate));
}

void SpectralFreeze::setReleaseMilliseconds (float ms) noexcept
{
    releaseStep = static_cast<float> (1000.0 / (std::max (1.0f, ms) * sampleRate));
}

float SpectralFreeze::nextRandomPhase (std::uint32_t& state) noexcept
{
    // xorshift32 : rapide, sans état partagé, suffisant pour des phases.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float> (state) * (2.0f * std::numbers::pi_v<float> / 4294967296.0f);
}

void SpectralFreeze::capture() noexcept
{
    const auto n = static_cast<std::size_t> (size);

    // Les `size` dernières entrées, dans l'ordre chronologique, fenêtrées.
    for (std::size_t i = 0; i < n; ++i)
        frame[i] = { history[(historyIndex + i) % n] * window[i], 0.0f };

    fft.forward (frame.data());

    for (std::size_t k = 0; k < magnitudes.size(); ++k)
        magnitudes[k] = std::abs (frame[k]);

    hasCapture = true;
}

void SpectralFreeze::synthesiseFrame() noexcept
{
    const auto n = static_cast<std::size_t> (size);
    const auto half = n / 2;
    const auto binHz = static_cast<float> (sampleRate) / static_cast<float> (size);
    const auto amplitude = gain * normalisation;

    for (std::size_t channel = 0; channel < overlap.size(); ++channel)
    {
        // Spectre capturé, filtre de tonalité (passe-bas du 2e ordre) et phases aléatoires.
        frame[0] = {};
        frame[half] = {};

        for (std::size_t k = 1; k < half; ++k)
        {
            const auto ratio = static_cast<float> (k) * binHz / toneHz;
            const auto tone = 1.0f / std::sqrt (1.0f + ratio * ratio * ratio * ratio);
            const auto phase = nextRandomPhase (randomState[channel]);
            const auto value = std::polar (magnitudes[k] * tone, phase);

            frame[k] = value;
            frame[n - k] = std::conj (value); // spectre hermitien : signal réel
        }

        fft.inverse (frame.data());

        // Superposition dans l'anneau de sortie, à partir de la position de lecture courante.
        auto& out = overlap[channel];

        for (std::size_t i = 0; i < n; ++i)
            out[(overlapIndex + i) % n] += frame[i].real() * window[i] * amplitude;
    }
}

void SpectralFreeze::process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept
{
    const auto n = static_cast<std::size_t> (size);

    for (std::size_t i = 0; i < input.size(); ++i)
    {
        history[historyIndex] = input[i];
        historyIndex = (historyIndex + 1) % n;

        // Chaque nouvel appui capture le son du moment, même si une nappe sonnait déjà.
        if (hold && ! wasHolding)
            capture();

        wasHolding = hold;

        if (hasCapture)
            gain = hold ? std::min (1.0f, gain + attackStep) : std::max (0.0f, gain - releaseStep);

        if (--samplesUntilFrame <= 0)
        {
            samplesUntilFrame = hop;

            if (gain > 0.0f)
                synthesiseFrame();
        }

        left[i] = overlap[0][overlapIndex];
        right[i] = overlap[1][overlapIndex];
        overlap[0][overlapIndex] = 0.0f;
        overlap[1][overlapIndex] = 0.0f;
        overlapIndex = (overlapIndex + 1) % n;
    }
}

} // namespace subdamp::dsp
