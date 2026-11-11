#include "subdamp/dsp/DelayLineShifter.h"

#include "subdamp/dsp/Interpolation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace subdamp::dsp
{

void DelayLineShifter::prepare (double sampleRate, double windowSeconds)
{
    window = std::max (256.0, std::round (sampleRate * windowSeconds));
    minDelay = margin;
    maxDelay = margin + window;

    // ± 8 ms : couvre une demi-période d'un Do grave (65 Hz), pour toujours trouver la bonne phase.
    search = static_cast<int> (std::round (sampleRate * 0.008));
    correlationLength = static_cast<int> (std::round (sampleRate * 0.01));
    maxCrossfade = std::round (sampleRate * 0.008);

    // Pas de recherche proportionnels à la fréquence : même coût CPU à 48 et 96 kHz.
    coarseStep = std::max (4, static_cast<int> (std::lround (sampleRate / 12000.0)));
    coarseStride = std::max (2, static_cast<int> (std::lround (sampleRate / 24000.0)));

    const auto oldest = maxDelay + search + correlationLength + 8.0;
    ring.assign (std::bit_ceil (static_cast<std::size_t> (oldest)), 0.0f);
    mask = ring.size() - 1;

    reset();
}

void DelayLineShifter::reset() noexcept
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    written = 0;

    delays = { minDelay + 0.5 * window, minDelay + 0.5 * window };
    active = 0;
    crossfading = false;
    crossfadeProgress = 0.0;
}

int DelayLineShifter::averageLatencySamples() const noexcept
{
    return static_cast<int> (minDelay + 0.5 * window);
}

double DelayLineShifter::crossfadeLengthFor (double rate) const noexcept
{
    // Aux grands rapports (±2 octaves), la tête balaie vite la fenêtre : fondu raccourci
    // pour que les deux têtes restent dans la fenêtre pendant le fondu.
    return std::max (16.0, std::min (maxCrossfade, 0.3 * window / std::abs (rate)));
}

float DelayLineShifter::correlation (std::int64_t newest, double delayA, double delayB, int stride) const noexcept
{
    // Compare ce que les deux têtes viennent de lire (le passé est toujours disponible).
    const auto startA = newest - static_cast<std::int64_t> (std::llround (delayA));
    const auto startB = newest - static_cast<std::int64_t> (std::llround (delayB));

    float dot = 0.0f, energy = 1.0e-9f;

    for (int j = 0; j < correlationLength; j += stride)
    {
        const auto a = ring[static_cast<std::size_t> (startA - j) & mask];
        const auto b = ring[static_cast<std::size_t> (startB - j) & mask];
        dot += a * b;
        energy += b * b;
    }

    return dot / std::sqrt (energy);
}

void DelayLineShifter::startSplice (double rate, double crossfadeLength) noexcept
{
    const auto newest = written - 1;
    const auto current = delays[static_cast<std::size_t> (active)];

    // Saut vers l'autre bout de la fenêtre, en gardant la place pour la recherche et le fondu.
    const auto nominal = rate < 0.0 ? maxDelay - search - 1.0 : minDelay + search + 1.0;

    // Recherche grossière puis fine (pas de 1) de la meilleure phase.
    auto best = 0;
    auto bestScore = -1.0e30f;

    for (int offset = -search; offset <= search; offset += coarseStep)
    {
        if (const auto score = correlation (newest, current, nominal + offset, coarseStride); score > bestScore)
        {
            bestScore = score;
            best = offset;
        }
    }

    const auto coarse = best;

    for (int offset = std::max (-search, coarse - coarseStep + 1); offset <= std::min (search, coarse + coarseStep - 1); ++offset)
    {
        if (const auto score = correlation (newest, current, nominal + offset, 1); score > bestScore)
        {
            bestScore = score;
            best = offset;
        }
    }

    // Même partie fractionnaire que la tête active : les deux restent alignées à l'échantillon près.
    const auto fraction = current - std::floor (current);
    delays[static_cast<std::size_t> (1 - active)] = std::floor (nominal) + best + fraction;

    crossfading = true;
    crossfadeProgress = 0.0;
    crossfadeStep = 1.0 / crossfadeLength;
}

float DelayLineShifter::process (float input, float ratio) noexcept
{
    ring[static_cast<std::size_t> (written) & mask] = input;
    ++written;

    // Transposition vers l'aigu : la lecture va plus vite que l'écriture, le retard diminue.
    const auto rate = 1.0 - static_cast<double> (ratio);
    auto& current = delays[static_cast<std::size_t> (active)];
    auto& incoming = delays[static_cast<std::size_t> (1 - active)];

    current += rate;

    if (crossfading)
    {
        incoming += rate;
        crossfadeProgress += crossfadeStep;
    }
    else if (std::abs (rate) > 1.0e-6)
    {
        const auto crossfadeLength = crossfadeLengthFor (rate);
        const auto delayAtEnd = current + rate * crossfadeLength;

        if (delayAtEnd <= minDelay + 1.0 || delayAtEnd >= maxDelay - 1.0)
            startSplice (rate, crossfadeLength);
    }

    // Sécurité : jamais hors de la mémoire disponible.
    current = std::clamp (current, minDelay, maxDelay + search);
    incoming = std::clamp (incoming, minDelay, maxDelay + search);

    const auto newest = static_cast<double> (written - 1);
    const auto out = readHermite (ring.data(), mask, newest - current);

    if (! crossfading)
        return out;

    const auto in = readHermite (ring.data(), mask, newest - incoming);
    const auto s = static_cast<float> (std::sin (0.5 * std::numbers::pi * std::min (1.0, crossfadeProgress)));
    const auto gainIn = s * s;
    const auto result = out + gainIn * (in - out);

    if (crossfadeProgress >= 1.0)
    {
        crossfading = false;
        active = 1 - active;
    }

    return result;
}

} // namespace subdamp::dsp
