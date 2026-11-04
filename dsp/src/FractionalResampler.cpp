#include "subdamp/dsp/FractionalResampler.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace subdamp::dsp
{

namespace
{
    double sinc (double x)
    {
        if (std::abs (x) < 1.0e-9)
            return 1.0;

        const auto px = std::numbers::pi * x;
        return std::sin (px) / px;
    }

    /** Fenêtre de Blackman-Harris à 4 termes, centrée : 1 en u = 0, ~0 en u = ±1. */
    double blackmanHarris (double u)
    {
        const auto x = std::numbers::pi * u;
        return 0.35875 + 0.48829 * std::cos (x) + 0.14128 * std::cos (2.0 * x) + 0.01168 * std::cos (3.0 * x);
    }

    // Recalage périodique des compteurs absolus, pour garder la précision des doubles.
    // Multiple de toute taille d'historique (puissance de 2) : l'indexation reste valide.
    constexpr std::int64_t rebaseChunk = std::int64_t { 1 } << 30;
}

void FractionalResampler::prepare (double inputRate, double outputRate, int maxInputBlock, int halfTaps)
{
    const auto ratio = outputRate / inputRate;
    const auto bandwidth = std::min (1.0, ratio);
    const auto cutoff = 0.5 * bandwidth * 0.92; // en cycles par échantillon d'entrée

    outputStep = inputRate / outputRate;
    width = halfTaps / bandwidth;

    // Noyau tabulé sur [-width, +width]
    const auto tableSize = static_cast<std::size_t> (std::ceil (2.0 * width * tableResolution)) + 2;
    kernel.assign (tableSize, 0.0f);

    for (std::size_t i = 0; i < tableSize; ++i)
    {
        const auto tau = static_cast<double> (i) / tableResolution - width;

        if (std::abs (tau) <= width)
            kernel[i] = static_cast<float> (2.0 * cutoff * sinc (2.0 * cutoff * tau) * blackmanHarris (tau / width));
    }

    // Normalisation du gain continu : la somme des coefficients vus à un instant donné vaut 1.
    double dcGain = 0.0;
    constexpr int phases = 16;

    for (int p = 0; p < phases; ++p)
    {
        const auto phase = static_cast<double> (p) / phases;

        for (auto k = -std::ceil (width) - 1.0; k <= std::ceil (width) + 1.0; k += 1.0)
        {
            const auto tau = phase - k;

            if (std::abs (tau) <= width)
                dcGain += 2.0 * cutoff * sinc (2.0 * cutoff * tau) * blackmanHarris (tau / width);
        }
    }

    dcGain /= phases;

    for (auto& coefficient : kernel)
        coefficient = static_cast<float> (coefficient / dcGain);

    const auto historySize = std::bit_ceil (static_cast<std::size_t> (std::ceil (2.0 * width)) + static_cast<std::size_t> (maxInputBlock) + 8);
    history.assign (historySize, 0.0f);
    historyMask = historySize - 1;

    reset();
}

void FractionalResampler::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    written = 0;
    nextOutputTime = 0.0;
}

float FractionalResampler::interpolate (double time) const noexcept
{
    const auto first = static_cast<std::int64_t> (std::floor (time - width)) + 1;
    const auto last = static_cast<std::int64_t> (std::floor (time + width));

    float sum = 0.0f;

    for (auto k = first; k <= last; ++k)
    {
        const auto position = (time - static_cast<double> (k) + width) * tableResolution;
        const auto index = static_cast<std::size_t> (position);
        const auto fraction = static_cast<float> (position - static_cast<double> (index));
        const auto coefficient = kernel[index] + fraction * (kernel[index + 1] - kernel[index]);

        sum += history[static_cast<std::size_t> (k) & historyMask] * coefficient;
    }

    return sum;
}

int FractionalResampler::process (const float* input, int numInput, float* output, int maxOutput) noexcept
{
    int produced = 0;

    for (int n = 0; n < numInput; ++n)
    {
        history[static_cast<std::size_t> (written) & historyMask] = input[n];
        ++written;

        const auto newest = static_cast<double> (written - 1);

        while (produced < maxOutput && nextOutputTime + width <= newest)
        {
            output[produced++] = interpolate (nextOutputTime);
            nextOutputTime += outputStep;
        }
    }

    if (written > 2 * rebaseChunk)
    {
        written -= rebaseChunk;
        nextOutputTime -= static_cast<double> (rebaseChunk);
    }

    return produced;
}

} // namespace subdamp::dsp
