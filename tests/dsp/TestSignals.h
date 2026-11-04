#pragma once

#include <cmath>
#include <numbers>
#include <span>
#include <vector>

/** Outils de génération et d'analyse de signaux pour les tests DSP. */
namespace subdamp::test
{

inline std::vector<float> sine (double frequency, double sampleRate, int numSamples, float amplitude = 0.5f)
{
    std::vector<float> signal (static_cast<std::size_t> (numSamples));

    for (int n = 0; n < numSamples; ++n)
        signal[static_cast<std::size_t> (n)] = amplitude * static_cast<float> (std::sin (2.0 * std::numbers::pi * frequency * n / sampleRate));

    return signal;
}

/** Amplitude de la composante à `frequency` (algorithme de Goertzel, fenêtre de Hann). */
inline double magnitudeAt (std::span<const float> signal, double frequency, double sampleRate)
{
    const auto n = signal.size();
    const auto omega = 2.0 * std::numbers::pi * frequency / sampleRate;
    const auto coefficient = 2.0 * std::cos (omega);

    double s1 = 0.0, s2 = 0.0, windowSum = 0.0;

    for (std::size_t i = 0; i < n; ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (n - 1));
        windowSum += window;

        const auto s = signal[i] * window + coefficient * s1 - s2;
        s2 = s1;
        s1 = s;
    }

    const auto power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
    return 2.0 * std::sqrt (std::max (0.0, power)) / windowSum;
}

inline double rms (std::span<const float> signal)
{
    double sum = 0.0;

    for (const auto s : signal)
        sum += static_cast<double> (s) * s;

    return signal.empty() ? 0.0 : std::sqrt (sum / static_cast<double> (signal.size()));
}

/** Traite un signal complet par blocs de taille fixe. */
template <typename Effect>
void processInBlocks (Effect& effect, std::vector<float>& signal, int blockSize)
{
    for (std::size_t start = 0; start < signal.size(); start += static_cast<std::size_t> (blockSize))
    {
        const auto size = std::min (static_cast<std::size_t> (blockSize), signal.size() - start);
        effect.process (std::span<float> { signal.data() + start, size });
    }
}

} // namespace subdamp::test
