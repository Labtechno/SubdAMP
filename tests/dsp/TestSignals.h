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

/** Dent de scie à bande limitée (harmoniques en 1/k jusqu'à 45 % de la fréquence d'échantillonnage). */
inline std::vector<float> sawtooth (double frequency, double sampleRate, int numSamples, float amplitude = 0.3f)
{
    std::vector<float> signal (static_cast<std::size_t> (numSamples), 0.0f);
    const auto harmonics = static_cast<int> (0.45 * sampleRate / frequency);

    for (int k = 1; k <= harmonics; ++k)
    {
        const auto w = 2.0 * std::numbers::pi * frequency * k / sampleRate;
        const auto a = amplitude * 0.6 / k;

        for (int n = 0; n < numSamples; ++n)
            signal[static_cast<std::size_t> (n)] += static_cast<float> (a * std::sin (w * n));
    }

    return signal;
}

/**
    Corde pincée (Karplus-Strong) : un signal réaliste pour les détecteurs de hauteur, avec
    attaque bruitée et harmoniques qui s'éteignent. Fréquence exacte : sampleRate / (delay + 0,5).
*/
inline std::vector<float> pluckedString (int delay, double sampleRate, int numSamples, float amplitude = 0.5f, unsigned seed = 1)
{
    std::vector<float> line (static_cast<std::size_t> (delay));
    unsigned state = seed;

    for (auto& s : line)
    {
        state = state * 1664525u + 1013904223u;
        s = amplitude * (static_cast<float> (state >> 8) / static_cast<float> (1u << 24) * 2.0f - 1.0f);
    }

    std::vector<float> signal (static_cast<std::size_t> (numSamples));
    std::size_t index = 0;
    float previous = 0.0f;
    const auto decay = static_cast<float> (std::pow (0.001, 1.0 / (3.0 * sampleRate / delay))); // -60 dB en ≈ 3 s

    for (auto& out : signal)
    {
        const auto current = line[index];
        out = current;
        line[index] = decay * 0.5f * (current + previous);
        previous = current;
        index = (index + 1) % line.size();
    }

    return signal;
}

inline double pluckedStringFrequency (int delay, double sampleRate)
{
    return sampleRate / (delay + 0.5);
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
